// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX to Wishbone Master Bridge
//
// Hardware-native processor replacement. 
// Consumes CONTROL frames (READ_BLOCK, WRITE_BLOCK, PING, HELLO)
// from the Router stream and executes direct Wishbone bus cycles
// to memory-mapped IO engines.
module asp_wishbone_master (
    input  wire        clk,
    input  wire        rst,

    // FCSP CONTROL Ingress
    input  wire        s_cmd_tvalid,
    input  wire [7:0]  s_cmd_tdata,
    input  wire        s_cmd_tlast,
    output logic        s_cmd_tready,
    input  wire        s_tid,

    // FCSP CONTROL Egress (Responses)
    output logic        m_rsp_tvalid,
    output logic [7:0]  m_rsp_tdata,
    output logic        m_rsp_tlast,
    input  wire        m_rsp_tready,
    output logic        m_rsp_tdest,

    // FCSP DEBUG Egress
    output logic        m_dbg_tvalid,
    output logic [7:0]  m_dbg_tdata,
    output logic        m_dbg_tlast,
    input  wire        m_dbg_tready,

    // Wishbone Master Interface to IO Engines
    output logic [31:0] wb_adr_o,
    output logic [31:0] wb_dat_o,
    output logic [3:0]  wb_sel_o,
    output logic        wb_we_o,
    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    input  wire        wb_ack_i,
    input  wire [31:0] wb_dat_i
);
    localparam logic [7:0] OP_PING        = 8'h06;
    localparam logic [7:0] OP_READ_BLOCK  = 8'h10;
    localparam logic [7:0] OP_WRITE_BLOCK = 8'h11;
    localparam logic [7:0] OP_GET_CAPS    = 8'h12;
    localparam logic [7:0] OP_HELLO       = 8'h13;

    localparam logic [7:0] RES_OK             = 8'h00;
    localparam logic [7:0] RES_INVALID_ARG    = 8'h01;
    localparam logic [7:0] RES_NOT_SUPPORTED  = 8'h04;

    typedef enum logic [3:0] {
        ST_IDLE          = 4'd0,
        ST_GET_HEADER    = 4'd1,
        ST_GET_ADDR      = 4'd2,
        ST_GET_LEN       = 4'd3,
        ST_WB_WRITE_DATA = 4'd4,
        ST_WB_WRITE_EXEC = 4'd5,
        ST_WB_READ_EXEC  = 4'd6,
        ST_RSP_SEND      = 4'd7,
        ST_DISCARD_CMD   = 4'd8
    } st_t;

    st_t st;

    logic [7:0]  op;
    logic [7:0]  space;
    logic [31:0] addr;
    logic [15:0] len;
    
    // Internal response buffer for simple stateless replies.
    logic [7:0]  rsp_buf [0:31];
    logic [15:0] rsp_len;
    logic [15:0] rsp_idx;

    logic [1:0] byte_idx; // Used for multi-byte field extraction
    logic [7:0] wb_byte_offset;
        // Saved copy of s_cmd_tlast from the last consumed payload byte.
        // s_cmd_tlast may be de-asserted by the source before the WB operation
        // completes, so we latch it here for use in ST_WB_READ_EXEC /
        // ST_WB_WRITE_EXEC.
        logic cmd_was_last;
    logic tid_latched;

    always_comb begin
        s_cmd_tready = 1'b0;
        m_rsp_tvalid = 1'b0;
        m_rsp_tlast  = 1'b0;
        m_rsp_tdata  = 8'h00;
        m_rsp_tdest  = 1'b0;

        wb_cyc_o = 1'b0;
        wb_stb_o = 1'b0;
        wb_we_o  = 1'b0;
        
        m_dbg_tvalid = 1'b0;
        m_dbg_tdata  = 8'h00;
        m_dbg_tlast  = 1'b0;

        unique case (st)
            ST_IDLE, ST_GET_HEADER, ST_GET_ADDR, ST_GET_LEN, ST_DISCARD_CMD: begin
                s_cmd_tready = 1'b1;
            end
            ST_WB_WRITE_DATA: begin
                // Ready for write payload byte
                s_cmd_tready = 1'b0; 
                if (byte_idx == 2'd0) s_cmd_tready = 1'b1;
            end
            ST_WB_WRITE_EXEC: begin
                wb_cyc_o = 1'b1;
                wb_stb_o = 1'b1;
                wb_we_o  = 1'b1;
            end
            ST_WB_READ_EXEC: begin
                wb_cyc_o = 1'b1;
                wb_stb_o = 1'b1;
                wb_we_o  = 1'b0;
            end
            ST_RSP_SEND: begin
                m_rsp_tdest  = tid_latched;
                m_rsp_tvalid = (rsp_idx < rsp_len);
                m_rsp_tlast  = (rsp_idx + 16'd1 == rsp_len);
                m_rsp_tdata  = (rsp_idx < 32) ? rsp_buf[rsp_idx[4:0]] : 8'h00;
            end
            default: ;
        endcase
    end

    always_ff @(posedge clk) begin
        if (rst) begin
            st <= ST_IDLE;
            op <= 8'h00;
            space <= 8'h00;
            addr <= 32'h00000000;
            len <= 16'd0;
            byte_idx <= 2'd0;
            rsp_len <= 16'd0;
            rsp_idx <= 16'd0;
            wb_adr_o <= 32'h0;
            wb_dat_o <= 32'h0;
            wb_sel_o <= 4'h0;
            wb_byte_offset <= 8'h0;
                cmd_was_last <= 1'b0;
            tid_latched <= 1'b0;
        end else begin
            unique case (st)
                ST_IDLE: begin
                    if (s_cmd_tvalid) begin
                        op <= s_cmd_tdata;
                        tid_latched <= s_tid;
                        addr <= 32'h0;
                        len <= 16'd0;
                        if (s_cmd_tlast) begin
                            // Handle zero-length bodies like GET_CAPS/HELLO
                            rsp_idx <= 16'd0;
                            if (s_cmd_tdata == OP_GET_CAPS) begin
                                rsp_buf[0] <= RES_OK;
                                rsp_len <= 16'd1;
                                st <= ST_RSP_SEND;
                            end else if (s_cmd_tdata == OP_HELLO) begin
                                rsp_buf[0] <= RES_OK;
                                rsp_len <= 16'd1;
                                st <= ST_RSP_SEND;
                            end else if (s_cmd_tdata == OP_PING) begin
                                rsp_buf[0] <= RES_OK;
                                rsp_len <= 16'd1;
                                st <= ST_RSP_SEND;
                            end else begin
                                rsp_buf[0] <= RES_NOT_SUPPORTED;
                                rsp_len <= 16'd1;
                                st <= ST_RSP_SEND;
                            end
                        end else begin
                            byte_idx <= 2'd0;
                            st <= ST_GET_HEADER;
                        end
                    end
                end

                ST_GET_HEADER: begin
                    if (s_cmd_tvalid) begin
                        if (op == OP_WRITE_BLOCK || op == OP_READ_BLOCK) begin
                            space <= s_cmd_tdata;
                            byte_idx <= 2'd0;
                            st <= s_cmd_tlast ? ST_DISCARD_CMD : ST_GET_ADDR;
                        end else begin
                            // Flush unsupported or simple op payloads (PING, HELLO, etc.)
                            rsp_buf[0] <= (op == OP_PING || op == OP_HELLO || op == OP_GET_CAPS) ? RES_OK : RES_NOT_SUPPORTED;
                            rsp_len <= 16'd1;
                            rsp_idx <= 16'd0;
                            st <= s_cmd_tlast ? ST_RSP_SEND : ST_DISCARD_CMD;
                        end
                    end
                end

                ST_GET_ADDR: begin
                    if (s_cmd_tvalid) begin
                        addr <= {addr[23:0], s_cmd_tdata};
                        byte_idx <= byte_idx + 2'd1;
                        if (byte_idx == 2'd3) begin
                            byte_idx <= 2'd0;
                            st <= s_cmd_tlast ? ST_DISCARD_CMD : ST_GET_LEN;
                        end else if (s_cmd_tlast) begin
                            st <= ST_DISCARD_CMD;
                        end
                    end
                end

                ST_GET_LEN: begin
                    if (s_cmd_tvalid) begin
                        len <= {len[7:0], s_cmd_tdata};
                        byte_idx <= byte_idx + 2'd1;
                        if (byte_idx == 2'd1) begin
                            // Finished fetching standard packet header.
                            wb_adr_o <= addr;
                            wb_byte_offset <= 8'd0;
                            
                            if (op == OP_WRITE_BLOCK) begin
                                byte_idx <= 2'd0;
                                wb_dat_o <= 32'h0;
                                wb_sel_o <= 4'h0;
                                st <= ST_WB_WRITE_DATA;
                            end else if (op == OP_READ_BLOCK) begin
                                cmd_was_last <= s_cmd_tlast;
                                st <= ST_WB_READ_EXEC;
                            end
                        end else if (s_cmd_tlast) begin
                            // Truncated header
                            st <= ST_IDLE;
                        end
                    end
                end

                ST_WB_WRITE_DATA: begin
                    if (s_cmd_tvalid) begin
                        // Map byte stream into 32-bit Wishbone word
                        logic [1:0] shift;
                        shift = wb_byte_offset[1:0];
                        
                        unique case(shift)
                            2'd0: begin wb_dat_o[31:24] <= s_cmd_tdata; wb_sel_o[3] <= 1'b1; end
                            2'd1: begin wb_dat_o[23:16] <= s_cmd_tdata; wb_sel_o[2] <= 1'b1; end
                            2'd2: begin wb_dat_o[15:8]  <= s_cmd_tdata; wb_sel_o[1] <= 1'b1; end
                            2'd3: begin wb_dat_o[7:0]   <= s_cmd_tdata; wb_sel_o[0] <= 1'b1; end
                        endcase
                        
                        wb_byte_offset <= wb_byte_offset + 8'd1;
                        // Execute cycle if we filled 4 bytes, or reached the end of payload length
                        if (shift == 2'd3 || wb_byte_offset + 8'd1 == len[7:0] || s_cmd_tlast) begin
                                cmd_was_last <= s_cmd_tlast;
                            st <= ST_WB_WRITE_EXEC;
                        end
                    end
                end

                ST_WB_WRITE_EXEC: begin
                    if (wb_ack_i) begin
                            if (wb_byte_offset == len[7:0] || cmd_was_last) begin
                            rsp_buf[0] <= RES_OK;
                            rsp_buf[1] <= len[15:8];
                            rsp_buf[2] <= len[7:0];
                            rsp_len <= 16'd3;
                            rsp_idx <= 16'd0;
                               st <= cmd_was_last ? ST_RSP_SEND : ST_DISCARD_CMD;
                        end else begin
                            wb_adr_o <= wb_adr_o + 32'd4;
                            wb_dat_o <= 32'h0;
                            wb_sel_o <= 4'h0;
                            byte_idx <= 2'd0;
                            st <= ST_WB_WRITE_DATA;
                        end
                    end
                end

                ST_WB_READ_EXEC: begin
                    if (wb_ack_i) begin
                        rsp_buf[0] <= RES_OK;
                        rsp_buf[1] <= 8'h00;
                        rsp_buf[2] <= 8'h04;
                        rsp_buf[3] <= wb_dat_i[31:24];
                        rsp_buf[4] <= wb_dat_i[23:16];
                        rsp_buf[5] <= wb_dat_i[15:8];
                        rsp_buf[6] <= wb_dat_i[7:0];
                        rsp_len <= 16'd7;
                        rsp_idx <= 16'd0;
                            st <= cmd_was_last ? ST_RSP_SEND : ST_DISCARD_CMD;
                    end
                end

                ST_DISCARD_CMD: begin
                    if (s_cmd_tvalid && s_cmd_tlast) begin
                        st <= ST_RSP_SEND; // Done discarding, send response
                    end else if (!s_cmd_tvalid && cmd_was_last) begin
                        st <= ST_RSP_SEND; // Frame already ended implicitly
                    end
                end

                ST_RSP_SEND: begin
                    if (rsp_len == 16'd0) begin
                        st <= ST_IDLE;
                    end else if (m_rsp_tvalid && m_rsp_tready) begin
                        if (m_rsp_tlast) begin
                            st <= ST_IDLE;
                        end else begin
                            rsp_idx <= rsp_idx + 16'd1;
                        end
                    end
                end

                default: st <= ST_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
