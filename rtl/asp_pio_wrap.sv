// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
`default_nettype none
// PIO tile (pio2 submodule, AXI4-Lite register port) behind AbstractX's Wishbone B4 classic fabric.
// Classic -> pipelined (one request per transaction), ZipCPU wbm2axilite (Apache-2.0, used from
// rtl/pio/third_party/wb2axip unmodified), then asp_pio_tile. Same port list as the tile used to have.
// @impl [SPEC-TLP-05] docs/DESIGN_SPECIFICATION.md
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
    // TLP ports passed through to the tile ([SPEC-PIO-10] until the pio2 split, pio2 [SPEC-ICN-09])
    input  wire [511:0]  s_tlp_tdata,
    input  wire          s_tlp_tvalid,
    output logic         s_tlp_tready,
    output logic [511:0] m_tlp_tdata,
    output logic         m_tlp_tvalid,
    input  wire          m_tlp_tready,
    output logic         o_pio_irq
);
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
    wire  p_stb = wb_cyc_i && wb_stb_i && !pending;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n)                 pending <= 1'b0;
        else if (b_ack || b_err)    pending <= 1'b0;
        else if (p_stb && !stall)   pending <= 1'b1;
    end
    assign wb_ack_o = b_ack || b_err;   // AbstractX's fabric has no err: an AXI error still ends the cycle

    logic [31:0] awaddr, araddr, wdata, rdata;
    logic [3:0]  wstrb;
    logic [1:0]  bresp, rresp;
    logic        awvalid, awready, wvalid, wready, bvalid, bready, arvalid, arready, rvalid, rready;
    logic [2:0]  awprot, arprot;

    wbm2axilite #(.C_AXI_ADDR_WIDTH(32)) u_bridge (
        .i_clk(clk), .i_reset(bridge_reset),
        .i_wb_cyc(wb_cyc_i), .i_wb_stb(p_stb), .i_wb_we(wb_we_i), .i_wb_addr(wb_adr_i[31:2]),
        .i_wb_data(wb_dat_i), .i_wb_sel(4'hF),
        .o_wb_stall(stall), .o_wb_ack(b_ack), .o_wb_data(wb_dat_o), .o_wb_err(b_err),
        .o_axi_awvalid(awvalid), .i_axi_awready(awready), .o_axi_awaddr(awaddr), .o_axi_awprot(awprot),
        .o_axi_wvalid(wvalid), .i_axi_wready(wready), .o_axi_wdata(wdata), .o_axi_wstrb(wstrb),
        .i_axi_bvalid(bvalid), .o_axi_bready(bready), .i_axi_bresp(bresp),
        .o_axi_arvalid(arvalid), .i_axi_arready(arready), .o_axi_araddr(araddr), .o_axi_arprot(arprot),
        .i_axi_rvalid(rvalid), .o_axi_rready(rready), .i_axi_rdata(rdata), .i_axi_rresp(rresp)
    );

    asp_pio_tile #(.BASE_ADDR(BASE_ADDR), .TILE_ID(TILE_ID)) u_tile (
        .clk(clk), .rst_n(rst_n), .i_sys_timestamp(i_sys_timestamp), .i_gpio(i_gpio),
        .o_gpio_out(o_gpio_out), .o_gpio_pindir(o_gpio_pindir),
        .s_axil_awaddr(awaddr), .s_axil_awvalid(awvalid), .s_axil_awready(awready),
        .s_axil_wdata(wdata), .s_axil_wstrb(wstrb), .s_axil_wvalid(wvalid), .s_axil_wready(wready),
        .s_axil_bresp(bresp), .s_axil_bvalid(bvalid), .s_axil_bready(bready),
        .s_axil_araddr(araddr), .s_axil_arvalid(arvalid), .s_axil_arready(arready),
        .s_axil_rdata(rdata), .s_axil_rresp(rresp), .s_axil_rvalid(rvalid), .s_axil_rready(rready),
        .s_tlp_tdata(s_tlp_tdata), .s_tlp_tvalid(s_tlp_tvalid), .s_tlp_tready(s_tlp_tready),
        .m_tlp_tdata(m_tlp_tdata), .m_tlp_tvalid(m_tlp_tvalid), .m_tlp_tready(m_tlp_tready),
        .o_pio_irq(o_pio_irq),
        // pio2 trace observation port: AbstractX traces through its TLP path, not used here
        .o_obs_pc(obs_pc), .o_obs_stalled(obs_stalled), .o_obs_tx_empty(obs_tx_empty),
        .o_obs_rx_full(obs_rx_full), .o_obs_irq(obs_irq)
    );
    logic [19:0] obs_pc;
    logic [3:0]  obs_stalled, obs_tx_empty, obs_rx_full;
    logic [7:0]  obs_irq;
    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused = &{1'b0, awprot, arprot, wb_adr_i[1:0],   // word addresses; prot not used by the tile
                     obs_pc, obs_stalled, obs_tx_empty, obs_rx_full, obs_irq};
    /* verilator lint_on UNUSEDSIGNAL */
endmodule
`default_nettype wire
