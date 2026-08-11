// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

module top_tang9k (
    input  wire  i_clk,       // 27MHz
    input  wire  i_reset_n,   // Button S2

    // Host SPI / Dual-SPI Interface
    input  wire  i_spi_sclk,
    input  wire  i_spi_cs_n,
    inout  wire  io_spi_io0,
    inout  wire  io_spi_io1,

    // External IMU SPI Interface
    output wire  o_imu_sclk,
    output wire  o_imu_cs_n,
    output wire  o_imu_mosi,
    input  wire  i_imu_miso,
    input  wire  i_imu_int,

    // Host Doorbell IRQ & LEDs
    output wire  o_int_req,
    output wire [3:0] o_motor_pins,
    output wire  o_neopixel_pin,
    output wire [5:0] o_led   // 6 LEDs on Tang Nano 9K
);

    wire clk_logic = i_clk;
    wire lock = 1'b1;
    wire rst_n = i_reset_n & lock;

    // 1 Hz FPGA Hardware Heartbeat Blinker for LED 0
    logic [24:0] hb_cnt;
    logic        hb_led;
    always_ff @(posedge clk_logic or negedge rst_n) begin
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

    asp_top u_asp_top (
        .clk        (clk_logic),
        .rst_n      (rst_n),
        .spi_sclk   (i_spi_sclk),
        .spi_cs_n   (i_spi_cs_n),
        .spi_io0    (io_spi_io0),
        .spi_io1    (io_spi_io1),

        .imu_sclk   (o_imu_sclk),
        .imu_cs_n   (o_imu_cs_n),
        .imu_mosi   (o_imu_mosi),
        .imu_miso   (i_imu_miso),
        .imu_int_i  (i_imu_int),

        .o_motor_pins(o_motor_pins),
        .o_neopixel_pin(o_neopixel_pin),
        .o_led      (asp_led_bits),

        .o_int_req  (o_int_req)
    );

    // LED 0 = FPGA Heartbeat Blinker (1 Hz)
    // LEDs 1..5 = Free & Linux-Controllable over PCIe TLP register 0x40000008
    assign o_led[0]   = hb_led;
    assign o_led[5:1] = asp_led_bits[5:1];

endmodule
