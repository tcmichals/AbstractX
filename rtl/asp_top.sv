// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX Top Level Integrator
//
// Wires together the SPI physical MAC, Protocol parser, 
// Wishbone Master, and System Registers to form a testable pipeline.
module asp_top (
    input  wire  clk,
    input  wire  rst_n,

    // SPI physical interface
    input  wire  spi_sclk,
    input  wire  spi_cs_n,
    input  wire  spi_mosi,
    output logic spi_miso,

    // Interrupt / Doorbell out
    output logic int_req
);

    // ----------------------------------------------------
    // SPI Frontend <-> Reg Bank Signals
    // ----------------------------------------------------
    logic [7:0] spi_rx_byte;
    logic       spi_rx_valid;
    logic       spi_rx_ready;
    
    logic [7:0] spi_tx_byte;
    logic       spi_tx_valid;
    logic       spi_tx_ready;
    
    logic       spi_busy;
    logic       spi_cs_n_q;
    logic       spi_cs_rise;

    always_ff @(posedge clk) begin
        if (!rst_n) spi_cs_n_q <= 1'b1;
        else        spi_cs_n_q <= spi_cs_n;
    end
    
    // Detect end of SPI transaction
    assign spi_cs_rise = (~spi_cs_n_q & spi_cs_n);

    asp_spi_frontend u_spi_frontend (
        .clk        (clk),
        .rst        (~rst_n),
        .i_sclk     (spi_sclk),
        .i_cs_n     (spi_cs_n),
        .i_mosi     (spi_mosi),
        .o_miso     (spi_miso),
        .o_rx_byte  (spi_rx_byte),
        .o_rx_valid (spi_rx_valid),
        .i_rx_ready (spi_rx_ready),
        .i_tx_byte  (spi_tx_byte),
        .i_tx_valid (spi_tx_valid),
        .o_tx_ready (spi_tx_ready),
        .o_busy     (spi_busy)
    );

    // ----------------------------------------------------
    // Reg Bank <-> Wishbone Master Datapath
    // ----------------------------------------------------
    logic [7:0] ing_tdata;
    logic       ing_tvalid;
    logic       ing_tready;
    
    logic [7:0] egr_tdata;
    logic       egr_tvalid;
    logic       egr_tready;
    
    asp_spi_reg_bank #(
        .P_ASP_VERSION(8'h01)
    ) u_reg_bank (
        .i_clk          (clk),
        .i_rst_n        (rst_n),
        
        .i_spi_rx_data  (spi_rx_byte),
        .i_spi_rx_valid (spi_rx_valid),
        .o_spi_rx_ready (spi_rx_ready),
        
        .o_spi_tx_data  (spi_tx_byte),
        .o_spi_tx_valid (spi_tx_valid),
        .i_spi_tx_ready (spi_tx_ready),
        
        .i_spi_cs_end   (spi_cs_rise),
        
        .o_ing_tdata    (ing_tdata),
        .o_ing_tvalid   (ing_tvalid),
        .i_ing_tready   (ing_tready),
        
        .i_egr_tdata    (egr_tdata),
        .i_egr_tvalid   (egr_tvalid),
        .o_egr_tready   (egr_tready),
        
        .i_rx_len       (fifo_data_count),
        .i_status_rx_overflow(1'b0),
        .i_status_crc_err(1'b0),
        .i_status_len_err(1'b0),
        .i_status_resync_evt(1'b0),
        
        .o_int_req      (int_req)
    );

    // ----------------------------------------------------
    // Wishbone Master <-> Sys Regs
    // ----------------------------------------------------
    logic [31:0] wb_adr;
    logic [31:0] wb_dat_w;
    logic [31:0] wb_dat_r;
    logic [3:0]  wb_sel;
    logic        wb_we;
    logic        wb_cyc;
    logic        wb_stb;
    logic        wb_ack;

    // ----------------------------------------------------
    // tlast Generation Skid Buffer
    // ----------------------------------------------------
    // To correctly drive the AXIS `tlast` signal for the Wishbone Master without
    // a complex protocol framer, we buffer the stream by 1 byte.
    // When SPI CS deasserts (transaction ends), the held byte is emitted with tlast=1.
    
    logic [7:0] cmd_tdata;
    logic       cmd_tvalid;
    logic       cmd_tlast;
    logic       cmd_tready;
    
    logic [7:0] skid_data;
    logic       skid_full;
    logic       skid_is_last;

    always_ff @(posedge clk) begin
        if (!rst_n) begin
            skid_full <= 1'b0;
            skid_data <= 8'h00;
        end else begin
            // If CS rises, the current data in skid is the last byte of the transaction
            if (spi_cs_rise && skid_full) begin
                // We don't clear full here, we let the wishbone master consume it,
                // but we flag it as the last byte for whenever it gets consumed.
            end

            if (ing_tvalid && ing_tready) begin
                skid_data <= ing_tdata;
                skid_full <= 1'b1;
            end else if (cmd_tvalid && cmd_tready) begin
                skid_full <= 1'b0;
            end
            
            // If transaction boundary was crossed and skid was drained, clear out
            if (spi_cs_rise && !skid_full) begin
                skid_full <= 1'b0;
            end
        end
    end

    // The skid buffer drives the wishbone master
    assign cmd_tdata  = skid_data;
    
    // We emit valid to WB master if the skid is full AND 
    // either a new byte is pushing us out, OR CS has risen (forcing flush)
    assign cmd_tvalid = skid_full && (ing_tvalid || spi_cs_rise || spi_cs_n_q);
    
    // tlast is true if CS has deasserted causing a flush
    assign cmd_tlast  = (spi_cs_rise || spi_cs_n_q);
    
    // Accept new bytes if skid isn't full, or if WB master is currently consuming
    assign ing_tready = ~skid_full || cmd_tready;

    // ----------------------------------------------------
    // Egress Payload FIFO
    // ----------------------------------------------------
    logic [7:0] wb_rsp_tdata;
    logic       wb_rsp_tvalid;
    logic       wb_rsp_tlast;
    logic       wb_rsp_tready;
    
    logic [15:0] fifo_data_count;
    
    asp_axis_fifo #(
        .DEPTH_LOG2(8)
    ) u_egr_fifo (
        .clk           (clk),
        .rst_n         (rst_n),
        .s_axis_tdata  (wb_rsp_tdata),
        .s_axis_tvalid (wb_rsp_tvalid),
        .s_axis_tlast  (wb_rsp_tlast),
        .s_axis_tready (wb_rsp_tready),
        .m_axis_tdata  (egr_tdata),
        .m_axis_tvalid (egr_tvalid),
        .m_axis_tlast  (), // SPI native ignores tlast
        .m_axis_tready (egr_tready),
        .data_count    (fifo_data_count)
    );

    asp_wishbone_master u_wb_master (
        .clk          (clk),
        .rst          (~rst_n),
        
        .s_cmd_tvalid (cmd_tvalid),
        .s_cmd_tdata  (cmd_tdata),
        .s_cmd_tlast  (cmd_tlast),
        .s_cmd_tready (cmd_tready),
        .s_tid        (1'b0),
        
        .m_rsp_tvalid (wb_rsp_tvalid),
        .m_rsp_tdata  (wb_rsp_tdata),
        .m_rsp_tlast  (wb_rsp_tlast), 
        .m_rsp_tready (wb_rsp_tready),
        .m_rsp_tdest  (),
        
        .m_dbg_tvalid (),
        .m_dbg_tdata  (),
        .m_dbg_tlast  (),
        .m_dbg_tready (1'b1),
        
        .wb_adr_o     (wb_adr),
        .wb_dat_o     (wb_dat_w),
        .wb_sel_o     (wb_sel),
        .wb_we_o      (wb_we),
        .wb_cyc_o     (wb_cyc),
        .wb_stb_o     (wb_stb),
        .wb_ack_i     (wb_ack),
        .wb_dat_i     (wb_dat_r)
    );

    asp_sys_regs #(
        .SYS_VERSION  (32'hA1B2C3D4)
    ) u_sys_regs (
        .clk          (clk),
        .rst          (~rst_n),
        .wb_adr_i     (wb_adr),
        .wb_dat_i     (wb_dat_w),
        .wb_sel_i     (wb_sel),
        .wb_we_i      (wb_we),
        .wb_cyc_i     (wb_cyc),
        .wb_stb_i     (wb_stb),
        .wb_ack_o     (wb_ack),
        .wb_dat_o     (wb_dat_r)
    );

endmodule

`default_nettype wire
