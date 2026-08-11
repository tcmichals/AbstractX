// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX System & LED Control Registers (Wishbone Slave)
// Base Address: 0x40000000
//
// Register Map:
// 0x40000000: SYS_VERSION (Read Only, 0xA1B2C3D4)
// 0x40000004: SCRATCH / LOOPBACK (Read / Write)
// 0x40000008: LED_CTRL (Read / Write, Bit 0..5 controls Onboard LEDs 0..5 for Linux)

module asp_sys_regs #(
    parameter logic [31:0] SYS_VERSION = 32'hA1B2C3D4
) (
    input  wire        clk,
    input  wire        rst,

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
    logic [31:0] led_reg; // Active-Low LEDs: 0 = ON, 1 = OFF

    assign o_led_bits = led_reg[5:0];

    always_ff @(posedge clk) begin
        if (rst) begin
            wb_ack_o    <= 1'b0;
            wb_dat_o    <= 32'h0;
            scratch_reg <= 32'h0;
            led_reg     <= 32'h3F; // Default OFF (Active-Low)
        end else begin
            wb_ack_o <= 1'b0;
            
            if (wb_cyc_i && wb_stb_i && !wb_ack_o) begin
                wb_ack_o <= 1'b1;

                if (wb_we_i) begin
                    // Write operation
                    case (wb_adr_i[7:0])
                        8'h04: scratch_reg <= wb_dat_i;
                        8'h08: led_reg     <= wb_dat_i; // Linux toggles LEDs 0..5
                        default: ;
                    endcase
                end else begin
                    // Read operation
                    case (wb_adr_i[7:0])
                        8'h00:   wb_dat_o <= SYS_VERSION;
                        8'h04:   wb_dat_o <= scratch_reg;
                        8'h08:   wb_dat_o <= led_reg;
                        default: wb_dat_o <= 32'hDEADBEEF;
                    endcase
                end
            end
        end
    end
endmodule

`default_nettype wire
