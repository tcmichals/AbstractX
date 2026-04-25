// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

module top_primer20k (
    input  wire  i_clk,       // 27MHz (H11)
    input  wire  i_reset_n,   // Button
    
    input  wire  i_spi_sclk,
    input  wire  i_spi_cs_n,
    input  wire  i_spi_mosi,
    output wire  o_spi_miso,
    
    output wire [5:0] o_led   // LEDs
);

    wire clk_logic;
    wire lock;

    assign clk_logic = i_clk;
    assign lock = 1'b1;

    wire rst_n = i_reset_n & lock;

    asp_top u_asp_top (
        .clk        (clk_logic),
        .rst_n      (rst_n),
        .spi_sclk   (i_spi_sclk),
        .spi_cs_n   (i_spi_cs_n),
        .spi_mosi   (i_spi_mosi),
        .spi_miso   (o_spi_miso),
        .int_req    ()
    );

    // Map some internal state to LEDs (likely low-active on Primer 20K)
    assign o_led[0] = ~(~rst_n); 
    assign o_led[1] = ~i_spi_cs_n;
    assign o_led[2] = ~i_spi_sclk;
    assign o_led[3] = 1'b0; // On
    assign o_led[4] = 1'b0;
    assign o_led[5] = 1'b0;

endmodule
