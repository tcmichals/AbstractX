// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
`default_nettype none
// Fixed PIO GPIO <-> board port map shared by every top ([SPEC-TLP-07] pin table).
// An output is the PIO's value while it drives the pin (pindir = 1), else the pin's idle level.
// @impl [SPEC-TLP-07] docs/DESIGN_SPECIFICATION.md
module asp_pio_pins #(
    parameter int NUM_PWM_INPUTS = 4          // at most 8 (GPIO 12..19)
) (
    // PIO tile side
    input  wire  [31:0] i_gpio_out,
    input  wire  [31:0] i_gpio_pindir,
    output logic [31:0] o_gpio_in,
    // Board side
    output logic [3:0]  o_motor_pins,         // GPIO 0..3, idle 0
    output logic        o_neopixel_pin,       // GPIO 4, idle 0
    output logic        o_uart_tx,            // GPIO 5, idle 1
    input  wire         i_uart_rx,            // GPIO 6
    output logic        o_imu_sclk,           // GPIO 7, idle 0
    output logic        o_imu_cs_n,           // GPIO 8, idle 1
    output logic        o_imu_mosi,           // GPIO 9, idle 0
    input  wire         i_imu_miso,           // GPIO 10
    input  wire         i_imu_int,            // GPIO 11
    input  wire  [NUM_PWM_INPUTS-1:0] i_pwm_pins  // GPIO 12..
);
    localparam int PWM_N = (NUM_PWM_INPUTS > 8) ? 8 : NUM_PWM_INPUTS;
    localparam logic [9:0] IDLE = 10'b01_0010_0000;   // GPIO5 (uart tx) and GPIO8 (imu cs_n) idle high

    logic [9:0] pin_o;
    always_comb begin
        for (int k = 0; k < 10; k++) pin_o[k] = i_gpio_pindir[k] ? i_gpio_out[k] : IDLE[k];
        o_gpio_in     = 32'd0;
        o_gpio_in[6]  = i_uart_rx;
        o_gpio_in[10] = i_imu_miso;
        o_gpio_in[11] = i_imu_int;
        for (int k = 0; k < PWM_N; k++) o_gpio_in[12 + k] = i_pwm_pins[k];
    end

    assign o_motor_pins   = pin_o[3:0];
    assign o_neopixel_pin = pin_o[4];
    assign o_uart_tx      = pin_o[5];
    assign o_imu_sclk     = pin_o[7];
    assign o_imu_cs_n     = pin_o[8];
    assign o_imu_mosi     = pin_o[9];

    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused = &{1'b0, i_gpio_out[31:10], i_gpio_pindir[31:10], pin_o[6]};
    /* verilator lint_on UNUSEDSIGNAL */
endmodule
`default_nettype wire
