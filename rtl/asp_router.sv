// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX 64-Byte TLP Parallel Vector Channel Router
//
// Accepts 512-bit (64-Byte) TLP containers from transport frontend and routes them
// to destination endpoints based on Channel/AXID field (tdata[23:16]).
module asp_router (
    input  wire         clk,
    input  wire         rst_n,

    // Slave Ingress TLP Stream (from SPI frontend)
    input  wire [511:0] s_tlp_tdata,
    input  wire         s_tlp_tvalid,
    output logic        s_tlp_tready,

    // Master CONTROL Stream (Wishbone Gateway - Channel 0x01)
    output logic [511:0] m_ctrl_tdata,
    output logic         m_ctrl_tvalid,
    input  wire          m_ctrl_tready,

    // Master TELEMETRY Stream (IMU Engine - Channel 0x02)
    output logic [511:0] m_tel_tdata,
    output logic         m_tel_tvalid,
    input  wire          m_tel_tready,

    // Master ESC_SERIAL Stream (UART ESC Engine - Channel 0x05)
    output logic [511:0] m_esc_tdata,
    output logic         m_esc_tvalid,
    input  wire          m_esc_tready,

    // Egress Transport Stream (Responses & Egress Telemetry -> SPI Frontend)
    output logic [511:0] m_egr_tdata,
    output logic         m_egr_tvalid,
    input  wire          m_egr_tready,

    // Egress Inputs from Peripherals
    input  wire [511:0]  s_wb_cpl_tdata,   // Wishbone CplD TLP
    input  wire          s_wb_cpl_tvalid,
    output logic         s_wb_cpl_tready,

    input  wire [511:0]  s_imu_stream_tdata, // IMU Auto-DMA Stream TLP
    input  wire          s_imu_stream_tvalid,
    output logic         s_imu_stream_tready,

    input  wire [511:0]  s_esc_stream_tdata, // ESC Serial Stream TLP
    input  wire          s_esc_stream_tvalid,
    output logic         s_esc_stream_tready
);

    localparam logic [7:0] CH_CONTROL   = 8'h01;
    localparam logic [7:0] CH_TELEMETRY = 8'h02;
    localparam logic [7:0] CH_ESC_SERIAL= 8'h05;

    // Extract Channel/AXID from DW0 (bits 487:480 in 512-bit big-endian TLP vector)
    logic [7:0] channel;
    assign channel = s_tlp_tdata[487:480];


    logic sel_ctrl, sel_tel, sel_esc, sel_known;

    // Ingress Demux Routing
    always_comb begin
        sel_ctrl  = (channel == CH_CONTROL);
        sel_tel   = (channel == CH_TELEMETRY);
        sel_esc   = (channel == CH_ESC_SERIAL);
        sel_known = sel_ctrl | sel_tel | sel_esc;

        m_ctrl_tdata  = s_tlp_tdata;
        m_ctrl_tvalid = s_tlp_tvalid & sel_ctrl;

        m_tel_tdata   = s_tlp_tdata;
        m_tel_tvalid  = s_tlp_tvalid & sel_tel;

        m_esc_tdata   = s_tlp_tdata;
        m_esc_tvalid  = s_tlp_tvalid & sel_esc;

        unique case (1'b1)
            sel_ctrl: s_tlp_tready = m_ctrl_tready;
            sel_tel:  s_tlp_tready = m_tel_tready;
            sel_esc:  s_tlp_tready = m_esc_tready;
            default:  s_tlp_tready = 1'b1; // drop unknown channel
        endcase
    end

    // Egress Mux Arbitration (Fixed Priority: Control CplD > IMU Telemetry > Serial ESC)
    always_comb begin
        s_wb_cpl_tready     = 1'b0;
        s_imu_stream_tready = 1'b0;
        s_esc_stream_tready = 1'b0;

        if (s_wb_cpl_tvalid) begin
            m_egr_tdata     = s_wb_cpl_tdata;
            m_egr_tvalid    = 1'b1;
            s_wb_cpl_tready = m_egr_tready;
        end else if (s_imu_stream_tvalid) begin
            m_egr_tdata         = s_imu_stream_tdata;
            m_egr_tvalid        = 1'b1;
            s_imu_stream_tready = m_egr_tready;
        end else if (s_esc_stream_tvalid) begin
            m_egr_tdata         = s_esc_stream_tdata;
            m_egr_tvalid        = 1'b1;
            s_esc_stream_tready = m_egr_tready;
        end else begin
            m_egr_tdata  = 512'd0;
            m_egr_tvalid = 1'b0;
        end
    end

endmodule

`default_nettype wire
