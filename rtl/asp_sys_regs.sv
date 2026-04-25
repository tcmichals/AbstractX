// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX System Registers (Wishbone Slave)
// Provides queryable version and loopback scratch register.
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
    output logic [31:0] wb_dat_o
);
    // Register Map (Offset from base):
    // 0x00 : SYS_VERSION (Read Only)
    // 0x04 : SCRATCH / LOOPBACK (Read / Write)

    logic [31:0] scratch_reg;

    always_ff @(posedge clk) begin
        if (rst) begin
            wb_ack_o    <= 1'b0;
            wb_dat_o    <= 32'h0;
            scratch_reg <= 32'h0;
        end else begin
            wb_ack_o <= 1'b0;
            
            if (wb_cyc_i && wb_stb_i && !wb_ack_o) begin
                wb_ack_o <= 1'b1; // 1-cycle acknowledge

                if (wb_we_i) begin
                    // Write operation
                    if (wb_adr_i[7:0] == 8'h04) begin
                        if (wb_sel_i[3]) scratch_reg[31:24] <= wb_dat_i[31:24];
                        if (wb_sel_i[2]) scratch_reg[23:16] <= wb_dat_i[23:16];
                        if (wb_sel_i[1]) scratch_reg[15:8]  <= wb_dat_i[15:8];
                        if (wb_sel_i[0]) scratch_reg[7:0]   <= wb_dat_i[7:0];
                    end
                end else begin
                    // Read operation
                    unique case (wb_adr_i[7:0])
                        8'h00:   wb_dat_o <= SYS_VERSION;
                        8'h04:   wb_dat_o <= scratch_reg;
                        default: wb_dat_o <= 32'hDEADBEEF; // Unaligned/Unknown mapping
                    endcase
                end
            end
        end
    end
endmodule

`default_nettype wire
