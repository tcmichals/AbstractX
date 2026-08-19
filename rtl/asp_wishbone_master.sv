// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX Wishbone Master Gateway (64-Byte TLP Profile)
//
// Converts 512-bit (64-Byte) TLP commands (MemRd=0x01, MemWr=0x02) into Wishbone cycles.
// Generates CplD (Type=0x03) 64-byte completion TLPs for MemRd requests.
module asp_wishbone_master (
    input  wire         clk,
    input  wire         rst_n,

    // Ingress 64-Byte TLP Command Stream (from Router)
    input  wire [511:0] s_tlp_tdata,
    input  wire         s_tlp_tvalid,
    output logic        s_tlp_tready,

    // Egress 64-Byte CplD Response TLP Stream (to Router)
    output logic [511:0] m_cpl_tdata,
    output logic         m_cpl_tvalid,
    input  wire          m_cpl_tready,

    // Wishbone Master Interface
    output logic [31:0] wb_adr_o,
    output logic [31:0] wb_dat_o,
    output logic [3:0]  wb_sel_o,
    output logic        wb_we_o,
    output logic        wb_cyc_o,
    output logic        wb_stb_o,
    input  wire         wb_ack_i,
    input  wire [31:0]  wb_dat_i
);

    localparam logic [7:0] TYPE_MEM_RD = 8'h01;
    localparam logic [7:0] TYPE_MEM_WR = 8'h02;
    localparam logic [7:0] TYPE_CPL_D  = 8'h03;

    typedef enum logic [2:0] {
        ST_IDLE,
        ST_DECODE,
        ST_WB_WRITE,
        ST_WB_READ,
        ST_BUILD_CPLD,
        ST_EMIT_CPLD
    } state_t;

    state_t state;

    // Latched TLP Fields
    logic [7:0]  tlp_type;
    logic [7:0]  tlp_tag;
    logic [7:0]  tlp_channel;
    logic [31:0] tlp_addr;
    logic [15:0] tlp_len;
    logic [15:0] tlp_seq;
    logic [63:0] tlp_ts;
    logic [31:0] write_data_latch;
    logic [31:0] read_data_latch;

    assign wb_sel_o = 4'hF; // 32-bit word select

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state            <= ST_IDLE;
            s_tlp_tready     <= 1'b1;
            m_cpl_tvalid     <= 1'b0;
            m_cpl_tdata      <= 512'd0;
            wb_cyc_o         <= 1'b0;
            wb_stb_o         <= 1'b0;
            wb_we_o          <= 1'b0;
            wb_adr_o         <= 32'd0;
            wb_dat_o         <= 32'd0;
            write_data_latch <= 32'd0;
            read_data_latch  <= 32'd0;
            tlp_type         <= 8'd0;
            tlp_tag          <= 8'd0;
            tlp_channel      <= 8'd0;
            tlp_addr         <= 32'd0;
            tlp_len          <= 16'd0;
            tlp_seq          <= 16'd0;
            tlp_ts           <= 64'd0;
        end else begin

            // Handshake clear
            if (m_cpl_tvalid && m_cpl_tready) begin
                m_cpl_tvalid <= 1'b0;
            end

            case (state)
                ST_IDLE: begin
                    wb_cyc_o     <= 1'b0;
                    wb_stb_o     <= 1'b0;
                    s_tlp_tready <= 1'b1;

                    if (s_tlp_tvalid && s_tlp_tready) begin
                        s_tlp_tready     <= 1'b0;
                        tlp_type         <= s_tlp_tdata[511:504]; // DW0: Type
                        tlp_tag          <= s_tlp_tdata[495:488]; // DW0: Tag
                        tlp_channel      <= s_tlp_tdata[487:480]; // DW0: Channel
                        tlp_addr         <= s_tlp_tdata[479:448]; // DW1: Target Address
                        tlp_len          <= s_tlp_tdata[447:432]; // DW2: Length DW
                        tlp_seq          <= s_tlp_tdata[431:416]; // DW2: Sequence
                        tlp_ts           <= s_tlp_tdata[415:352]; // DW3-4: Timestamp
                        write_data_latch <= s_tlp_tdata[351:320]; // DW5: Write data payload
                        state            <= ST_DECODE;
                    end
                end

                ST_DECODE: begin
                    if (tlp_type == TYPE_MEM_WR) begin
                        wb_adr_o <= tlp_addr;
                        wb_dat_o <= write_data_latch; // Latch preserved from ST_IDLE
                        wb_we_o  <= 1'b1;
                        wb_cyc_o <= 1'b1;
                        wb_stb_o <= 1'b1;
                        state    <= ST_WB_WRITE;
                    end else if (tlp_type == TYPE_MEM_RD) begin

                        wb_adr_o <= tlp_addr;
                        wb_we_o  <= 1'b0;
                        wb_cyc_o <= 1'b1;
                        wb_stb_o <= 1'b1;
                        state    <= ST_WB_READ;
                    end else begin
                        state <= ST_IDLE; // Unknown type drop
                    end
                end

                ST_WB_WRITE: begin
                    if (wb_ack_i) begin
                        wb_cyc_o <= 1'b0;
                        wb_stb_o <= 1'b0;
                        state    <= ST_IDLE;
                    end
                end

                ST_WB_READ: begin
                    if (wb_ack_i) begin
                        wb_cyc_o        <= 1'b0;
                        wb_stb_o        <= 1'b0;
                        read_data_latch <= wb_dat_i;
                        state           <= ST_BUILD_CPLD;
                    end
                end

                ST_BUILD_CPLD: begin
                    m_cpl_tdata <= {
                        // DW0: Type=0x03 (CplD), Flags=0x00, Tag=tlp_tag, Channel=tlp_channel
                        TYPE_CPL_D, 8'h00, tlp_tag, tlp_channel,
                        // DW1: Target Address = tlp_addr
                        tlp_addr,
                        // DW2: Length = 1 DW, Seq = tlp_seq
                        16'd1, tlp_seq,
                        // DW3-DW4: Timestamp
                        tlp_ts,
                        // DW5: Read Data payload
                        read_data_latch,
                        // DW6-DW14: Zero-padding (36 Bytes)
                        288'd0,
                        // DW15: CRC32 Placeholder
                        32'hDEADBEEF
                    };
                    state <= ST_EMIT_CPLD;
                end

                ST_EMIT_CPLD: begin
                    m_cpl_tvalid <= 1'b1;
                    if (m_cpl_tready) begin
                        state <= ST_IDLE;
                    end
                end

                default: state <= ST_IDLE;
            endcase
        end
    end

endmodule

`default_nettype wire
