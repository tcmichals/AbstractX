// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// AbstractX Top-Level Wrapper for Trenz CYC1000 (Intel/Altera Cyclone 10 LP 10CL025)
//
// Bridges the onboard 12.0 MHz oscillator and Arduino MKR header pins
// into the universal AbstractX switch fabric and peripherals.
//
// @impl [SPEC-TARGET-01] hw/cyc1000/top_cyc1000.sv

module top_cyc1000 (
    input  wire        i_clk,       // 12.0 MHz Onboard MEMS Clock (Pin M2)
    input  wire        i_reset_n,   // User Pushbutton S1 (Pin N6)

    // Host SPI / Dual-SPI Interface (MKR Header D8, D11, D10, D9)
    input  wire        i_spi_sclk,  // Pin M1 (D8)
    input  wire        i_spi_cs_n,  // Pin P1 (D11)
    inout  wire        io_spi_io0,  // Pin N2 (D10 / MOSI)
    inout  wire        io_spi_io1,  // Pin N1 (D9  / MISO)

    // External IMU Master SPI Interface (ICM-42688-P)
    output wire        o_imu_sclk,  // Pin B1 (D0)
    output wire        o_imu_cs_n,  // Pin C2 (D1)
    output wire        o_imu_mosi,  // Pin J1 (D2)
    input  wire        i_imu_miso,  // Pin J2 (D3)
    input  wire        i_imu_int,   // Pin K1 (D4)

    // Actuator Outputs (Motor DShot / NeoPixel)
    output wire [3:0]  o_motor_pins,// Pins K2, L1, P2, R1 (D5, D6, D12, A0)
    output wire        o_neopixel_pin, // Pin T1 (A1)

    // PWM Receiver Input Capture Channels 1..4
    input  wire [3:0]  i_pwm_pins,

    // Host Doorbell IRQ Pin
    output wire        o_int_req,   // Pin L2 (D7)

    // Onboard Status LEDs (8 User LEDs)
    output wire [7:0]  o_led
);

    wire clk_logic = i_clk;
    wire rst_n = i_reset_n;

    // 1 Hz FPGA Hardware Heartbeat Blinker on LED 0 (12 MHz clock)
    logic [23:0] hb_cnt;
    logic        hb_led;
    always_ff @(posedge clk_logic or negedge rst_n) begin
        if (!rst_n) begin
            hb_cnt <= 24'd0;
            hb_led <= 1'b0;
        end else if (hb_cnt >= 24'd5_999_999) begin
            hb_cnt <= 24'd0;
            hb_led <= ~hb_led;
        end else begin
            hb_cnt <= hb_cnt + 24'd1;
        end
    end

    wire [5:0] asp_led_bits;

    assign o_led[0] = hb_led;
    assign o_led[1] = ~i_spi_cs_n;   // Host SPI Active
    assign o_led[2] = i_imu_int;     // IMU DRDY Active
    assign o_led[3] = o_int_req;     // Host Doorbell Active
    assign o_led[7:4] = asp_led_bits[3:0];

    // Instantiate universal AbstractX Top-Level Fabric
    asp_top #(
        .DUAL_SPI_ENABLE(1'b1)
    ) u_asp_top (
        .clk          (clk_logic),
        .rst_n        (rst_n),
        .spi_sclk     (i_spi_sclk),
        .spi_cs_n     (i_spi_cs_n),
        .spi_io0      (io_spi_io0),
        .spi_io1      (io_spi_io1),
        .imu_sclk     (o_imu_sclk),
        .imu_cs_n     (o_imu_cs_n),
        .imu_mosi     (o_imu_mosi),
        .imu_miso     (i_imu_miso),
        .imu_int_i    (i_imu_int),
        .o_motor_pins (o_motor_pins),
        .o_neopixel_pin(o_neopixel_pin),
        .i_pwm_pins   (i_pwm_pins),
        .o_led        (asp_led_bits),
        .o_debug_pins (),
        .o_int_req    (o_int_req)
    );

endmodule
`default_nettype wire
