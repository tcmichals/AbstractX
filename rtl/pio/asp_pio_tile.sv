// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// AbstractX High-Performance PIO Tile Subsystem
//
// Features:
// 1. Quad Concurrent State Machines (SM0..SM3) with RP2040 ISA compatibility.
// 2. Shared 32-word x 16-bit Dual-Port Instruction Memory.
// 3. Internal 8-bit IRQ Bus (irq 0..7) for Supervisor/Worker State Machine Chaining.
// 4. Integrated 64-Byte TLP Packetizer Shim directly driving asp_router.sv.
// 5. Autonomous barectf CTF 1.8 Timeline Trace generation (Stream 5).
// 6. Wishbone B4 Pipelined Slave Interface (Base Address: 0x40000700).
//
// @impl [SPEC-PIO-01] [SPEC-PIO-02] [SPEC-PIO-03] [SPEC-PIO-04] [SPEC-PIO-05] [SPEC-PIO-07] rtl/pio/SPECIFICATION.md
module asp_pio_tile #(
    parameter logic [31:0] BASE_ADDR = 32'h40000700,
    parameter int          TILE_ID   = 0
) (
    input  wire          clk,
    input  wire          rst_n,

    // Monotonic 64-bit nanosecond system timebase
    input  wire [63:0]   i_sys_timestamp,

    // External Physical GPIO Pins (32-bit bus)
    input  wire [31:0]   i_gpio,
    output logic [31:0]  o_gpio_out,
    output logic [31:0]  o_gpio_pindir,

    // Wishbone B4 Slave Bus Interface (Base: 0x40000700)
    input  wire          wb_cyc_i,
    input  wire          wb_stb_i,
    input  wire          wb_we_i,
    input  wire [31:0]   wb_adr_i,
    input  wire [31:0]   wb_dat_i,
    output logic [31:0]  wb_dat_o,
    output logic         wb_ack_o,

    // Egress 512-bit (64-Byte) AXI-Stream to asp_router.sv
    output logic [511:0] m_tlp_tdata,
    output logic         m_tlp_tvalid,
    input  wire          m_tlp_tready,

    // Hardware Tile Interrupt Request to CPU
    output logic         o_pio_irq
);

    // ------------------------------------------------------------------------
    // Shared Dual-Port Instruction Memory (32 Words x 16-Bit)
    // ------------------------------------------------------------------------
    logic [15:0] iram [0:31];
    logic [4:0]  sm_pc [0:3];
    logic [15:0] sm_instr [0:3];

    // Read ports for the 4 state machines
    always_comb begin
        for (int s = 0; s < 4; s++) begin
            sm_instr[s] = iram[sm_pc[s]];
        end
    end

    // ------------------------------------------------------------------------
    // Shared Internal IRQ Bus (irq 0..7)
    // ------------------------------------------------------------------------
    logic [7:0] irq_flags;
    logic [7:0] sm_irq_set [0:3];
    logic [7:0] sm_irq_clr [0:3];
    logic [7:0] combined_irq_set;
    logic [7:0] combined_irq_clr;
    logic [7:0] wb_irq_clr;

    always_comb begin
        combined_irq_set = sm_irq_set[0] | sm_irq_set[1] | sm_irq_set[2] | sm_irq_set[3];
        combined_irq_clr = sm_irq_clr[0] | sm_irq_clr[1] | sm_irq_clr[2] | sm_irq_clr[3] | wb_irq_clr;
    end

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            irq_flags <= 8'd0;
        end else begin
            irq_flags <= (irq_flags | combined_irq_set) & ~combined_irq_clr;
        end
    end

    assign o_pio_irq = |irq_flags[3:0]; // Lower 4 IRQs route to host interrupt

    // ------------------------------------------------------------------------
    // Tile-Level Configuration & Status Registers
    // ------------------------------------------------------------------------
    logic [3:0] sm_en;
    logic [3:0] sm_restart;
    logic [3:0] sm_clkdiv_restart;

    logic [23:0] sm_clkdiv    [0:3];
    logic [31:0] sm_execctrl  [0:3];
    logic [31:0] sm_shiftctrl [0:3];
    logic [31:0] sm_pinctrl   [0:3];

    logic        tlp_en;
    logic [7:0]  tlp_tag;
    logic [7:0]  tlp_channel;
    logic [1:0]  tlp_stream_sm;
    logic [2:0]  burst_words;
    logic        trace_en;

    // FIFO Interface wires per State Machine
    logic [31:0] sm_tx_wdata  [0:3];
    logic [3:0]  sm_tx_wvalid;
    logic [3:0]  sm_tx_ready;

    logic [31:0] sm_rx_rdata  [0:3];
    logic [3:0]  sm_rx_rvalid;
    logic [3:0]  sm_rx_ren;

    // Pin signals per State Machine
    logic [31:0] sm_gpio_out    [0:3];
    logic [31:0] sm_gpio_out_en [0:3];
    logic [31:0] sm_gpio_pindir [0:3];
    logic [31:0] sm_gpio_pindir_en [0:3];

    // Status per State Machine
    logic [3:0]  sm_stalled;
    logic [3:0]  sm_active;

    // Pin Aggregation (OR with enable mask)
    always_comb begin
        o_gpio_out    = 32'd0;
        o_gpio_pindir = 32'd0;
        for (int p = 0; p < 32; p++) begin
            for (int s = 0; s < 4; s++) begin
                if (sm_gpio_out_en[s][p]) begin
                    o_gpio_out[p] = sm_gpio_out[s][p];
                end
                if (sm_gpio_pindir_en[s][p]) begin
                    o_gpio_pindir[p] = sm_gpio_pindir[s][p];
                end
            end
        end
    end

    // ------------------------------------------------------------------------
    // Instantiate 4 State Machines (SM0 .. SM3)
    // ------------------------------------------------------------------------
    generate
        for (genvar s = 0; s < 4; s++) begin : gen_sm
            asp_pio_sm #(
                .SM_ID(s)
            ) u_sm (
                .clk              (clk),
                .rst_n            (rst_n),
                .i_en             (sm_en[s]),
                .i_restart        (sm_restart[s]),
                .i_clkdiv_restart (sm_clkdiv_restart[s]),
                .i_clkdiv         (sm_clkdiv[s]),
                .i_execctrl       (sm_execctrl[s]),
                .i_shiftctrl      (sm_shiftctrl[s]),
                .i_pinctrl        (sm_pinctrl[s]),
                .o_pc             (sm_pc[s]),
                .i_instr          (sm_instr[s]),
                .i_gpio           (i_gpio),
                .o_gpio_out       (sm_gpio_out[s]),
                .o_gpio_out_en    (sm_gpio_out_en[s]),
                .o_gpio_pindir    (sm_gpio_pindir[s]),
                .o_gpio_pindir_en (sm_gpio_pindir_en[s]),
                .i_tx_wdata       (sm_tx_wdata[s]),
                .i_tx_wvalid      (sm_tx_wvalid[s]),
                .o_tx_ready       (sm_tx_ready[s]),
                .o_rx_rdata       (sm_rx_rdata[s]),
                .o_rx_rvalid      (sm_rx_rvalid[s]),
                .i_rx_ren         (sm_rx_ren[s]),
                .i_irq            (irq_flags),
                .o_irq_set        (sm_irq_set[s]),
                .o_irq_clr        (sm_irq_clr[s]),
                .o_exec_stalled   (sm_stalled[s]),
                .o_sm_active      (sm_active[s])
            );
        end
    endgenerate

    // ------------------------------------------------------------------------
    // Autonomous TLP & Trace Packetizer Shim
    // ------------------------------------------------------------------------
    logic [31:0] shim_data;
    logic        shim_valid;
    logic        shim_ready;
    logic        sm_trigger_pulse;

    always_comb begin
        shim_data  = sm_rx_rdata[tlp_stream_sm];
        shim_valid = sm_rx_rvalid[tlp_stream_sm];
    end

    asp_pio_tlp_shim #(
        .BASE_ADDR(BASE_ADDR)
    ) u_tlp_shim (
        .clk             (clk),
        .rst_n           (rst_n),
        .i_sys_timestamp (i_sys_timestamp),
        .i_tlp_en        (tlp_en),
        .i_tlp_tag       (tlp_tag),
        .i_tlp_channel   (tlp_channel),
        .i_burst_words   (burst_words),
        .i_trace_en      (trace_en),
        .i_tile_id       (8'(TILE_ID)),
        .i_sm_data       (shim_data),
        .i_sm_valid      (shim_valid),
        .o_sm_ready      (shim_ready),
        .i_sm_trigger    (sm_trigger_pulse),
        .i_sm_active     (sm_active),
        .i_sm_stalled    (sm_stalled),
        .i_sm_pc         (sm_pc),
        .i_irq_pulse     (combined_irq_set),
        .m_tlp_tdata     (m_tlp_tdata),
        .m_tlp_tvalid    (m_tlp_tvalid),
        .m_tlp_tready    (m_tlp_tready)
    );

    // ------------------------------------------------------------------------
    // Wishbone B4 Slave Bus Interface (Base: 0x40000700)
    // ------------------------------------------------------------------------
    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused_addr = |wb_adr_i[31:12];
    /* verilator lint_on UNUSEDSIGNAL */
    wire wb_sel = wb_cyc_i && wb_stb_i;
    wire [11:0] reg_offset = wb_adr_i[11:0] - BASE_ADDR[11:0];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wb_ack_o            <= 1'b0;
            wb_dat_o            <= 32'd0;
            wb_irq_clr          <= 8'd0;
            sm_en               <= 4'd0;
            sm_restart          <= 4'd0;
            sm_clkdiv_restart   <= 4'd0;
            sm_tx_wvalid        <= 4'd0;
            sm_rx_ren           <= 4'd0;
            tlp_en              <= 1'b0;
            tlp_tag             <= 8'd5;  // Default IMU Tag 5
            tlp_channel         <= 8'd2;  // Default Telemetry Channel 2
            tlp_stream_sm       <= 2'd1;  // Default SM1 Supervisor
            burst_words         <= 3'd4;  // Default 4 words (16 bytes)
            trace_en            <= 1'b0;
            sm_trigger_pulse    <= 1'b0;

            for (int i = 0; i < 32; i++) begin
                iram[i] <= 16'd0;
            end
            for (int s = 0; s < 4; s++) begin
                sm_clkdiv[s]    <= {16'd1, 8'd0}; // Div = 1.0 (full speed)
                sm_execctrl[s]  <= (32'd31 << 12) | (32'd0 << 7); // Wrap bottom=0, top=31
                sm_shiftctrl[s] <= 32'd0;
                sm_pinctrl[s]   <= 32'd0;
                sm_tx_wdata[s]  <= 32'd0;
            end
        end else begin
            wb_ack_o         <= 1'b0;
            wb_irq_clr       <= 8'd0;
            sm_restart       <= 4'd0;
            sm_clkdiv_restart<= 4'd0;
            sm_tx_wvalid     <= 4'd0;
            sm_rx_ren        <= 4'd0;
            sm_trigger_pulse <= 1'b0;

            // Route TLP Shim read enable to active SM RX FIFO when streaming
            if (tlp_en && shim_valid && shim_ready) begin
                sm_rx_ren[tlp_stream_sm] <= 1'b1;
            end

            if (wb_sel && !wb_ack_o) begin
                wb_ack_o <= 1'b1;

                if (wb_we_i) begin
                    // Write Transactions
                    case (reg_offset)
                        12'h000: begin // CTRL
                            sm_en             <= wb_dat_i[3:0];
                            sm_restart        <= wb_dat_i[7:4];
                            sm_clkdiv_restart <= wb_dat_i[11:8];
                        end
                        12'h004: begin // IRQ Clear
                            wb_irq_clr <= wb_dat_i[7:0];
                        end
                        12'h00C: begin // TLP Configuration
                            tlp_en        <= wb_dat_i[0];
                            tlp_tag       <= wb_dat_i[15:8];
                            tlp_channel   <= wb_dat_i[23:16];
                            tlp_stream_sm <= wb_dat_i[25:24];
                            burst_words   <= wb_dat_i[28:26];
                            trace_en      <= wb_dat_i[31];
                        end
                        default: begin
                            // Instruction Memory (0x010 .. 0x08C)
                            if (reg_offset >= 12'h010 && reg_offset <= 12'h08C) begin
                                int w_idx;
                                w_idx = (int'(reg_offset) - 32'h010) >> 2;
                                if (w_idx >= 0 && w_idx < 32) begin
                                    iram[w_idx] <= wb_dat_i[15:0];
                                end
                            end

                            // State Machine Registers (0x100 + 0x20 * N)
                            for (int s = 0; s < 4; s++) begin
                                int base_sm;
                                base_sm = 32'h100 + s * 32'h020;
                                if (reg_offset == 12'(base_sm + 12'h00)) begin
                                    sm_clkdiv[s] <= wb_dat_i[23:0];
                                end else if (reg_offset == 12'(base_sm + 12'h04)) begin
                                    sm_execctrl[s] <= wb_dat_i;
                                end else if (reg_offset == 12'(base_sm + 12'h08)) begin
                                    sm_shiftctrl[s] <= wb_dat_i;
                                end else if (reg_offset == 12'(base_sm + 12'h0C)) begin
                                    sm_pinctrl[s] <= wb_dat_i;
                                end else if (reg_offset == 12'(base_sm + 12'h10)) begin
                                    // TX FIFO Write
                                    sm_tx_wdata[s]  <= wb_dat_i;
                                    sm_tx_wvalid[s] <= 1'b1;
                                end else if (reg_offset == 12'(base_sm + 12'h1C)) begin
                                    // Software Trigger Pulse
                                    sm_trigger_pulse <= 1'b1;
                                end
                            end
                        end
                    endcase
                end else begin
                    // Read Transactions
                    case (reg_offset)
                        12'h000: wb_dat_o <= {28'd0, sm_en};
                        12'h004: wb_dat_o <= {24'd0, irq_flags};
                        12'h008: begin // FIFO Status
                            wb_dat_o <= {
                                ~sm_rx_rvalid, // RX empty
                                ~sm_tx_ready,  // TX full
                                24'd0
                            };
                        end
                        12'h00C: begin // TLP Configuration
                            wb_dat_o <= {
                                trace_en,
                                2'd0,
                                burst_words,
                                tlp_stream_sm,
                                tlp_channel,
                                tlp_tag,
                                7'd0,
                                tlp_en
                            };
                        end
                        default: begin
                            // Instruction Memory Read
                            if (reg_offset >= 12'h010 && reg_offset <= 12'h08C) begin
                                int r_idx;
                                r_idx = (int'(reg_offset) - 32'h010) >> 2;
                                if (r_idx >= 0 && r_idx < 32) begin
                                    wb_dat_o <= {16'd0, iram[r_idx]};
                                end else begin
                                    wb_dat_o <= 32'd0;
                                end
                            end else begin
                                // State Machine Registers Read
                                wb_dat_o <= 32'd0;
                                for (int s = 0; s < 4; s++) begin
                                    int base_sm;
                                    base_sm = 32'h100 + s * 32'h020;
                                    if (reg_offset == 12'(base_sm + 12'h00)) begin
                                        wb_dat_o <= {8'd0, sm_clkdiv[s]};
                                    end else if (reg_offset == 12'(base_sm + 12'h04)) begin
                                        wb_dat_o <= sm_execctrl[s];
                                    end else if (reg_offset == 12'(base_sm + 12'h08)) begin
                                        wb_dat_o <= sm_shiftctrl[s];
                                    end else if (reg_offset == 12'(base_sm + 12'h0C)) begin
                                        wb_dat_o <= sm_pinctrl[s];
                                    end else if (reg_offset == 12'(base_sm + 12'h14)) begin
                                        // RX FIFO Read
                                        wb_dat_o     <= sm_rx_rdata[s];
                                        sm_rx_ren[s] <= 1'b1;
                                    end else if (reg_offset == 12'(base_sm + 12'h18)) begin
                                        // Status
                                        wb_dat_o <= {25'd0, sm_active[s], sm_stalled[s], sm_pc[s]};
                                    end
                                end
                            end
                        end
                    endcase
                end
            end
        end
    end

endmodule

`default_nettype wire
