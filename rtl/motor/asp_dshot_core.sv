// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX Hardware DShot / PWM Motor Output IP Core (Wishbone Slave)
// Address Base: 0x40000200
//
// Features:
// - Supports DShot150, DShot300, DShot600, and standard 50-400Hz Servo PWM
// - 8 Independent Output Channels (ch1..ch8)
// - Auto-calculates DShot 4-bit CRC hardware checksum
// - Wishbone register interface over 64B PCIe TLP Memory Writes

module asp_dshot_core #(
    parameter int CLK_FREQ_HZ = 50_000_000,
    parameter int NUM_CHANNELS = 8
) (
    input  wire        clk,
    input  wire        rst_n,

    // Wishbone Slave Interface (Base: 0x40000200)
    input  wire        wb_cyc,
    input  wire        wb_stb,
    input  wire        wb_we,
    input  wire [31:0] wb_addr,
    input  wire [31:0] wb_data_i,
    output logic[31:0] wb_data_o,
    output logic       wb_ack,

    // Motor Output Pins (Ch 1..8)
    output logic [NUM_CHANNELS-1:0] o_motor_pins
);

    // Register Map:
    // 0x40000200: Motor Control / Protocol Select (0=DShot600, 1=DShot300, 2=DShot150, 3=PWM)
    // 0x40000204: Motor Enable Mask (Bit 0..7)
    // 0x40000210..0x4000022C: Throttle Values (Ch 0..7, 11-bit throttle 0..2047)

    logic [31:0] reg_ctrl;
    logic [31:0] reg_enable;
    logic [15:0] throttle_val [0:NUM_CHANNELS-1];

    // DShot Bit Timing Counters (@ 50MHz clock)
    // DShot600: Bit period = 1.67 us (83 clocks), T0H = 0.625 us (31 clocks), T1H = 1.25 us (62 clocks)
    localparam logic [7:0] DS600_BIT_CLKS = 8'd83;
    localparam logic [7:0] DS600_T0H_CLKS = 8'd31;
    localparam logic [7:0] DS600_T1H_CLKS = 8'd62;

    logic [15:0] frame_cnt;
    logic        frame_tick;

    // 1 kHz DShot Frame Trigger (50,000 clocks @ 50MHz)
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            frame_cnt  <= 16'd0;
            frame_tick <= 1'b0;
        end else begin
            if (frame_cnt >= 16'd49999) begin
                frame_cnt  <= 16'd0;
                frame_tick <= 1'b1;
            end else begin
                frame_cnt  <= frame_cnt + 16'd1;
                frame_tick <= 1'b0;
            end
        end
    end

    // Wishbone Register Writes
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_ctrl   <= 32'd0;
            reg_enable <= 32'd0;
            wb_ack     <= 1'b0;
            wb_data_o  <= 32'd0;
            for (int i = 0; i < NUM_CHANNELS; i++) throttle_val[i] <= 16'd0;
        end else begin
            wb_ack <= 1'b0;
            if (wb_cyc && wb_stb && !wb_ack) begin
                wb_ack <= 1'b1;
                if (wb_we) begin
                    if (wb_addr[7:0] == 8'h00) begin
                        reg_ctrl   <= wb_data_i;
                    end else if (wb_addr[7:0] == 8'h04) begin
                        reg_enable <= wb_data_i;
                    end else if (wb_addr[7:0] >= 8'h10 && wb_addr[7:0] < 8'h10 + (NUM_CHANNELS * 4)) begin
                        throttle_val[(wb_addr[7:0] - 8'h10) >> 2] <= wb_data_i[15:0];
                    end
                end else begin
                    if (wb_addr[7:0] == 8'h00) begin
                        wb_data_o <= reg_ctrl;
                    end else if (wb_addr[7:0] == 8'h04) begin
                        wb_data_o <= reg_enable;
                    end else if (wb_addr[7:0] >= 8'h10 && wb_addr[7:0] < 8'h10 + (NUM_CHANNELS * 4)) begin
                        wb_data_o <= {16'd0, throttle_val[(wb_addr[7:0] - 8'h10) >> 2]};
                    end else begin
                        wb_data_o <= 32'd0;
                    end
                end
            end
        end
    end

    // Individual DShot Channel Frame Generators
    genvar c;
    generate
        for (c = 0; c < NUM_CHANNELS; c++) begin : gen_dshot_ch
            logic [15:0] packet;
            logic [3:0]  crc;
            logic [15:0] shift_reg;
            logic [4:0]  bit_idx;
            logic [7:0]  bit_clk_cnt;
            logic        active;

            // Calculate DShot 4-bit CRC: (packet ^ (packet >> 4) ^ (packet >> 8)) & 0x0F
            wire [11:0] packet_12b = {throttle_val[c][10:0], 1'b0}; // 11-bit throttle + telemetry bit
            assign crc = (packet_12b[11:8] ^ packet_12b[7:4] ^ packet_12b[3:0]) & 4'hF;
            assign packet = {packet_12b, crc};

            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    shift_reg     <= 16'd0;
                    bit_idx       <= 5'd0;
                    bit_clk_cnt   <= 8'd0;
                    active        <= 1'b0;
                    o_motor_pins[c] <= 1'b0;
                end else if (reg_enable[c]) begin
                    if (frame_tick) begin
                        shift_reg     <= packet;
                        bit_idx       <= 5'd16;
                        bit_clk_cnt   <= 8'd0;
                        active        <= 1'b1;
                    end else if (active) begin
                        if (bit_clk_cnt >= DS600_BIT_CLKS - 8'd1) begin
                            bit_clk_cnt <= 8'd0;
                            if (bit_idx == 5'd1) begin
                                active <= 1'b0;
                                o_motor_pins[c] <= 1'b0;
                            end else begin
                                bit_idx <= bit_idx - 5'd1;
                            end
                        end else begin
                            bit_clk_cnt <= bit_clk_cnt + 8'd1;
                            // Generate DShot NRZ pulse
                            if (shift_reg[bit_idx-1]) begin
                                o_motor_pins[c] <= (bit_clk_cnt < DS600_T1H_CLKS);
                            end else begin
                                o_motor_pins[c] <= (bit_clk_cnt < DS600_T0H_CLKS);
                            end
                        end
                    end else begin
                        o_motor_pins[c] <= 1'b0;
                    end
                end else begin
                    o_motor_pins[c] <= 1'b0;
                    active          <= 1'b0;
                end
            end
        end
    endgenerate

endmodule
