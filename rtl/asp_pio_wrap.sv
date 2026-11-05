// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
`default_nettype none
// PIO tile (pio2 submodule, AXI4-Lite register port, AXI-Stream FIFO ports) as an AXI4-Lite slave of
// AbstractX's control plane, plus the TLP data path. No Wishbone (2026-11-05).
// Registers: AXI4-Lite -> Forencich axil_reg_if; the TLP data path registers moved out of the tile
// (pio2 [SPEC-ICN-09]) are served here, every other access is forwarded to the tile's AXI4-Lite port.
// Data: TLP ingress queue -> tile TX streams; tile RX streams -> TLP egress (rtl/pio_tlp/).
// @impl [SPEC-TLP-05] [SPEC-TLP-06] docs/DESIGN_SPECIFICATION.md
module asp_pio_wrap #(
    parameter logic [31:0] BASE_ADDR = 32'h40000700,
    parameter int          TILE_ID   = 0
) (
    input  wire          clk,
    input  wire          rst_n,
    input  wire [63:0]   i_sys_timestamp,
    input  wire [31:0]   i_gpio,
    output logic [31:0]  o_gpio_out,
    output logic [31:0]  o_gpio_pindir,
    // AXI4-Lite slave (from the control plane crossbar)
    input  wire  [31:0]  s_axil_awaddr,
    input  wire  [2:0]   s_axil_awprot,
    input  wire          s_axil_awvalid,
    output logic         s_axil_awready,
    input  wire  [31:0]  s_axil_wdata,
    input  wire  [3:0]   s_axil_wstrb,
    input  wire          s_axil_wvalid,
    output logic         s_axil_wready,
    output logic [1:0]   s_axil_bresp,
    output logic         s_axil_bvalid,
    input  wire          s_axil_bready,
    input  wire  [31:0]  s_axil_araddr,
    input  wire  [2:0]   s_axil_arprot,
    input  wire          s_axil_arvalid,
    output logic         s_axil_arready,
    output logic [31:0]  s_axil_rdata,
    output logic [1:0]   s_axil_rresp,
    output logic         s_axil_rvalid,
    input  wire          s_axil_rready,
    // TLP ports ([SPEC-TLP-06])
    input  wire [511:0]  s_tlp_tdata,
    input  wire          s_tlp_tvalid,
    output logic         s_tlp_tready,
    output logic [511:0] m_tlp_tdata,
    output logic         m_tlp_tvalid,
    input  wire          m_tlp_tready,
    output logic         o_pio_irq
);
    // ------------------------------------------------------------------------
    // AXI4-Lite -> register port (Forencich axil_reg_if); the TLP data path offsets are local
    // ([SPEC-TLP-05]), every other access is forwarded to the tile's AXI4-Lite port
    // ------------------------------------------------------------------------
    logic [31:0] reg_wr_addr, reg_wr_data, reg_rd_addr, reg_rd_data;
    logic [3:0]  reg_wr_strb;
    logic        reg_wr_en, reg_rd_en, reg_wr_wait, reg_rd_wait, reg_wr_ack, reg_rd_ack;

    axil_reg_if #(.DATA_WIDTH(32), .ADDR_WIDTH(32)) u_if (
        .clk(clk), .rst(!rst_n),
        .s_axil_awaddr(s_axil_awaddr), .s_axil_awprot(s_axil_awprot), .s_axil_awvalid(s_axil_awvalid),
        .s_axil_awready(s_axil_awready), .s_axil_wdata(s_axil_wdata), .s_axil_wstrb(s_axil_wstrb),
        .s_axil_wvalid(s_axil_wvalid), .s_axil_wready(s_axil_wready), .s_axil_bresp(s_axil_bresp),
        .s_axil_bvalid(s_axil_bvalid), .s_axil_bready(s_axil_bready),
        .s_axil_araddr(s_axil_araddr), .s_axil_arprot(s_axil_arprot), .s_axil_arvalid(s_axil_arvalid),
        .s_axil_arready(s_axil_arready), .s_axil_rdata(s_axil_rdata), .s_axil_rresp(s_axil_rresp),
        .s_axil_rvalid(s_axil_rvalid), .s_axil_rready(s_axil_rready),
        .reg_wr_addr(reg_wr_addr), .reg_wr_data(reg_wr_data), .reg_wr_strb(reg_wr_strb),
        .reg_wr_en(reg_wr_en), .reg_wr_wait(reg_wr_wait), .reg_wr_ack(reg_wr_ack),
        .reg_rd_addr(reg_rd_addr), .reg_rd_en(reg_rd_en), .reg_rd_data(reg_rd_data),
        .reg_rd_wait(reg_rd_wait), .reg_rd_ack(reg_rd_ack)
    );

    function automatic logic is_local(input logic [31:0] a);
        logic [11:0] off;
        off = a[11:0] - BASE_ADDR[11:0];
        is_local = off == 12'h00C || off == 12'h090 || off == 12'h094 || off == 12'h098 ||
               (off[11:8] == 4'h1 && off[4:0] == 5'h1C && off[7:5] <= 3'd3);   // SMx + 0x1C
    endfunction
    wire wr_local = is_local(reg_wr_addr);
    wire rd_local = is_local(reg_rd_addr);
    wire [11:0] wr_off = reg_wr_addr[11:0] - BASE_ADDR[11:0];
    wire [11:0] rd_off = reg_rd_addr[11:0] - BASE_ADDR[11:0];

    logic        tlp_en, trace_en, in_en;
    logic [3:0]  out_mask, burst_words;
    logic [7:0]  tlp_tag, tlp_channel, in_channel;
    logic [23:0] out_timeout;
    logic        sm_trigger_pulse;
    logic [5:0]  in_queued;
    logic        in_busy;

    // Tile AXI4-Lite master side
    logic [31:0] t_awaddr, t_wdata, t_araddr, t_rdata;
    logic        t_awvalid, t_awready, t_wvalid, t_wready, t_bvalid, t_bready;
    logic        t_arvalid, t_arready, t_rvalid, t_rready;
    logic [1:0]  t_bresp, t_rresp;
    logic        wr_busy, rd_busy;                 // a forwarded access is in flight

    // Forwarded accesses keep axil_reg_if waiting (its timeout is held) until the tile answers.
    assign reg_wr_wait = reg_wr_en && !wr_local && !reg_wr_ack;
    assign reg_rd_wait = reg_rd_en && !rd_local && !reg_rd_ack;
    assign t_bready    = 1'b1;
    assign t_rready    = 1'b1;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tlp_en <= 1'b0; trace_en <= 1'b0; in_en <= 1'b0;
            out_mask <= 4'd0; burst_words <= 4'd0;
            tlp_tag <= 8'd0; tlp_channel <= 8'd0; in_channel <= 8'd0;
            out_timeout <= 24'd0;
            sm_trigger_pulse <= 1'b0;
            reg_wr_ack <= 1'b0; reg_rd_ack <= 1'b0; reg_rd_data <= 32'd0;
            wr_busy <= 1'b0; rd_busy <= 1'b0;
            t_awaddr <= 32'd0; t_awvalid <= 1'b0; t_wdata <= 32'd0; t_wvalid <= 1'b0;
            t_araddr <= 32'd0; t_arvalid <= 1'b0;
        end else begin
            reg_wr_ack       <= 1'b0;
            reg_rd_ack       <= 1'b0;
            sm_trigger_pulse <= 1'b0;

            // ---------------- writes ----------------
            if (reg_wr_en && !reg_wr_ack && wr_local) begin
                reg_wr_ack <= 1'b1;
                unique case (wr_off)
                    12'h00C: begin                                     // OUT_CFG
                        tlp_en      <= reg_wr_data[0];
                        out_mask    <= reg_wr_data[7:4];
                        tlp_tag     <= reg_wr_data[15:8];
                        tlp_channel <= reg_wr_data[23:16];
                        burst_words <= reg_wr_data[27:24];
                        trace_en    <= reg_wr_data[31];
                    end
                    12'h090: out_timeout <= reg_wr_data[23:0];           // OUT_TIMEOUT
                    12'h094: begin                                     // IN_CFG
                        in_en      <= reg_wr_data[0];
                        in_channel <= reg_wr_data[15:8];
                    end
                    12'h098: ;                                         // IN_STATUS: read-only
                    default: sm_trigger_pulse <= 1'b1;                 // SMx + 0x1C
                endcase
            end else if (reg_wr_en && !reg_wr_ack && !wr_local && !wr_busy) begin
                t_awaddr  <= reg_wr_addr;
                t_awvalid <= 1'b1;
                t_wdata   <= reg_wr_data;
                t_wvalid  <= 1'b1;
                wr_busy   <= 1'b1;
            end
            if (t_awvalid && t_awready) t_awvalid <= 1'b0;
            if (t_wvalid && t_wready)   t_wvalid  <= 1'b0;
            if (wr_busy && t_bvalid) begin
                wr_busy    <= 1'b0;
                reg_wr_ack <= 1'b1;
            end

            // ---------------- reads ----------------
            if (reg_rd_en && !reg_rd_ack && rd_local) begin
                reg_rd_ack <= 1'b1;
                unique case (rd_off)
                    12'h00C: reg_rd_data <= {trace_en, 3'd0, burst_words, tlp_channel, tlp_tag,
                                             out_mask, 3'd0, tlp_en};
                    12'h090: reg_rd_data <= {8'd0, out_timeout};
                    12'h094: reg_rd_data <= {16'd0, in_channel, 7'd0, in_en};
                    12'h098: reg_rd_data <= {23'd0, in_busy, 2'd0, in_queued};   // IN_STATUS
                    default: reg_rd_data <= 32'd0;                                // trigger reads 0
                endcase
            end else if (reg_rd_en && !reg_rd_ack && !rd_local && !rd_busy) begin
                t_araddr  <= reg_rd_addr;
                t_arvalid <= 1'b1;
                rd_busy   <= 1'b1;
            end
            if (t_arvalid && t_arready) t_arvalid <= 1'b0;
            if (rd_busy && t_rvalid) begin
                rd_busy     <= 1'b0;
                reg_rd_ack  <= 1'b1;
                reg_rd_data <= t_rdata;
            end
        end
    end

    // ------------------------------------------------------------------------
    // Tile
    // ------------------------------------------------------------------------
    logic [127:0] tx_tdata, rx_tdata;
    logic [3:0]   tx_tvalid, tx_tready, rx_tvalid, rx_tready;
    logic [19:0]  obs_pc;
    logic [3:0]   obs_stalled, obs_tx_empty, obs_rx_full, obs_active;
    logic [7:0]   obs_irq;

    asp_pio_tile #(.BASE_ADDR(BASE_ADDR)) u_tile (
        .clk(clk), .rst_n(rst_n), .i_gpio(i_gpio),
        .o_gpio_out(o_gpio_out), .o_gpio_pindir(o_gpio_pindir),
        .s_axil_awaddr(t_awaddr), .s_axil_awvalid(t_awvalid), .s_axil_awready(t_awready),
        .s_axil_wdata(t_wdata), .s_axil_wstrb(4'hF), .s_axil_wvalid(t_wvalid), .s_axil_wready(t_wready),
        .s_axil_bresp(t_bresp), .s_axil_bvalid(t_bvalid), .s_axil_bready(t_bready),
        .s_axil_araddr(t_araddr), .s_axil_arvalid(t_arvalid), .s_axil_arready(t_arready),
        .s_axil_rdata(t_rdata), .s_axil_rresp(t_rresp), .s_axil_rvalid(t_rvalid), .s_axil_rready(t_rready),
        .s_axis_tx_tdata(tx_tdata), .s_axis_tx_tvalid(tx_tvalid), .s_axis_tx_tready(tx_tready),
        .m_axis_rx_tdata(rx_tdata), .m_axis_rx_tvalid(rx_tvalid), .m_axis_rx_tready(rx_tready),
        .o_pio_irq(o_pio_irq),
        .o_obs_pc(obs_pc), .o_obs_stalled(obs_stalled), .o_obs_tx_empty(obs_tx_empty),
        .o_obs_rx_full(obs_rx_full), .o_obs_active(obs_active), .o_obs_irq(obs_irq)
    );

    // ------------------------------------------------------------------------
    // TLP data path ([SPEC-TLP-06])
    // ------------------------------------------------------------------------
    // Ingress: a group's lanes get TVALID only in a clock where all of them have TREADY (the tile's
    // TREADY never depends on TVALID, and already drops in a clock its register port writes TXFn).
    asp_pio_tlp_ingress u_tlp_ingress (
        .clk(clk), .rst_n(rst_n),
        .i_en(in_en), .i_channel(in_channel),
        .s_tlp_tdata(s_tlp_tdata), .s_tlp_tvalid(s_tlp_tvalid), .s_tlp_tready(s_tlp_tready),
        .i_tx_ready(tx_tready), .i_tx_block(4'd0),
        .o_tx_wvalid(tx_tvalid), .o_tx_wdata(tx_tdata),
        .o_queued(in_queued), .o_busy(in_busy)
    );

    // IRQ trace events: flags that rise on the observation port (the tile no longer exports set pulses)
    logic [7:0] obs_irq_q;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) obs_irq_q <= 8'd0;
        else        obs_irq_q <= obs_irq;
    end

    // Egress: the round pop is the RX TREADY; the tile's RX TVALID already drops in a clock its
    // register port reads RXFn, so no separate block input is needed.
    asp_pio_tlp_shim #(.BASE_ADDR(BASE_ADDR)) u_tlp_shim (
        .clk(clk), .rst_n(rst_n),
        .i_sys_timestamp(i_sys_timestamp),
        .i_tlp_en(tlp_en), .i_mask(out_mask), .i_tlp_tag(tlp_tag), .i_tlp_channel(tlp_channel),
        .i_burst_words(burst_words), .i_idle_timeout(out_timeout), .i_trace_en(trace_en),
        .i_tile_id(8'(TILE_ID)),
        .i_rx_data(rx_tdata), .i_rx_valid(rx_tvalid), .i_rx_block(4'd0), .o_rx_pop(rx_tready),
        .i_sm_trigger(sm_trigger_pulse),
        .i_sm_active(obs_active), .i_sm_stalled(obs_stalled), .i_sm_pc(obs_pc),
        .i_irq_pulse(obs_irq & ~obs_irq_q),
        .m_tlp_tdata(m_tlp_tdata), .m_tlp_tvalid(m_tlp_tvalid), .m_tlp_tready(m_tlp_tready)
    );

    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused = &{1'b0, reg_wr_strb, t_bresp, t_rresp, reg_wr_addr[31:12], reg_rd_addr[31:12],
                     obs_tx_empty, obs_rx_full};
    /* verilator lint_on UNUSEDSIGNAL */
endmodule
`default_nettype wire
