// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
`default_nettype none
// PIO tile (pio2 submodule, AXI4-Lite register port, AXI-Stream FIFO ports) behind AbstractX's
// Wishbone B4 classic fabric and TLP router.
// Registers: classic -> pipelined (one request per transaction), ZipCPU wbm2axilite (Apache-2.0, used
// from rtl/pio/third_party/wb2axip unmodified), then the tile. The TLP data path registers moved out of
// the tile (pio2 [SPEC-ICN-09]) are served here and never reach the bridge.
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
    // Wishbone B4 classic slave (AbstractX fabric)
    input  wire          wb_cyc_i,
    input  wire          wb_stb_i,
    input  wire          wb_we_i,
    input  wire [31:0]   wb_adr_i,
    input  wire [31:0]   wb_dat_i,
    output logic [31:0]  wb_dat_o,
    output logic         wb_ack_o,
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
    // Register decode: the TLP data path offsets are local ([SPEC-TLP-05] tile split)
    // ------------------------------------------------------------------------
    wire [11:0] reg_offset = wb_adr_i[11:0] - BASE_ADDR[11:0];
    wire is_trigger = reg_offset[11:8] == 4'h1 && reg_offset[4:0] == 5'h1C &&
                      reg_offset[7:5] <= 3'd3;                          // SMx + 0x1C, x = 0..3
    wire is_local   = reg_offset == 12'h00C || reg_offset == 12'h090 ||
                      reg_offset == 12'h094 || reg_offset == 12'h098 || is_trigger;
    wire wb_req     = wb_cyc_i && wb_stb_i;

    logic        tlp_en, trace_en, in_en;
    logic [3:0]  out_mask, burst_words;
    logic [7:0]  tlp_tag, tlp_channel, in_channel;
    logic [23:0] out_timeout;
    logic        sm_trigger_pulse;
    logic [5:0]  in_queued;
    logic        in_busy;
    logic        local_ack;
    logic [31:0] local_dat;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tlp_en <= 1'b0; trace_en <= 1'b0; in_en <= 1'b0;
            out_mask <= 4'd0; burst_words <= 4'd0;
            tlp_tag <= 8'd0; tlp_channel <= 8'd0; in_channel <= 8'd0;
            out_timeout <= 24'd0;
            sm_trigger_pulse <= 1'b0;
            local_ack <= 1'b0;
            local_dat <= 32'd0;
        end else begin
            local_ack        <= 1'b0;
            sm_trigger_pulse <= 1'b0;
            if (wb_req && is_local && !local_ack) begin
                local_ack <= 1'b1;
                local_dat <= 32'd0;
                if (wb_we_i) begin
                    case (reg_offset)
                        12'h00C: begin                                     // OUT_CFG
                            tlp_en      <= wb_dat_i[0];
                            out_mask    <= wb_dat_i[7:4];
                            tlp_tag     <= wb_dat_i[15:8];
                            tlp_channel <= wb_dat_i[23:16];
                            burst_words <= wb_dat_i[27:24];
                            trace_en    <= wb_dat_i[31];
                        end
                        12'h090: out_timeout <= wb_dat_i[23:0];             // OUT_TIMEOUT
                        12'h094: begin                                     // IN_CFG
                            in_en      <= wb_dat_i[0];
                            in_channel <= wb_dat_i[15:8];
                        end
                        default: if (is_trigger) sm_trigger_pulse <= 1'b1; // SMx + 0x1C
                    endcase
                end else begin
                    case (reg_offset)
                        12'h00C: local_dat <= {trace_en, 3'd0, burst_words, tlp_channel, tlp_tag,
                                               out_mask, 3'd0, tlp_en};
                        12'h090: local_dat <= {8'd0, out_timeout};
                        12'h094: local_dat <= {16'd0, in_channel, 7'd0, in_en};
                        12'h098: local_dat <= {23'd0, in_busy, 2'd0, in_queued};    // IN_STATUS
                        default: local_dat <= 32'd0;                                 // trigger reads 0
                    endcase
                end
            end
        end
    end

    // ------------------------------------------------------------------------
    // Everything else: Wishbone -> AXI4-Lite bridge -> tile ([SPEC-TLP-05])
    // ------------------------------------------------------------------------
    // wbm2axilite has a synchronous reset; rst_n is an asynchronous reset everywhere else. A two-flop
    // synchroniser (rst_n only as its async reset) gives the bridge its own flopped reset, so no net is
    // used both ways (rtl-lint SYNCASYNCNET); the bridge leaves reset two clocks after the tile.
    logic [1:0] rst_sync;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) rst_sync <= 2'b00;
        else        rst_sync <= {rst_sync[0], 1'b1};
    end
    wire bridge_reset = !rst_sync[1];

    // Classic master holds stb until ack; a pipelined slave takes stb && !stall as a new request.
    // Issue once, re-arm when the answer arrives (the master moves to its next request on that edge).
    logic pending;
    logic stall, b_ack, b_err;
    logic [31:0] b_dat;
    wire  p_stb = wb_req && !is_local && !pending;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)                 pending <= 1'b0;
        else if (b_ack || b_err)    pending <= 1'b0;
        else if (p_stb && !stall)   pending <= 1'b1;
    end
    // AbstractX's fabric has no err: an AXI error still ends the cycle
    assign wb_ack_o = b_ack || b_err || local_ack;
    assign wb_dat_o = local_ack ? local_dat : b_dat;

    logic [31:0] awaddr, araddr, wdata, rdata;
    logic [3:0]  wstrb;
    logic [1:0]  bresp, rresp;
    logic        awvalid, awready, wvalid, wready, bvalid, bready, arvalid, arready, rvalid, rready;
    logic [2:0]  awprot, arprot;

    wbm2axilite #(.C_AXI_ADDR_WIDTH(32)) u_bridge (
        .i_clk(clk), .i_reset(bridge_reset),
        .i_wb_cyc(wb_cyc_i && !is_local), .i_wb_stb(p_stb), .i_wb_we(wb_we_i), .i_wb_addr(wb_adr_i[31:2]),
        .i_wb_data(wb_dat_i), .i_wb_sel(4'hF),
        .o_wb_stall(stall), .o_wb_ack(b_ack), .o_wb_data(b_dat), .o_wb_err(b_err),
        .o_axi_awvalid(awvalid), .i_axi_awready(awready), .o_axi_awaddr(awaddr), .o_axi_awprot(awprot),
        .o_axi_wvalid(wvalid), .i_axi_wready(wready), .o_axi_wdata(wdata), .o_axi_wstrb(wstrb),
        .i_axi_bvalid(bvalid), .o_axi_bready(bready), .i_axi_bresp(bresp),
        .o_axi_arvalid(arvalid), .i_axi_arready(arready), .o_axi_araddr(araddr), .o_axi_arprot(arprot),
        .i_axi_rvalid(rvalid), .o_axi_rready(rready), .i_axi_rdata(rdata), .i_axi_rresp(rresp)
    );

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
        .s_axil_awaddr(awaddr), .s_axil_awvalid(awvalid), .s_axil_awready(awready),
        .s_axil_wdata(wdata), .s_axil_wstrb(wstrb), .s_axil_wvalid(wvalid), .s_axil_wready(wready),
        .s_axil_bresp(bresp), .s_axil_bvalid(bvalid), .s_axil_bready(bready),
        .s_axil_araddr(araddr), .s_axil_arvalid(arvalid), .s_axil_arready(arready),
        .s_axil_rdata(rdata), .s_axil_rresp(rresp), .s_axil_rvalid(rvalid), .s_axil_rready(rready),
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
    wire _unused = &{1'b0, awprot, arprot, wb_adr_i[1:0], obs_tx_empty, obs_rx_full};
    /* verilator lint_on UNUSEDSIGNAL */
endmodule
`default_nettype wire
