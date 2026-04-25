// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX channel router (skeleton)
//
// Accepts one packetized AXIS-like payload stream plus frame metadata and
// routes it to a per-channel output stream.
module asp_router (
    input  wire        clk,
    input  wire        rst,

    // Slave frame/payload stream
    input  wire        s_frame_tvalid,
    input  wire [7:0]  s_frame_tdata,
    input  wire        s_frame_tlast,
    output logic        s_frame_tready,
    input  wire [7:0]  s_frame_channel,
    input  wire [7:0]  s_frame_flags,
    input  wire [15:0] s_frame_seq,
    input  wire [15:0] s_frame_payload_len,

    // Master CONTROL stream
    output logic        m_ctrl_tvalid,
    output logic [7:0]  m_ctrl_tdata,
    output logic        m_ctrl_tlast,
    input  wire        m_ctrl_tready,

    // Master TELEMETRY stream
    output logic        m_tel_tvalid,
    output logic [7:0]  m_tel_tdata,
    output logic        m_tel_tlast,
    input  wire        m_tel_tready,

    // Master FC_LOG stream
    output logic        m_log_tvalid,
    output logic [7:0]  m_log_tdata,
    output logic        m_log_tlast,
    input  wire        m_log_tready,

    // Master DEBUG_TRACE stream
    output logic        m_dbg_tvalid,
    output logic [7:0]  m_dbg_tdata,
    output logic        m_dbg_tlast,
    input  wire        m_dbg_tready,

    // Master ESC_SERIAL stream
    output logic        m_esc_tvalid,
    output logic [7:0]  m_esc_tdata,
    output logic        m_esc_tlast,
    input  wire        m_esc_tready,

    // Simple observability/status
    output logic        o_route_valid,
    output logic        o_route_drop,
    output logic [7:0]  o_route_channel
);
    localparam logic [7:0] CH_CONTROL     = 8'h01;
    localparam logic [7:0] CH_TELEMETRY   = 8'h02;
    localparam logic [7:0] CH_FC_LOG      = 8'h03;
    localparam logic [7:0] CH_DEBUG_TRACE = 8'h04;
    localparam logic [7:0] CH_ESC_SERIAL  = 8'h05;

    logic sel_ctrl, sel_tel, sel_log, sel_dbg, sel_esc, sel_known;

    always_comb begin
        sel_ctrl  = (s_frame_channel == CH_CONTROL);
        sel_tel   = (s_frame_channel == CH_TELEMETRY);
        sel_log   = (s_frame_channel == CH_FC_LOG);
        sel_dbg   = (s_frame_channel == CH_DEBUG_TRACE);
        sel_esc   = (s_frame_channel == CH_ESC_SERIAL);
        sel_known = sel_ctrl | sel_tel | sel_log | sel_dbg | sel_esc;

        m_ctrl_tvalid = s_frame_tvalid & sel_ctrl;
        m_ctrl_tdata  = s_frame_tdata;
        m_ctrl_tlast  = s_frame_tlast;

        m_tel_tvalid = s_frame_tvalid & sel_tel;
        m_tel_tdata  = s_frame_tdata;
        m_tel_tlast  = s_frame_tlast;

        m_log_tvalid = s_frame_tvalid & sel_log;
        m_log_tdata  = s_frame_tdata;
        m_log_tlast  = s_frame_tlast;

        m_dbg_tvalid = s_frame_tvalid & sel_dbg;
        m_dbg_tdata  = s_frame_tdata;
        m_dbg_tlast  = s_frame_tlast;

        m_esc_tvalid = s_frame_tvalid & sel_esc;
        m_esc_tdata  = s_frame_tdata;
        m_esc_tlast  = s_frame_tlast;

        unique case (1'b1)
            sel_ctrl: s_frame_tready = m_ctrl_tready;
            sel_tel:  s_frame_tready = m_tel_tready;
            sel_log:  s_frame_tready = m_log_tready;
            sel_dbg:  s_frame_tready = m_dbg_tready;
            sel_esc:  s_frame_tready = m_esc_tready;
            default:  s_frame_tready = 1'b1; // drop unknown channel deterministically
        endcase
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            o_route_valid   <= 1'b0;
            o_route_drop    <= 1'b0;
            o_route_channel <= 8'h00;
        end else begin
            o_route_valid   <= 1'b0;
            o_route_drop    <= 1'b0;

            if (s_frame_tvalid && s_frame_tready) begin
                o_route_valid   <= sel_known;
                o_route_drop    <= ~sel_known;
                o_route_channel <= s_frame_channel;
            end
        end
    end

    // Consume metadata in skeleton revision so interface is stable even before
    // per-output metadata FIFOs are introduced.
    logic _unused_meta;
    always_comb begin
        _unused_meta = ^s_frame_flags ^ ^s_frame_seq ^ ^s_frame_payload_len;
    end
endmodule

`default_nettype wire
