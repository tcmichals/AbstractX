// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX PCIe System, ID & Master Timestamp Registers (Wishbone Slave)
// Base Address: 0x40000000
//
// PCIe BAR Register Map:
// 0x40000000: REG_SYS_ID_REV    (Read-Only, 0xABF10164 -> Device ID: 0xABF1, Rev: 0x01, Arch: 0x64)
// 0x40000004: REG_SYS_VENDOR_ID (Read-Only, 0x19981ACC -> Subsys: 0x1998, Vendor: 0x1ACC)
// 0x40000008: REG_SYS_SCRATCH   (Read/Write, Host Loopback Scratchpad)
// 0x4000000C: REG_SYS_LED_CTRL  (Read/Write, Bit 1..5 controls Onboard LEDs 2..6 for Linux)
// 0x40000010: REG_SYS_TIME_LOW  (Read-Only, 64-bit Master Timestamp Low [31:0] - Latches High [63:32])
// 0x40000014: REG_SYS_TIME_HIGH (Read-Only, Latched Shadow Master Timestamp High [63:32])

module asp_sys_regs #(
    parameter logic [31:0] SYS_ID_REV    = 32'hABF10164, // Device 0xABF1, Rev 0x01, Arch 0x64
    parameter logic [31:0] SYS_VENDOR_ID = 32'h19981ACC  // Subsys 0x1998, Vendor 0x1ACC
) (
    input  wire        clk,
    input  wire        rst,

    // Central Master Hardware Timestamp Counter (64-bit nanoseconds)
    input  wire [63:0] i_sys_timestamp,

    // Wishbone Slave Interface
    input  wire [31:0] wb_adr_i,
    input  wire [31:0] wb_dat_i,
    input  wire [3:0]  wb_sel_i,
    input  wire        wb_we_i,
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    output logic       wb_ack_o,
    output logic [31:0] wb_dat_o,

    // Linux-Controllable Onboard LED Output Bits (0..5)
    output logic [5:0] o_led_bits
);

    logic [31:0] scratch_reg;
    logic [31:0] led_reg;          // Active-Low LEDs: 0 = ON, 1 = OFF
    logic [31:0] time_high_shadow; // Atomic shadow register for 64-bit timestamp

    assign o_led_bits = led_reg[5:0];

    always_ff @(posedge clk) begin
        if (rst) begin
            wb_ack_o         <= 1'b0;
            wb_dat_o         <= 32'h0;
            scratch_reg      <= 32'hCAFEBABE; // Initial default scratch pattern
            led_reg          <= 32'h3E;       // Default OFF for LEDs 1..5 (Active-Low)
            time_high_shadow <= 32'h0;
        end else begin
            wb_ack_o <= 1'b0;
            
            if (wb_cyc_i && wb_stb_i && !wb_ack_o) begin
                wb_ack_o <= 1'b1;

                if (wb_we_i) begin
                    // Write operation
                    case (wb_adr_i[7:0])
                        8'h08: scratch_reg <= wb_dat_i; // Host Scratch Loopback
                        8'h0C: led_reg     <= wb_dat_i; // Linux Onboard LED Control
                        default: ;
                    endcase
                end else begin
                    // Read operation
                    case (wb_adr_i[7:0])
                        8'h00:   wb_dat_o <= SYS_ID_REV;    // Device ID & Revision
                        8'h04:   wb_dat_o <= SYS_VENDOR_ID; // Vendor ID
                        8'h08:   wb_dat_o <= scratch_reg;   // Scratch Loopback
                        8'h0C:   wb_dat_o <= led_reg;       // LED Status
                        8'h10: begin                        // Timestamp Low + Atomic Shadow Latch
                            wb_dat_o         <= i_sys_timestamp[31:0];
                            time_high_shadow <= i_sys_timestamp[63:32];
                        end
                        8'h14:   wb_dat_o <= time_high_shadow; // Latched Shadow Timestamp High
                        default: wb_dat_o <= 32'hDEADBEEF;
                    endcase
                end
            end
        end
    end
endmodule

`default_nettype wire
