// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX Hardware NeoPixel (WS2812B) LED Controller IP Core
// Base Address: 0x40000600
//
// Features:
// - Single-wire 800 kHz NRZ pulse timing (T0H = 350ns, T1H = 700ns, Bit Period = 1250ns)
// - Supports up to 32 RGB/RGBW NeoPixels in series
// - Wishbone slave interface for status LED animations

module asp_neopixel_core #(
    parameter int CLK_FREQ_HZ = 50_000_000,
    parameter int MAX_LEDS = 32
) (
    input  wire        clk,
    input  wire        rst_n,

    // Wishbone Slave Interface (Base: 0x40000600)
    input  wire        wb_cyc,
    input  wire        wb_stb,
    input  wire        wb_we,
    input  wire [31:0] wb_addr,
    input  wire [31:0] wb_data_i,
    output logic[31:0] wb_data_o,
    output logic       wb_ack,

    // NeoPixel Output Pin
    output logic       o_neopixel_pin
);

    // WS2812B Timing Constants (@ 50 MHz clock)
    // Bit Period = 1.25 us = 63 clocks
    // T0H = 0.35 us = 18 clocks
    // T1H = 0.70 us = 35 clocks
    // Reset Latch = > 50 us = 2500 clocks
    localparam logic [15:0] CLKS_BIT = 16'd63;
    localparam logic [15:0] CLKS_T0H = 16'd18;
    localparam logic [15:0] CLKS_T1H = 16'd35;
    localparam logic [15:0] CLKS_RST = 16'd2500;

    logic [23:0] led_colors [0:MAX_LEDS-1];
    logic [31:0] reg_ctrl; // [0] = Enable, [7:0] = Active LED count

    // Wishbone Writes
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_ctrl  <= 32'd1; // Default: Enable, 1 LED
            wb_ack    <= 1'b0;
            wb_data_o <= 32'd0;
            for (int i = 0; i < MAX_LEDS; i++) led_colors[i] <= 24'd0;
        end else begin
            wb_ack <= 1'b0;
            if (wb_cyc && wb_stb && !wb_ack) begin
                wb_ack <= 1'b1;
                if (wb_we) begin
                    if (wb_addr[7:0] == 8'h00) begin
                        reg_ctrl <= wb_data_i;
                    end else if (wb_addr[7:2] > 6'd0 && wb_addr[7:2] <= MAX_LEDS[5:0]) begin
                        led_colors[wb_addr[7:2]-6'd1] <= wb_data_i[23:0];
                    end
                end else begin
                    if (wb_addr[7:0] == 8'h00) wb_data_o <= reg_ctrl;
                    else                       wb_data_o <= 32'd0;
                end
            end
        end
    end

    // FSM State Machine for NRZ Transmission
    typedef enum logic [1:0] { ST_IDLE = 2'b00, ST_SEND_BIT = 2'b01, ST_LATCH_RST = 2'b10 } state_t;
    state_t state;

    logic [5:0]  led_idx;
    logic [4:0]  bit_idx; // 24 bits per GRB pixel (Green[7:0], Red[7:0], Blue[7:0])
    logic [15:0] timer;
    logic [23:0] cur_grb;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state          <= ST_IDLE;
            timer          <= 16'd0;
            led_idx        <= 6'd0;
            bit_idx        <= 5'd23;
            o_neopixel_pin <= 1'b0;
            cur_grb        <= 24'd0;
        end else if (reg_ctrl[0]) begin
            case (state)
                ST_IDLE: begin
                    o_neopixel_pin <= 1'b0;
                    timer          <= 16'd0;
                    led_idx        <= 6'd0;
                    bit_idx        <= 5'd23;
                    cur_grb        <= led_colors[0];
                    state          <= ST_SEND_BIT;
                end

                ST_SEND_BIT: begin
                    if (timer >= CLKS_BIT - 16'd1) begin
                        timer <= 16'd0;
                        if (bit_idx == 5'd0) begin
                            bit_idx <= 5'd23;
                            if (led_idx >= (reg_ctrl[5:0] - 6'd1)) begin
                                state <= ST_LATCH_RST;
                            end else begin
                                led_idx <= led_idx + 6'd1;
                                cur_grb <= led_colors[led_idx + 6'd1];
                            end
                        end else begin
                            bit_idx <= bit_idx - 5'd1;
                        end
                    end else begin
                        timer <= timer + 16'd1;
                        if (cur_grb[bit_idx]) begin
                            o_neopixel_pin <= (timer < CLKS_T1H);
                        end else begin
                            o_neopixel_pin <= (timer < CLKS_T0H);
                        end
                    end
                end

                ST_LATCH_RST: begin
                    o_neopixel_pin <= 1'b0;
                    if (timer >= CLKS_RST) begin
                        timer <= 16'd0;
                        state <= ST_IDLE;
                    end else begin
                        timer <= timer + 16'd1;
                    end
                end

                default: begin
                    state <= ST_IDLE;
                end
            endcase
        end else begin
            o_neopixel_pin <= 1'b0;
            state          <= ST_IDLE;
        end
    end

endmodule
