// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// AbstractX 64-byte TLP router, all-PIO fabric ([SPEC-TLP-07]).
// Ingress: channel 0x01 (CONTROL) -> Wishbone master; every other channel -> PIO tile.
// Egress: control completions, then the PIO stream; a presented TLP is held until accepted.
//
// @impl [SPEC-TLP-01] [SPEC-TLP-03] [SPEC-TLP-07] docs/DESIGN_SPECIFICATION.md
module asp_router (
    input  wire          clk,
    input  wire          rst_n,

    // Ingress (link frontend)
    input  wire [511:0]  s_tlp_tdata,
    input  wire          s_tlp_tvalid,
    output logic         s_tlp_tready,

    // CONTROL (channel 0x01) -> Wishbone master
    output logic [511:0] m_ctrl_tdata,
    output logic         m_ctrl_tvalid,
    input  wire          m_ctrl_tready,

    // Every other channel -> PIO tile (its IN_CFG channel picks its own)
    output logic [511:0] m_pio_tdata,
    output logic         m_pio_tvalid,
    input  wire          m_pio_tready,

    // Egress (-> link frontend)
    output logic [511:0] m_egr_tdata,
    output logic         m_egr_tvalid,
    input  wire          m_egr_tready,

    input  wire  [511:0] s_wb_cpl_tdata,     // Wishbone completions
    input  wire          s_wb_cpl_tvalid,
    output logic         s_wb_cpl_tready,

    input  wire  [511:0] s_pio_stream_tdata, // PIO tile stream
    input  wire          s_pio_stream_tvalid,
    output logic         s_pio_stream_tready
);
    localparam logic [7:0] CH_CONTROL = 8'h01;

    wire [7:0] channel  = s_tlp_tdata[487:480];   // DW0 channel byte
    wire       sel_ctrl = channel == CH_CONTROL;

    assign m_ctrl_tdata  = s_tlp_tdata;
    assign m_ctrl_tvalid = s_tlp_tvalid & sel_ctrl;
    assign m_pio_tdata   = s_tlp_tdata;
    assign m_pio_tvalid  = s_tlp_tvalid & ~sel_ctrl;
    assign s_tlp_tready  = sel_ctrl ? m_ctrl_tready : m_pio_tready;

    // Egress: fixed priority, but a grant is held while its TLP is presented and not accepted.
    logic held, held_pio;              // a TLP is presented and waiting; which source
    wire  pick_pio = held ? held_pio : !s_wb_cpl_tvalid;

    always_comb begin
        m_egr_tvalid        = pick_pio ? s_pio_stream_tvalid : s_wb_cpl_tvalid;
        m_egr_tdata         = pick_pio ? s_pio_stream_tdata  : s_wb_cpl_tdata;
        s_wb_cpl_tready     = !pick_pio && m_egr_tready;
        s_pio_stream_tready =  pick_pio && m_egr_tready;
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            held     <= 1'b0;
            held_pio <= 1'b0;
        end else begin
            held     <= m_egr_tvalid && !m_egr_tready;
            held_pio <= pick_pio;
        end
    end

`ifndef SYNTHESIS
    /* verilator lint_off SYNCASYNCNET */
    // [SVA-ROUTER-01] one ingress destination at a time
    property p_ingress_one_hot;
        @(posedge clk) disable iff (!rst_n) !(m_ctrl_tvalid && m_pio_tvalid);
    endproperty
    assert property (p_ingress_one_hot) else $error("[SVA-ROUTER-01] multi-hot ingress valid");

    // [SVA-ROUTER-02] AXI-Stream: a presented egress TLP keeps valid and data until accepted
    property p_egress_stable;
        @(posedge clk) disable iff (!rst_n)
        (m_egr_tvalid && !m_egr_tready) |=> (m_egr_tvalid && $stable(m_egr_tdata));
    endproperty
    assert property (p_egress_stable) else $error("[SVA-ROUTER-02] egress tdata changed before tready");
    /* verilator lint_on SYNCASYNCNET */
`endif
endmodule

`default_nettype wire
