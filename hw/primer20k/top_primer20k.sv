// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

module top_primer20k (
    input  wire  i_clk,       // 27MHz (H11)
    input  wire  i_reset_n,   // Button

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
    output wire [5:0] o_led   // LEDs
);

    wire clk_logic = i_clk;
    wire lock = 1'b1;
    wire rst_n = i_reset_n & lock;

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

        .o_int_req  (o_int_req)
    );

    // Map internal state to LEDs
    assign o_led[0] = ~rst_n; 
    assign o_led[1] = i_spi_cs_n;
    assign o_led[2] = i_spi_sclk;
    assign o_led[3] = o_int_req;
    assign o_led[4] = 1'b0;
    assign o_led[5] = 1'b0;

endmodule
