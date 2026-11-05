// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
`default_nettype none
// Control plane shared by every top ([SPEC-TLP-07], [SPEC-TLP-04]): CONTROL TLPs -> asp_tlp_axil_master
// -> Forencich axil_crossbar (unmodified, pio2 third_party/verilog-axi) -> asp_sys_regs (0x40000000/256 B)
// and an AXI4-Lite port for the PIO tile (0x40000700/256 B + 0x40000800/256 B). Anything else: DECERR.
// No Wishbone (2026-11-05).
// @impl [SPEC-TLP-07] [SPEC-TLP-04] docs/DESIGN_SPECIFICATION.md
module asp_ctrl_plane (
    input  wire          clk,
    input  wire          rst_n,
    input  wire  [63:0]  i_sys_timestamp,

    input  wire  [511:0] s_tlp_tdata,          // CONTROL TLPs
    input  wire          s_tlp_tvalid,
    output logic         s_tlp_tready,
    output logic [511:0] m_cpl_tdata,          // completions
    output logic         m_cpl_tvalid,
    input  wire          m_cpl_tready,

    // AXI4-Lite master port to the PIO tile wrapper
    output logic [31:0]  m_pio_awaddr,
    output logic [2:0]   m_pio_awprot,
    output logic         m_pio_awvalid,
    input  wire          m_pio_awready,
    output logic [31:0]  m_pio_wdata,
    output logic [3:0]   m_pio_wstrb,
    output logic         m_pio_wvalid,
    input  wire          m_pio_wready,
    input  wire  [1:0]   m_pio_bresp,
    input  wire          m_pio_bvalid,
    output logic         m_pio_bready,
    output logic [31:0]  m_pio_araddr,
    output logic [2:0]   m_pio_arprot,
    output logic         m_pio_arvalid,
    input  wire          m_pio_arready,
    input  wire  [31:0]  m_pio_rdata,
    input  wire  [1:0]   m_pio_rresp,
    input  wire          m_pio_rvalid,
    output logic         m_pio_rready,

    output logic [5:0]   o_led_bits
);
    // Master -> crossbar slave port
    logic [31:0] awaddr, wdata, araddr, rdata;
    logic [2:0]  awprot, arprot;
    logic [3:0]  wstrb;
    logic [1:0]  bresp, rresp;
    logic        awvalid, awready, wvalid, wready, bvalid, bready, arvalid, arready, rvalid, rready;
    logic        wr_err;

    asp_tlp_axil_master u_master (
        .clk(clk), .rst_n(rst_n),
        .s_tlp_tdata(s_tlp_tdata), .s_tlp_tvalid(s_tlp_tvalid), .s_tlp_tready(s_tlp_tready),
        .m_cpl_tdata(m_cpl_tdata), .m_cpl_tvalid(m_cpl_tvalid), .m_cpl_tready(m_cpl_tready),
        .m_axil_awaddr(awaddr), .m_axil_awprot(awprot), .m_axil_awvalid(awvalid), .m_axil_awready(awready),
        .m_axil_wdata(wdata), .m_axil_wstrb(wstrb), .m_axil_wvalid(wvalid), .m_axil_wready(wready),
        .m_axil_bresp(bresp), .m_axil_bvalid(bvalid), .m_axil_bready(bready),
        .m_axil_araddr(araddr), .m_axil_arprot(arprot), .m_axil_arvalid(arvalid), .m_axil_arready(arready),
        .m_axil_rdata(rdata), .m_axil_rresp(rresp), .m_axil_rvalid(rvalid), .m_axil_rready(rready),
        .o_wr_err(wr_err)
    );

    // Crossbar master ports: 0 = system registers, 1 = PIO tile
    localparam int M = 2;
    logic [M*32-1:0] x_awaddr, x_wdata, x_araddr, x_rdata;
    logic [M*3-1:0]  x_awprot, x_arprot;
    logic [M*4-1:0]  x_wstrb;
    logic [M*2-1:0]  x_bresp, x_rresp;
    logic [M-1:0]    x_awvalid, x_awready, x_wvalid, x_wready, x_bvalid, x_bready;
    logic [M-1:0]    x_arvalid, x_arready, x_rvalid, x_rready;

    axil_crossbar #(
        .S_COUNT(1), .M_COUNT(M), .DATA_WIDTH(32), .ADDR_WIDTH(32), .M_REGIONS(2),
        // {pio r1, pio r0, sys r1 (unused), sys r0}
        .M_BASE_ADDR({32'h4000_0800, 32'h4000_0700, 32'h0000_0000, 32'h4000_0000}),
        .M_ADDR_WIDTH({32'd8, 32'd8, 32'd0, 32'd8})
    ) u_xbar (
        .clk(clk), .rst(!rst_n),
        .s_axil_awaddr(awaddr), .s_axil_awprot(awprot), .s_axil_awvalid(awvalid), .s_axil_awready(awready),
        .s_axil_wdata(wdata), .s_axil_wstrb(wstrb), .s_axil_wvalid(wvalid), .s_axil_wready(wready),
        .s_axil_bresp(bresp), .s_axil_bvalid(bvalid), .s_axil_bready(bready),
        .s_axil_araddr(araddr), .s_axil_arprot(arprot), .s_axil_arvalid(arvalid), .s_axil_arready(arready),
        .s_axil_rdata(rdata), .s_axil_rresp(rresp), .s_axil_rvalid(rvalid), .s_axil_rready(rready),
        .m_axil_awaddr(x_awaddr), .m_axil_awprot(x_awprot), .m_axil_awvalid(x_awvalid),
        .m_axil_awready(x_awready), .m_axil_wdata(x_wdata), .m_axil_wstrb(x_wstrb),
        .m_axil_wvalid(x_wvalid), .m_axil_wready(x_wready), .m_axil_bresp(x_bresp),
        .m_axil_bvalid(x_bvalid), .m_axil_bready(x_bready),
        .m_axil_araddr(x_araddr), .m_axil_arprot(x_arprot), .m_axil_arvalid(x_arvalid),
        .m_axil_arready(x_arready), .m_axil_rdata(x_rdata), .m_axil_rresp(x_rresp),
        .m_axil_rvalid(x_rvalid), .m_axil_rready(x_rready)
    );

    asp_sys_regs u_sys_regs (
        .clk(clk), .rst_n(rst_n), .i_sys_timestamp(i_sys_timestamp), .i_wr_err(wr_err),
        .s_axil_awaddr(x_awaddr[0 +: 32]), .s_axil_awprot(x_awprot[0 +: 3]), .s_axil_awvalid(x_awvalid[0]),
        .s_axil_awready(x_awready[0]), .s_axil_wdata(x_wdata[0 +: 32]), .s_axil_wstrb(x_wstrb[0 +: 4]),
        .s_axil_wvalid(x_wvalid[0]), .s_axil_wready(x_wready[0]), .s_axil_bresp(x_bresp[0 +: 2]),
        .s_axil_bvalid(x_bvalid[0]), .s_axil_bready(x_bready[0]),
        .s_axil_araddr(x_araddr[0 +: 32]), .s_axil_arprot(x_arprot[0 +: 3]), .s_axil_arvalid(x_arvalid[0]),
        .s_axil_arready(x_arready[0]), .s_axil_rdata(x_rdata[0 +: 32]), .s_axil_rresp(x_rresp[0 +: 2]),
        .s_axil_rvalid(x_rvalid[0]), .s_axil_rready(x_rready[0]),
        .o_led_bits(o_led_bits)
    );

    assign m_pio_awaddr   = x_awaddr[32 +: 32];
    assign m_pio_awprot   = x_awprot[3 +: 3];
    assign m_pio_awvalid  = x_awvalid[1];
    assign x_awready[1]   = m_pio_awready;
    assign m_pio_wdata    = x_wdata[32 +: 32];
    assign m_pio_wstrb    = x_wstrb[4 +: 4];
    assign m_pio_wvalid   = x_wvalid[1];
    assign x_wready[1]    = m_pio_wready;
    assign x_bresp[2 +: 2] = m_pio_bresp;
    assign x_bvalid[1]    = m_pio_bvalid;
    assign m_pio_bready   = x_bready[1];
    assign m_pio_araddr   = x_araddr[32 +: 32];
    assign m_pio_arprot   = x_arprot[3 +: 3];
    assign m_pio_arvalid  = x_arvalid[1];
    assign x_arready[1]   = m_pio_arready;
    assign x_rdata[32 +: 32] = m_pio_rdata;
    assign x_rresp[2 +: 2] = m_pio_rresp;
    assign x_rvalid[1]    = m_pio_rvalid;
    assign m_pio_rready   = x_rready[1];
endmodule
`default_nettype wire
