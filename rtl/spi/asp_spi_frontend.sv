// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// AbstractX Configurable SPI / Dual-SPI 64-Byte TLP Frontend
//
// Converts physical SPI / Dual-SPI pins into 512-bit (64-Byte) parallel TLP streams.
// Supports both Single-SPI (SCLK, CS_N, MOSI, MISO) and Dual-SPI (SCLK, CS_N, IO0, IO1) modes
// via parameter DUAL_SPI_ENABLE.
//
// Drives external doorbell interrupt output (o_int_req) to notify host processor when
// an egress TLP is ready in the FPGA output FIFO.

module asp_spi_frontend #(
    parameter bit DUAL_SPI_ENABLE = 1'b1
) (
    input  wire        clk,
    input  wire        rst_n,

    // Physical SPI / Dual-SPI Pins
    input  wire        i_sclk,
    input  wire        i_cs_n,
`ifdef VERILATOR
    input  wire        io_sdio0, // MOSI in Single-SPI / IO0 in Dual-SPI
    input  wire        io_sdio1, // MISO in Single-SPI / IO1 in Dual-SPI
    output wire        io_sdio0_o,
    output wire        io_sdio1_o,
    output wire        io_sdio0_oe,
    output wire        io_sdio1_oe,
`else
    inout  wire        io_sdio0, // MOSI in Single-SPI / IO0 in Dual-SPI
    inout  wire        io_sdio1, // MISO in Single-SPI / IO1 in Dual-SPI
`endif

    // Interrupt Request Doorbell pin to Host CPU GPIO
    input  wire [7:0]  i_egress_count,
    output wire        o_int_req,

    // 512-Bit (64-Byte) TLP Ingress Stream (Host -> FPGA)
    output logic [511:0] o_tlp_rx_data,
    output logic         o_tlp_rx_valid,
    input  wire          i_tlp_rx_ready,

    // 512-Bit (64-Byte) TLP Egress Stream (FPGA -> Host)
    input  wire [511:0]  i_tlp_tx_data,
    input  wire          i_tlp_tx_valid,
    output logic         o_tlp_tx_ready,

    // Status diagnostics
    output logic [3:0]   o_status_flags
);

    // Command Codes
    localparam logic [7:0] CMD_READ_STATUS = 8'hA0;
    localparam logic [7:0] CMD_WRITE_BURST = 8'hA1;
    localparam logic [7:0] CMD_READ_BURST  = 8'hA2;

    // ------------------------------------------------------------------------
    // Pin Tristate & Synchronizer Logic
    // ------------------------------------------------------------------------
    logic [2:0] cs_sync;
    logic [2:0] sclk_sync;
    logic [1:0] io0_in_sync;
    logic [1:0] io1_in_sync;

    logic       io0_oe;
    logic       io1_oe;
    logic       io0_out;
    logic       io1_out;

    // Tristate IO buffer assignments
`ifndef VERILATOR
    assign io_sdio0 = io0_oe ? io0_out : 1'bZ;
    assign io_sdio1 = io1_oe ? io1_out : 1'bZ;
`else
    assign io_sdio0_o  = io0_out;
    assign io_sdio1_o  = io1_out;
    assign io_sdio0_oe = io0_oe;
    assign io_sdio1_oe = io1_oe;
`endif

    logic cs_active;
    logic cs_fall;
    logic cs_rise;
    logic sclk_rise;
    logic sclk_fall;

    assign cs_active = ~cs_sync[2];
    assign cs_fall   = (cs_sync[2:1] == 2'b10);
    assign cs_rise   = (cs_sync[2:1] == 2'b01);
    assign sclk_rise = (sclk_sync[2:1] == 2'b01);
    assign sclk_fall = (sclk_sync[2:1] == 2'b10);

    // Doorbell Interrupt to Host CPU GPIO: Driven HIGH when egress TLP ready
    assign o_int_req = (i_egress_count != 8'h00);

    // ------------------------------------------------------------------------
    // State Machine
    // ------------------------------------------------------------------------
    typedef enum logic [2:0] {
        ST_IDLE,
        ST_CMD_BYTE,
        ST_STATUS_RESP,
        ST_WRITE_TLP_BURST,
        ST_READ_TLP_BURST,
        ST_TLP_COMPLETE
    } state_t;

    state_t state;

    logic [7:0]   cmd_shift;
    logic [3:0]   bit_cnt;
    logic [8:0]   clk_pulse_cnt; // Counts up to 256 (Dual-SPI) or 512 (Single-SPI)
    logic [511:0] rx_shift_reg;
    logic [511:0] tx_shift_reg;
    logic [31:0]  status_shift_reg;

    logic         crc_err_flag;
    logic         overflow_flag;

    // @impl [SPEC-TLP-CRC-01] docs/tier2_contracts/TLP_BUS_SPECIFICATION.md
    // Reflected IEEE 802.3 CRC32 (zlib.crc32) over wire bytes 0..59, updated one
    // byte per clock as bytes arrive or leave. A single-cycle CRC over all 60
    // bytes unrolled 480 bit-steps into one path (Fmax 13.7 MHz on GW2A-18).
    function automatic logic [31:0] crc32_byte(input logic [31:0] crc_in, input logic [7:0] data_byte);
        logic [31:0] crc;
        integer bit_idx;
        begin
            crc = crc_in ^ {24'h000000, data_byte};
            for (bit_idx = 0; bit_idx < 8; bit_idx = bit_idx + 1) begin
                if (crc[0]) crc = (crc >> 1) ^ 32'hEDB88320;
                else        crc = crc >> 1;
            end
            crc32_byte = crc;
        end
    endfunction

    logic [31:0] rx_crc;       // running CRC of received bytes 0..59
    logic [31:0] tx_crc;       // running CRC of transmitted bytes 0..59
    logic [31:0] tx_crc_out;   // egress bytes 60..63 being shifted out
    logic [7:0]  crc_rx_byte;
    logic [7:0]  crc_tx_byte;
    logic        crc_rx_v;
    logic        crc_tx_v;
    logic        tx_empty;     // no TLP pending: send 64 zero bytes, no CRC
    wire  [31:0] tx_crc_wire = tx_empty ? 32'd0 : ~tx_crc;

    wire [511:0] rx_dual_complete =
        {rx_shift_reg[509:0], io1_in_sync[1], io0_in_sync[1]};
    wire [511:0] rx_single_complete =
        {rx_shift_reg[510:0], io0_in_sync[1]};

    assign o_status_flags = {overflow_flag, crc_err_flag, i_tlp_rx_ready, (i_egress_count != 8'h00)};

    // Sync input signals
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            cs_sync     <= 3'b111;
            sclk_sync   <= 3'b000;
            io0_in_sync <= 2'b00;
            io1_in_sync <= 2'b00;
        end else begin
            cs_sync     <= {cs_sync[1:0], i_cs_n};
            sclk_sync   <= {sclk_sync[1:0], i_sclk};
            io0_in_sync <= {io0_in_sync[0], io_sdio0};
            io1_in_sync <= {io1_in_sync[0], io_sdio1};
        end
    end

    // Main SPI State Machine
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state            <= ST_IDLE;
            bit_cnt          <= 4'd0;

            clk_pulse_cnt    <= 9'd0;
            cmd_shift        <= 8'h00;
            rx_shift_reg     <= 512'd0;
            tx_shift_reg     <= 512'd0;
            status_shift_reg <= 32'd0;
            o_tlp_rx_data    <= 512'd0;
            o_tlp_rx_valid   <= 1'b0;
            o_tlp_tx_ready   <= 1'b0;
            io0_oe           <= 1'b0;
            io1_oe           <= 1'b0;
            io0_out          <= 1'b0;
            io1_out          <= 1'b0;
            crc_err_flag     <= 1'b0;
            overflow_flag    <= 1'b0;
            rx_crc           <= 32'hFFFFFFFF;
            tx_crc           <= 32'hFFFFFFFF;
            tx_crc_out       <= 32'd0;
            crc_rx_byte      <= 8'd0;
            crc_tx_byte      <= 8'd0;
            crc_rx_v         <= 1'b0;
            crc_tx_v         <= 1'b0;
            tx_empty         <= 1'b1;
        end else begin
            // Apply the byte captured on the previous clock; the next byte is
            // at least 4 SCLK edges away, so a capture never overlaps an update.
            crc_rx_v <= 1'b0;
            crc_tx_v <= 1'b0;
            if (crc_rx_v) rx_crc <= crc32_byte(rx_crc, crc_rx_byte);
            if (crc_tx_v) tx_crc <= crc32_byte(tx_crc, crc_tx_byte);

            // Clear valid flag once downstream consumer accepts TLP
            if (o_tlp_rx_valid && i_tlp_rx_ready) begin
                o_tlp_rx_valid <= 1'b0;
            end

            // CS Deassertion Resets State Machine to IDLE
            if (cs_rise) begin
                state        <= ST_IDLE;
                io0_oe       <= 1'b0;
                io1_oe       <= 1'b0;
                bit_cnt      <= 4'd0;
                clk_pulse_cnt <= 9'd0;
            end else if (cs_active) begin

                case (state)
                    ST_IDLE: begin
                        bit_cnt       <= 4'd0;
                        clk_pulse_cnt <= 9'd0;
                        io0_oe        <= 1'b0;
                        io1_oe        <= 1'b0;
                        state         <= ST_CMD_BYTE;
                    end

                    // Phase 1: Shift in 1-byte command from host (MOSI / IO0)
                    ST_CMD_BYTE: begin
                        if (sclk_rise) begin
                            cmd_shift <= {cmd_shift[6:0], io0_in_sync[1]};
                            if (bit_cnt == 4'd7) begin
                                bit_cnt <= 4'd0;
                                case ({cmd_shift[6:0], io0_in_sync[1]})
                                    CMD_READ_STATUS: begin
                                        state <= ST_STATUS_RESP;
                                        status_shift_reg <= {8'h64, o_status_flags, 4'h0, i_egress_count, 8'h08};
                                    end
                                    CMD_WRITE_BURST: begin
                                        state  <= ST_WRITE_TLP_BURST;
                                        rx_crc <= 32'hFFFFFFFF;
                                    end
                                    CMD_READ_BURST: begin
                                        state    <= ST_READ_TLP_BURST;
                                        tx_crc   <= 32'hFFFFFFFF;
                                        tx_empty <= !i_tlp_tx_valid;
                                        if (i_tlp_tx_valid) begin
                                            // Bytes 60..63 are sent from tx_crc_out
                                            tx_shift_reg <= {i_tlp_tx_data[511:32], 32'd0};
                                        end else begin
                                            tx_shift_reg <= 512'd0; // Pad if empty
                                        end
                                    end
                                    default: state <= ST_IDLE;
                                endcase
                            end else begin
                                bit_cnt <= bit_cnt + 4'd1;
                            end
                        end
                    end

                    // Command 0xA0: Return 4-byte Status Vector
                    ST_STATUS_RESP: begin
                        if (DUAL_SPI_ENABLE) begin
                            io0_oe <= 1'b1;
                            io1_oe <= 1'b1;
                            if (sclk_fall) begin
                                io0_out <= status_shift_reg[30];
                                io1_out <= status_shift_reg[31];
                                status_shift_reg <= {status_shift_reg[29:0], 2'b00};
                            end
                        end else begin
                            io0_oe <= 1'b0;
                            io1_oe <= 1'b1; // MISO
                            if (sclk_fall) begin
                                io1_out <= status_shift_reg[31];
                                status_shift_reg <= {status_shift_reg[30:0], 1'b0};
                            end
                        end
                    end

                    // Command 0xA1: Receive 64-Byte Ingress TLP Burst (512 bits)
                    ST_WRITE_TLP_BURST: begin
                        io0_oe <= 1'b0;
                        io1_oe <= 1'b0;
                        if (sclk_rise) begin
                            if (DUAL_SPI_ENABLE) begin
                                rx_shift_reg <= {rx_shift_reg[509:0], io1_in_sync[1], io0_in_sync[1]};
                                clk_pulse_cnt <= clk_pulse_cnt + 9'd1;
                                // Byte k completes on pulse 4k+3; CRC covers bytes 0..59
                                if (clk_pulse_cnt[1:0] == 2'b11 && clk_pulse_cnt[8:2] < 7'd60) begin
                                    crc_rx_byte <= {rx_shift_reg[5:0], io1_in_sync[1], io0_in_sync[1]};
                                    crc_rx_v    <= 1'b1;
                                end
                                if (clk_pulse_cnt == 9'd255) begin
                                    state <= ST_TLP_COMPLETE;
                                    if (rx_dual_complete[31:0] == ~rx_crc) begin
                                        if (!o_tlp_rx_valid) begin
                                            o_tlp_rx_data  <= {rx_dual_complete[511:32], 32'd0};
                                            o_tlp_rx_valid <= 1'b1;
                                        end else begin
                                            overflow_flag <= 1'b1;
                                        end
                                    end else begin
                                        crc_err_flag <= 1'b1;
                                    end
                                end
                            end else begin
                                rx_shift_reg <= {rx_shift_reg[510:0], io0_in_sync[1]};
                                clk_pulse_cnt <= clk_pulse_cnt + 9'd1;
                                // Byte k completes on pulse 8k+7
                                if (clk_pulse_cnt[2:0] == 3'b111 && clk_pulse_cnt[8:3] < 6'd60) begin
                                    crc_rx_byte <= {rx_shift_reg[6:0], io0_in_sync[1]};
                                    crc_rx_v    <= 1'b1;
                                end
                                if (clk_pulse_cnt == 9'd511) begin
                                    state <= ST_TLP_COMPLETE;
                                    if (rx_single_complete[31:0] == ~rx_crc) begin
                                        if (!o_tlp_rx_valid) begin
                                            o_tlp_rx_data  <= {rx_single_complete[511:32], 32'd0};
                                            o_tlp_rx_valid <= 1'b1;
                                        end else begin
                                            overflow_flag <= 1'b1;
                                        end
                                    end else begin
                                        crc_err_flag <= 1'b1;
                                    end
                                end
                            end
                        end
                    end


                    // Command 0xA2: Transmit 64-Byte Egress TLP Burst (512 bits)
                    ST_READ_TLP_BURST: begin
                        if (DUAL_SPI_ENABLE) begin
                            io0_oe <= 1'b1;
                            io1_oe <= 1'b1;
                            if (sclk_fall) begin
                                // Byte k is at the top on pulse 4k: fold it into the CRC
                                if (clk_pulse_cnt[1:0] == 2'b00 && clk_pulse_cnt[8:2] < 7'd60) begin
                                    crc_tx_byte <= tx_shift_reg[511:504];
                                    crc_tx_v    <= 1'b1;
                                end
                                if (clk_pulse_cnt < 9'd240) begin
                                    io0_out <= tx_shift_reg[510];
                                    io1_out <= tx_shift_reg[511];
                                end else if (clk_pulse_cnt == 9'd240) begin
                                    io0_out    <= tx_crc_wire[30];
                                    io1_out    <= tx_crc_wire[31];
                                    tx_crc_out <= {tx_crc_wire[29:0], 2'b00};
                                end else begin
                                    io0_out    <= tx_crc_out[30];
                                    io1_out    <= tx_crc_out[31];
                                    tx_crc_out <= {tx_crc_out[29:0], 2'b00};
                                end
                                tx_shift_reg <= {tx_shift_reg[509:0], 2'b00};
                                clk_pulse_cnt <= clk_pulse_cnt + 9'd1;
                                if (clk_pulse_cnt == 9'd255) begin
                                    state <= ST_TLP_COMPLETE;
                                    o_tlp_tx_ready <= 1'b1; // Consume packet when burst finishes
                                end
                            end
                        end else begin
                            io0_oe <= 1'b0;
                            io1_oe <= 1'b1; // MISO
                            if (sclk_fall) begin
                                // Byte k is at the top on pulse 8k
                                if (clk_pulse_cnt[2:0] == 3'b000 && clk_pulse_cnt[8:3] < 6'd60) begin
                                    crc_tx_byte <= tx_shift_reg[511:504];
                                    crc_tx_v    <= 1'b1;
                                end
                                if (clk_pulse_cnt < 9'd480) begin
                                    io1_out <= tx_shift_reg[511];
                                end else if (clk_pulse_cnt == 9'd480) begin
                                    io1_out    <= tx_crc_wire[31];
                                    tx_crc_out <= {tx_crc_wire[30:0], 1'b0};
                                end else begin
                                    io1_out    <= tx_crc_out[31];
                                    tx_crc_out <= {tx_crc_out[30:0], 1'b0};
                                end
                                tx_shift_reg <= {tx_shift_reg[510:0], 1'b0};
                                clk_pulse_cnt <= clk_pulse_cnt + 9'd1;
                                if (clk_pulse_cnt == 9'd511) begin
                                    state <= ST_TLP_COMPLETE;
                                    o_tlp_tx_ready <= 1'b1; // Consume packet when burst finishes
                                end
                            end
                        end
                    end

                    ST_TLP_COMPLETE: begin
                        io0_oe <= 1'b0;
                        io1_oe <= 1'b0;
                        o_tlp_tx_ready <= 1'b0;
                    end


                    default: state <= ST_IDLE;
                endcase
            end
        end
    end

endmodule

`default_nettype wire
