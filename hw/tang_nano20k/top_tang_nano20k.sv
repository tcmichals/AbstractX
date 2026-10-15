// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sipeed Tang Nano 20K (GW2AR-LV18QN88C8/I7) board top for Cubie A5E testing.
// Maps pins and the clock only; asp_top is unchanged ([SPEC-A5E-LINK-02]).
// Feature set ([SPEC-TN20K-03]): Dual-SPI A5E link, 4 DShot motors,
// 6 RC PWM inputs, 1 NeoPixel, 27 MHz fabric clock.
// @impl [SPEC-TN20K-02] [SPEC-TN20K-03] hw/tang_nano20k/SPECIFICATION.md

`default_nettype none

module top_tang_nano20k (
    input  wire       i_clk,          // 27 MHz on-board oscillator
    input  wire       i_reset_n,      // reset button (pin and polarity: open item)

    // A5E Dual-SPI link + doorbell ([SPEC-A5E-LINK-01])
    input  wire       i_spi_sclk,
    input  wire       i_spi_cs_n,
    inout  wire       io_spi_io0,
    inout  wire       io_spi_io1,
    output wire       o_int_req,

    // ICM-42688-P IMU
    output wire       o_imu_sclk,
    output wire       o_imu_cs_n,
    output wire       o_imu_mosi,
    input  wire       i_imu_miso,
    input  wire       i_imu_int,

    output wire [3:0] o_motor_pins,   // DShot600 x4
    input  wire [5:0] i_pwm_pins,     // RC PWM inputs x6
    output wire       o_neopixel_pin, // WS2812 data
    output wire [5:0] o_led           // on-board LEDs
);

    wire clk = i_clk;

    // Reset synchronizer: asynchronous assert, synchronous de-assert, so no
    // flop leaves reset on a different clock edge ([SPEC-TN20K-02]).
    logic [1:0] rst_sync;
    always_ff @(posedge clk or negedge i_reset_n) begin
        if (!i_reset_n) rst_sync <= 2'b00;
        else            rst_sync <= {rst_sync[0], 1'b1};
    end
    wire rst_n = rst_sync[1];

    // 1 Hz heartbeat on LED 0
    logic [24:0] hb_cnt;
    logic        hb_led;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            hb_cnt <= 25'd0;
            hb_led <= 1'b0;
        end else if (hb_cnt >= 25'd13_499_999) begin
            hb_cnt <= 25'd0;
            hb_led <= ~hb_led;
        end else begin
            hb_cnt <= hb_cnt + 25'd1;
        end
    end

    wire [5:0] asp_led_bits;
    wire [3:0] debug_pins_unused;

    asp_top #(
        .DUAL_SPI_ENABLE(1'b1),
        .CLK_FREQ_HZ    (27_000_000), // [SPEC-FPGA-CLK-01]
        .NUM_PWM_INPUTS (6)
    ) u_asp_top (
        .clk           (clk),
        .rst_n         (rst_n),
        .spi_sclk      (i_spi_sclk),
        .spi_cs_n      (i_spi_cs_n),
        .spi_io0       (io_spi_io0),
        .spi_io1       (io_spi_io1),
        .imu_sclk      (o_imu_sclk),
        .imu_cs_n      (o_imu_cs_n),
        .imu_mosi      (o_imu_mosi),
        .imu_miso      (i_imu_miso),
        .imu_int_i     (i_imu_int),
        .o_motor_pins  (o_motor_pins),
        .i_pwm_pins    (i_pwm_pins),
        .o_neopixel_pin(o_neopixel_pin),
        .o_led         (asp_led_bits),
        .o_debug_pins  (debug_pins_unused),
        .o_int_req     (o_int_req)
    );

    assign o_led[0]   = hb_led;
    assign o_led[5:1] = asp_led_bits[5:1];

endmodule

`default_nettype wire
