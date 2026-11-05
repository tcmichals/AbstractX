// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
`default_nettype none
// AbstractX System, ID & Master Timestamp Registers (AXI4-Lite slave through Forencich axil_reg_if)
// Base Address: 0x40000000
//
// 0x00: SYS_ID_REV    (RO, 0xABF10164 -> Device ID 0xABF1, Rev 0x01, Arch 0x64)
// 0x04: SYS_VENDOR_ID (RO, 0x19981ACC)
// 0x08: SYS_SCRATCH   (RW, host loopback)
// 0x0C: SYS_LED_CTRL  (RW, bits 0..5 drive the board LEDs)
// 0x10: SYS_TIME_LOW  (RO, timestamp [31:0]; reading it latches [63:32] into TIME_HIGH)
// 0x14: SYS_TIME_HIGH (RO, latched timestamp [63:32])
// 0x18: CTRL_WR_ERR   (RO, control writes answered SLVERR/DECERR, wraps; [SPEC-TLP-04])
// other offsets read 0xDEADBEEF, writes are ignored
//
// @impl [SPEC-TLP-02] [SPEC-TLP-04] docs/DESIGN_SPECIFICATION.md
module asp_sys_regs #(
    parameter logic [31:0] SYS_ID_REV    = 32'hABF10164,
    parameter logic [31:0] SYS_VENDOR_ID = 32'h19981ACC
) (
    input  wire         clk,
    input  wire         rst_n,
    input  wire  [63:0] i_sys_timestamp,
    input  wire         i_wr_err,            // one clock per failed control write

    input  wire  [31:0] s_axil_awaddr,
    input  wire  [2:0]  s_axil_awprot,
    input  wire         s_axil_awvalid,
    output logic        s_axil_awready,
    input  wire  [31:0] s_axil_wdata,
    input  wire  [3:0]  s_axil_wstrb,
    input  wire         s_axil_wvalid,
    output logic        s_axil_wready,
    output logic [1:0]  s_axil_bresp,
    output logic        s_axil_bvalid,
    input  wire         s_axil_bready,
    input  wire  [31:0] s_axil_araddr,
    input  wire  [2:0]  s_axil_arprot,
    input  wire         s_axil_arvalid,
    output logic        s_axil_arready,
    output logic [31:0] s_axil_rdata,
    output logic [1:0]  s_axil_rresp,
    output logic        s_axil_rvalid,
    input  wire         s_axil_rready,

    output logic [5:0]  o_led_bits
);
    logic [31:0] reg_wr_addr, reg_wr_data, reg_rd_addr;
    logic [3:0]  reg_wr_strb;
    logic        reg_wr_en, reg_rd_en;
    logic [31:0] reg_rd_data;
    logic        reg_rd_ack, reg_wr_ack;

    axil_reg_if #(.DATA_WIDTH(32), .ADDR_WIDTH(32)) u_if (
        .clk(clk), .rst(!rst_n),
        .s_axil_awaddr(s_axil_awaddr), .s_axil_awprot(s_axil_awprot), .s_axil_awvalid(s_axil_awvalid),
        .s_axil_awready(s_axil_awready), .s_axil_wdata(s_axil_wdata), .s_axil_wstrb(s_axil_wstrb),
        .s_axil_wvalid(s_axil_wvalid), .s_axil_wready(s_axil_wready), .s_axil_bresp(s_axil_bresp),
        .s_axil_bvalid(s_axil_bvalid), .s_axil_bready(s_axil_bready),
        .s_axil_araddr(s_axil_araddr), .s_axil_arprot(s_axil_arprot), .s_axil_arvalid(s_axil_arvalid),
        .s_axil_arready(s_axil_arready), .s_axil_rdata(s_axil_rdata), .s_axil_rresp(s_axil_rresp),
        .s_axil_rvalid(s_axil_rvalid), .s_axil_rready(s_axil_rready),
        .reg_wr_addr(reg_wr_addr), .reg_wr_data(reg_wr_data), .reg_wr_strb(reg_wr_strb),
        .reg_wr_en(reg_wr_en), .reg_wr_wait(1'b0), .reg_wr_ack(reg_wr_ack),
        .reg_rd_addr(reg_rd_addr), .reg_rd_en(reg_rd_en), .reg_rd_data(reg_rd_data),
        .reg_rd_wait(1'b0), .reg_rd_ack(reg_rd_ack)
    );

    logic [31:0] scratch_reg, led_reg, time_high_shadow, wr_err_count;
    assign o_led_bits = led_reg[5:0];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            scratch_reg      <= 32'd0;
            led_reg          <= 32'd0;
            time_high_shadow <= 32'd0;
            wr_err_count     <= 32'd0;
            reg_wr_ack       <= 1'b0;
            reg_rd_ack       <= 1'b0;
            reg_rd_data      <= 32'd0;
        end else begin
            reg_wr_ack <= 1'b0;
            reg_rd_ack <= 1'b0;
            if (i_wr_err) wr_err_count <= wr_err_count + 32'd1;
            if (reg_wr_en && !reg_wr_ack) begin
                reg_wr_ack <= 1'b1;
                unique case (reg_wr_addr[7:0])
                    8'h08: scratch_reg <= reg_wr_data;
                    8'h0C: led_reg     <= reg_wr_data;
                    default: ;
                endcase
            end
            if (reg_rd_en && !reg_rd_ack) begin
                reg_rd_ack <= 1'b1;
                unique case (reg_rd_addr[7:0])
                    8'h00: reg_rd_data <= SYS_ID_REV;
                    8'h04: reg_rd_data <= SYS_VENDOR_ID;
                    8'h08: reg_rd_data <= scratch_reg;
                    8'h0C: reg_rd_data <= led_reg;
                    8'h10: begin
                        reg_rd_data      <= i_sys_timestamp[31:0];
                        time_high_shadow <= i_sys_timestamp[63:32];
                    end
                    8'h14: reg_rd_data <= time_high_shadow;
                    8'h18: reg_rd_data <= wr_err_count;
                    default: reg_rd_data <= 32'hDEADBEEF;
                endcase
            end
        end
    end

    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused = &{1'b0, reg_wr_addr[31:8], reg_rd_addr[31:8], reg_wr_strb};
    /* verilator lint_on UNUSEDSIGNAL */
endmodule
`default_nettype wire
