// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
//
// AbstractX Wishbone PWM Receiver Decoder & Input Capture IP Core (`asp_pwm_decoder.sv`)
// Mapped to TLP Wishbone BAR Address: 0x40000300
//
// Register Map (Read-Only, 32-bit word-aligned):
//   0x40000300: CTRL / Status [31:16]=ID (0x0001), [15:8]=NUM_CH, [7:0]=Ready Flags
//   0x40000304: CH1 Pulse Width [15:0] (microseconds) + Error Flags [31:16]
//   0x40000308: CH2 Pulse Width [15:0] (microseconds) + Error Flags [31:16]
//   0x4000030C: CH3 Pulse Width [15:0] (microseconds) + Error Flags [31:16]
//   0x40000310: CH4 Pulse Width [15:0] (microseconds) + Error Flags [31:16]
//
// Architecture:
// - Shared 1MHz global timebase counter for microsecond precision.
// - 2-stage anti-metastability input synchronizer per channel.
// - Rising/falling edge timestamp capture with pulse width calculation.
// - Guard interval bounds (750us - 2600us) & 20ms loss-of-signal timeout detection.

`default_nettype none

module asp_pwm_decoder #(
    parameter int CLK_FREQ_HZ  = 27_000_000,
    parameter int NUM_CHANNELS = 4
) (
    input  wire        clk,
    input  wire        rst_n,

    // Physical RC Receiver / PWM Input Vector
    input  wire [NUM_CHANNELS-1:0] i_pwm_pins,

    // Wishbone Slave Interface (Base Address: 0x40000300)
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    input  wire        wb_we_i,
    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    output logic [31:0] wb_dat_o,
    output logic        wb_ack_o
);

    // -----------------------------------------------------------------
    // Shared 1 MHz Microsecond Timebase
    // -----------------------------------------------------------------
    localparam int CLK_DIVIDER = (CLK_FREQ_HZ / 1_000_000) - 1;

    logic [15:0] tick_counter;
    logic        tick_1us;
    logic [15:0] global_timer;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tick_counter <= '0;
            tick_1us     <= 1'b0;
            global_timer <= '0;
        end else begin
            if (tick_counter >= CLK_DIVIDER[15:0]) begin
                tick_counter <= '0;
                tick_1us     <= 1'b1;
                global_timer <= global_timer + 16'd1;
            end else begin
                tick_counter <= tick_counter + 16'd1;
                tick_1us     <= 1'b0;
            end
        end
    end

    // -----------------------------------------------------------------
    // PWM Guard Interval & Error Flag Constants
    // -----------------------------------------------------------------
    localparam logic [15:0] GUARD_TIME_ON_MIN  = 16'd750;   // Min valid RC pulse (750us)
    localparam logic [15:0] GUARD_TIME_ON_MAX  = 16'd2600;  // Max valid RC pulse (2600us)
    localparam logic [15:0] GUARD_TIME_OFF_MAX = 16'd20000; // 20ms loss-of-signal timeout

    localparam logic [15:0] GUARD_ERROR_SHORT  = 16'h4000;  // Pulse too short (<750us)
    localparam logic [15:0] GUARD_ERROR_HIGH   = 16'h8000;  // Pulse too long (>2600us)
    localparam logic [15:0] GUARD_ERROR_LOW    = 16'hC000;  // Loss of signal timeout

    logic [NUM_CHANNELS-1:0] pwm_ready_flags;
    logic [31:0]             pwm_values [0:NUM_CHANNELS-1];

    // -----------------------------------------------------------------
    // Input Capture Channels (Synchronizer + Edge Timer)
    // -----------------------------------------------------------------
    genvar i;
    generate
        for (i = 0; i < NUM_CHANNELS; i++) begin : gen_pwm_ch
            logic [1:0]  sync;
            logic [15:0] start_time;
            logic        measuring;
            logic [15:0] off_timer;

            always_ff @(posedge clk or negedge rst_n) begin
                if (!rst_n) begin
                    sync               <= 2'b00;
                    start_time         <= '0;
                    measuring          <= 1'b0;
                    off_timer          <= '0;
                    pwm_ready_flags[i] <= 1'b0;
                    pwm_values[i]      <= {GUARD_ERROR_LOW, 16'd0};
                end else begin
                    // 2-stage anti-metastability synchronizer
                    sync <= {sync[0], i_pwm_pins[i]};

                    // Pulse ready flag for 1 clock cycle upon completed measurement
                    pwm_ready_flags[i] <= 1'b0;

                    // Edge detection logic
                    if (sync[1] == 1'b0 && sync[0] == 1'b1) begin
                        // Rising edge: latch start time
                        start_time <= global_timer;
                        measuring  <= 1'b1;
                        off_timer  <= '0;
                    end else if (sync[1] == 1'b1 && sync[0] == 1'b0 && measuring) begin
                        // Falling edge: calculate pulse width delta
                        logic [15:0] delta;
                        delta = global_timer - start_time;
                        measuring <= 1'b0;
                        pwm_ready_flags[i] <= 1'b1;

                        if (delta < GUARD_TIME_ON_MIN) begin
                            pwm_values[i] <= {GUARD_ERROR_SHORT, delta};
                        end else if (delta > GUARD_TIME_ON_MAX) begin
                            pwm_values[i] <= {GUARD_ERROR_HIGH, delta};
                        end else begin
                            pwm_values[i] <= {16'h0000, delta}; // Valid RC Pulse (1000us - 2000us)
                        end
                    end

                    // Loss of signal timeout logic (20ms without rising edge)
                    if (tick_1us) begin
                        if (sync[1] == 1'b0) begin
                            if (off_timer < GUARD_TIME_OFF_MAX) begin
                                off_timer <= off_timer + 16'd1;
                            end else if (off_timer == GUARD_TIME_OFF_MAX) begin
                                pwm_values[i] <= {GUARD_ERROR_LOW, 16'd0};
                                pwm_ready_flags[i] <= 1'b1;
                                off_timer <= off_timer + 16'd1; // Prevent continuous strobing
                            end
                        end else begin
                            off_timer <= '0;
                        end
                    end
                end
            end
        end
    endgenerate

    // -----------------------------------------------------------------
    // Wishbone Read Slave Multiplexer
    // -----------------------------------------------------------------
    wire [4:0] reg_offset = wb_adr_i[6:2];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wb_dat_o <= 32'h0;
            wb_ack_o <= 1'b0;
        end else begin
            wb_ack_o <= wb_stb_i && wb_cyc_i && !wb_ack_o;

            if (wb_stb_i && wb_cyc_i && !wb_we_i) begin
                if (reg_offset == 5'h00) begin
                    // CTRL / Status Register: [31:16]=ID, [15:8]=NUM_CHANNELS, [7:0]=Ready Flags
                    wb_dat_o <= {16'h0001, 8'(NUM_CHANNELS), 8'(pwm_ready_flags)};
                end else if (reg_offset >= 5'h01 && reg_offset <= 5'h04) begin
                    // Read Channel 1..4 Measured Pulse Width
                    int ch_idx;
                    ch_idx = int'(reg_offset) - 1;
                    if (ch_idx < NUM_CHANNELS) begin
                        wb_dat_o <= pwm_values[ch_idx];
                    end else begin
                        wb_dat_o <= 32'h0;
                    end
                end else begin
                    wb_dat_o <= 32'h0;
                end
            end
        end
    end

    // Unused input lint suppression
    logic _unused;
    always_comb _unused = &{1'b0, wb_dat_i, wb_adr_i[31:7], wb_adr_i[1:0], 1'b0};

endmodule

`default_nettype wire
