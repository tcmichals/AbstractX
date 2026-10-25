// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// AbstractX RP2040-Compatible PIO State Machine Engine
//
// Features:
// 1. Full 16-bit RP2040 Instruction Set Architecture:
//    - JMP, WAIT, IN, OUT, PUSH, PULL, MOV, IRQ, SET
// 2. Fractional 16.8-bit clock divider with phase accumulator.
// 3. Independent 32-bit X, Y, ISR, OSR registers with Autopush & Autopull.
// 4. Synchronous 4-word TX and RX FIFOs.
// 5. Concurrent Delay cycles & Side-set pin driving.
// 6. Direct Inter-SM IRQ bus (irq 0..7) for Supervisor/Worker handshakes.
//
// @impl [SPEC-PIO-01] [SPEC-PIO-02] [SPEC-PIO-03] rtl/pio/SPECIFICATION.md
module asp_pio_sm #(
    parameter int SM_ID = 0
) (
    input  wire         clk,
    input  wire         rst_n,

    // Enable and Control Signals
    input  wire         i_en,
    input  wire         i_restart,
    input  wire         i_clkdiv_restart,

    // Configuration Registers (Wishbone Mapped)
    /* verilator lint_off UNUSEDSIGNAL */
    input  wire [23:0]  i_clkdiv,       // [23:8] Int, [7:0] Frac
    input  wire [31:0]  i_execctrl,
    input  wire [31:0]  i_shiftctrl,
    input  wire [31:0]  i_pinctrl,
    /* verilator lint_on UNUSEDSIGNAL */

    // Instruction Memory Interface
    output logic [4:0]  o_pc,
    input  wire  [15:0] i_instr,

    // Physical GPIO Bus (All 32 pins)
    input  wire  [31:0] i_gpio,
    output logic [31:0] o_gpio_out,
    output logic [31:0] o_gpio_out_en,
    output logic [31:0] o_gpio_pindir,
    output logic [31:0] o_gpio_pindir_en,

    // TX FIFO Interface (Host / Inter-SM write -> SM read)
    input  wire  [31:0] i_tx_wdata,
    input  wire         i_tx_wvalid,
    output logic        o_tx_ready,

    // RX FIFO Interface (SM write -> Host / TLP Shim read)
    output logic [31:0] o_rx_rdata,
    output logic        o_rx_rvalid,
    input  wire         i_rx_ren,

    // Inter-SM IRQ Bus
    input  wire  [7:0]  i_irq,
    output logic [7:0]  o_irq_set,
    output logic [7:0]  o_irq_clr,

    // Observability / Status
    output logic        o_exec_stalled,
    output logic        o_sm_active
);

    // ------------------------------------------------------------------------
    // Configuration Fields Unpacking (RP2040 / RP2350 Standard Layout)
    // ------------------------------------------------------------------------
    wire [4:0] wrap_bottom     = i_execctrl[11:7];
    wire [4:0] wrap_top        = i_execctrl[16:12];
    wire [4:0] jmp_pin         = i_execctrl[28:24];
    wire [2:0] sideset_count   = i_pinctrl[31:29];
    wire       sideset_opt     = i_execctrl[30];
    wire       sideset_pindirs = i_execctrl[29];
    /* verilator lint_off UNUSEDSIGNAL */
    wire [4:0] status_n        = i_execctrl[4:0];
    /* verilator lint_on UNUSEDSIGNAL */
    wire       status_sel      = i_execctrl[5];

    wire       in_shift_dir    = i_shiftctrl[18]; // 0=Left, 1=Right
    wire       out_shift_dir   = i_shiftctrl[19]; // 0=Left, 1=Right
    wire       autopush        = i_shiftctrl[16];
    wire       autopull        = i_shiftctrl[17];
    wire [4:0] push_thresh_raw = i_shiftctrl[24:20];
    wire [4:0] pull_thresh_raw = i_shiftctrl[29:25];
    wire [5:0] push_thresh     = (push_thresh_raw == 5'd0) ? 6'd32 : {1'b0, push_thresh_raw};
    wire [5:0] pull_thresh     = (pull_thresh_raw == 5'd0) ? 6'd32 : {1'b0, pull_thresh_raw};

    wire [4:0] in_base         = i_pinctrl[4:0];
    wire [4:0] out_base        = i_pinctrl[9:5];
    wire [4:0] set_base        = i_pinctrl[14:10];
    wire [4:0] side_base       = i_pinctrl[19:15];
    wire [5:0] out_count       = i_pinctrl[25:20];
    wire [2:0] set_count       = i_pinctrl[28:26];

    // ------------------------------------------------------------------------
    // Clock Divider (16.8 Fractional Phase Accumulator)
    // ------------------------------------------------------------------------
    logic [15:0] clkdiv_int_cnt;
    logic [7:0]  clkdiv_frac_acc;
    logic        sm_clk_enable;

    wire [15:0] div_int  = i_clkdiv[23:8];
    wire [7:0]  div_frac = i_clkdiv[7:0];

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            clkdiv_int_cnt  <= 16'd0;
            clkdiv_frac_acc <= 8'd0;
            sm_clk_enable   <= 1'b0;
        end else if (i_clkdiv_restart || !i_en) begin
            clkdiv_int_cnt  <= 16'd0;
            clkdiv_frac_acc <= 8'd0;
            sm_clk_enable   <= 1'b0;
        end else if (div_int == 16'd0 && div_frac == 8'd0) begin
            // 0 divider = full clock rate
            sm_clk_enable   <= 1'b1;
            clkdiv_int_cnt  <= 16'd0;
        end else begin
            if (clkdiv_int_cnt == 16'd0) begin
                logic [8:0] next_frac;
                next_frac = {1'b0, clkdiv_frac_acc} + {1'b0, div_frac};
                clkdiv_frac_acc <= next_frac[7:0];
                clkdiv_int_cnt  <= div_int + (next_frac[8] ? 16'd1 : 16'd0) - 16'd1;
                sm_clk_enable   <= 1'b1;
            end else begin
                clkdiv_int_cnt <= clkdiv_int_cnt - 16'd1;
                sm_clk_enable  <= 1'b0;
            end
        end
    end

    // ------------------------------------------------------------------------
    // Core Working Registers & Shift Registers
    // ------------------------------------------------------------------------
    logic [4:0]  pc;
    logic [31:0] reg_x;
    logic [31:0] reg_y;
    logic [31:0] isr;
    logic [5:0]  isr_count;
    logic [31:0] osr;
    logic [5:0]  osr_count;
    logic [4:0]  delay_cycles;
    logic        exec_override_valid;
    logic [15:0] exec_override_instr;

    assign o_pc        = pc;
    assign o_sm_active = i_en;

    // ------------------------------------------------------------------------
    // Synchronous 4-Word FIFOs
    // ------------------------------------------------------------------------
    logic [31:0] tx_fifo [0:3];
    logic [1:0]  tx_wr_ptr, tx_rd_ptr;
    logic [2:0]  tx_level;

    logic [31:0] rx_fifo [0:3];
    logic [1:0]  rx_wr_ptr, rx_rd_ptr;
    logic [2:0]  rx_level;

    wire tx_full  = (tx_level == 3'd4);
    wire tx_empty = (tx_level == 3'd0);
    wire rx_full  = (rx_level == 3'd4);
    wire rx_empty = (rx_level == 3'd0);

    assign o_tx_ready = !tx_full;
    assign o_rx_rvalid = !rx_empty;
    assign o_rx_rdata  = rx_fifo[rx_rd_ptr];

    // FIFO Push/Pull signals from SM execution
    logic        sm_pull_req;
    logic [31:0] sm_pull_data;
    logic        sm_push_req;
    logic [31:0] sm_push_data;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_wr_ptr <= 2'd0;
            tx_rd_ptr <= 2'd0;
            tx_level  <= 3'd0;
            rx_wr_ptr <= 2'd0;
            rx_rd_ptr <= 2'd0;
            rx_level  <= 3'd0;
        end else if (i_restart) begin
            tx_wr_ptr <= 2'd0;
            tx_rd_ptr <= 2'd0;
            tx_level  <= 3'd0;
            rx_wr_ptr <= 2'd0;
            rx_rd_ptr <= 2'd0;
            rx_level  <= 3'd0;
        end else begin
            // TX FIFO Management
            if (i_tx_wvalid && !tx_full) begin
                tx_fifo[tx_wr_ptr] <= i_tx_wdata;
                tx_wr_ptr <= tx_wr_ptr + 2'd1;
                if (!sm_pull_req || tx_empty) begin
                    tx_level <= tx_level + 3'd1;
                end
            end
            if (sm_pull_req && !tx_empty) begin
                tx_rd_ptr <= tx_rd_ptr + 2'd1;
                if (!i_tx_wvalid || tx_full) begin
                    tx_level <= tx_level - 3'd1;
                end
            end

            // RX FIFO Management
            if (sm_push_req && !rx_full) begin
                rx_fifo[rx_wr_ptr] <= sm_push_data;
                rx_wr_ptr <= rx_wr_ptr + 2'd1;
                if (!i_rx_ren || rx_empty) begin
                    rx_level <= rx_level + 3'd1;
                end
            end
            if (i_rx_ren && !rx_empty) begin
                rx_rd_ptr <= rx_rd_ptr + 2'd1;
                if (!sm_push_req || rx_full) begin
                    rx_level <= rx_level - 3'd1;
                end
            end
        end
    end

    assign sm_pull_data = tx_fifo[tx_rd_ptr];

    // ------------------------------------------------------------------------
    // Active Instruction & Side-Set Extraction
    // ------------------------------------------------------------------------
    wire [15:0] curr_instr = exec_override_valid ? exec_override_instr : i_instr;
    wire [2:0]  opcode     = curr_instr[15:13];
    wire [4:0]  raw_delay  = curr_instr[12:8];

    // Decode Side-set cleanly without variable slice ranges
    logic [4:0] inst_delay;
    logic [4:0] inst_sideset_val;
    logic       inst_sideset_en;

    always_comb begin
        if (sideset_count == 3'd0) begin
            inst_sideset_en  = 1'b0;
            inst_sideset_val = 5'd0;
            inst_delay       = raw_delay;
        end else if (sideset_opt) begin
            inst_sideset_en = raw_delay[4];
            case (sideset_count)
                3'd1: begin
                    inst_sideset_val = {4'd0, raw_delay[3]};
                    inst_delay       = {2'd0, raw_delay[2:0]};
                end
                3'd2: begin
                    inst_sideset_val = {3'd0, raw_delay[3:2]};
                    inst_delay       = {3'd0, raw_delay[1:0]};
                end
                3'd3: begin
                    inst_sideset_val = {2'd0, raw_delay[3:1]};
                    inst_delay       = {4'd0, raw_delay[0]};
                end
                3'd4: begin
                    inst_sideset_val = {1'b0, raw_delay[3:0]};
                    inst_delay       = 5'd0;
                end
                default: begin
                    inst_sideset_val = {1'b0, raw_delay[3:0]};
                    inst_delay       = 5'd0;
                end
            endcase
        end else begin
            inst_sideset_en = 1'b1;
            case (sideset_count)
                3'd1: begin
                    inst_sideset_val = {4'd0, raw_delay[4]};
                    inst_delay       = {1'b0, raw_delay[3:0]};
                end
                3'd2: begin
                    inst_sideset_val = {3'd0, raw_delay[4:3]};
                    inst_delay       = {2'd0, raw_delay[2:0]};
                end
                3'd3: begin
                    inst_sideset_val = {2'd0, raw_delay[4:2]};
                    inst_delay       = {3'd0, raw_delay[1:0]};
                end
                3'd4: begin
                    inst_sideset_val = {1'b0, raw_delay[4:1]};
                    inst_delay       = {4'd0, raw_delay[0]};
                end
                3'd5: begin
                    inst_sideset_val = raw_delay[4:0];
                    inst_delay       = 5'd0;
                end
                default: begin
                    inst_sideset_val = 5'd0;
                    inst_delay       = raw_delay;
                end
            endcase
        end
    end

    // ------------------------------------------------------------------------
    // Bit-Reverse Helper Function for MOV
    // ------------------------------------------------------------------------
    function automatic logic [31:0] bit_reverse(input logic [31:0] val);
        logic [31:0] res;
        for (int i = 0; i < 32; i++) begin
            res[i] = val[31 - i];
        end
        return res;
    endfunction

    // ------------------------------------------------------------------------
    // Main State Machine Execution Pipeline
    // ------------------------------------------------------------------------
    logic stall_cycle;
    assign o_exec_stalled = stall_cycle;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc                  <= 5'd0;
            reg_x               <= 32'd0;
            reg_y               <= 32'd0;
            isr                 <= 32'd0;
            isr_count           <= 6'd0;
            osr                 <= 32'd0;
            osr_count           <= 6'd0;
            delay_cycles        <= 5'd0;
            exec_override_valid <= 1'b0;
            exec_override_instr <= 16'd0;
            o_gpio_out          <= 32'd0;
            o_gpio_out_en       <= 32'd0;
            o_gpio_pindir       <= 32'd0;
            o_gpio_pindir_en    <= 32'd0;
            o_irq_set           <= 8'd0;
            o_irq_clr           <= 8'd0;
            sm_pull_req         <= 1'b0;
            sm_push_req         <= 1'b0;
            sm_push_data        <= 32'd0;
            stall_cycle         <= 1'b0;
        end else if (i_restart) begin
            pc                  <= wrap_bottom;
            delay_cycles        <= 5'd0;
            exec_override_valid <= 1'b0;
            o_irq_set           <= 8'd0;
            o_irq_clr           <= 8'd0;
            sm_pull_req         <= 1'b0;
            sm_push_req         <= 1'b0;
            stall_cycle         <= 1'b0;
        end else begin
            // Clear single-cycle strobe pulses
            o_irq_set   <= 8'd0;
            o_irq_clr   <= 8'd0;
            sm_pull_req <= 1'b0;
            sm_push_req <= 1'b0;

            if (i_en && sm_clk_enable) begin
                if (delay_cycles > 5'd0) begin
                    delay_cycles <= delay_cycles - 5'd1;
                end else begin
                    // Instruction Decode & Execution
                    logic [4:0] next_pc;
                    logic       advance_pc;
                    logic       exec_stall;

                    next_pc     = (pc == wrap_top) ? wrap_bottom : (pc + 5'd1);
                    advance_pc  = 1'b1;
                    exec_stall  = 1'b0;

                    // Apply Side-Set immediately if valid
                    if (inst_sideset_en) begin
                        for (int i = 0; i < 5; i++) begin
                            if (3'(i) < sideset_count) begin
                                logic [4:0] p_idx;
                                p_idx = side_base + 5'(i);
                                if (sideset_pindirs) begin
                                    o_gpio_pindir[p_idx]    <= inst_sideset_val[i];
                                    o_gpio_pindir_en[p_idx] <= 1'b1;
                                end else begin
                                    o_gpio_out[p_idx]       <= inst_sideset_val[i];
                                    o_gpio_out_en[p_idx]    <= 1'b1;
                                end
                            end
                        end
                    end

                    case (opcode)
                        // ----------------------------------------------------
                        // JMP (3'b000)
                        // ----------------------------------------------------
                        3'b000: begin
                            logic cond_met;
                            case (curr_instr[7:5])
                                3'b000: cond_met = 1'b1;                      // Unconditional
                                3'b001: cond_met = (reg_x == 32'd0);          // !X
                                3'b010: begin                                 // X--
                                    cond_met = (reg_x != 32'd0);
                                    reg_x    <= reg_x - 32'd1;
                                end
                                3'b011: cond_met = (reg_y == 32'd0);          // !Y
                                3'b100: begin                                 // Y--
                                    cond_met = (reg_y != 32'd0);
                                    reg_y    <= reg_y - 32'd1;
                                end
                                3'b101: cond_met = (reg_x != reg_y);          // X != Y
                                3'b110: cond_met = i_gpio[jmp_pin];           // PIN
                                3'b111: cond_met = (osr_count < pull_thresh); // !OSRE
                            endcase
                            if (cond_met) begin
                                next_pc = curr_instr[4:0];
                            end
                        end

                        // ----------------------------------------------------
                        // WAIT (3'b001)
                        // ----------------------------------------------------
                        3'b001: begin
                            logic polarity;
                            logic pin_val;
                            polarity = curr_instr[7];

                            case (curr_instr[6:5])
                                2'b00: pin_val = i_gpio[curr_instr[4:0]];                                // GPIO
                                2'b01: pin_val = i_gpio[(int'(in_base) + int'(curr_instr[4:0])) % 32];  // PIN
                                2'b10: pin_val = i_irq[curr_instr[2:0]];                                 // IRQ flag
                                default: pin_val = polarity;
                            endcase

                            if (pin_val != polarity) begin
                                exec_stall = 1'b1;
                                advance_pc = 1'b0;
                            end else if (curr_instr[6:5] == 2'b10 && curr_instr[7] == 1'b1) begin
                                // Clearing consumed IRQ
                                o_irq_clr[curr_instr[2:0]] <= 1'b1;
                            end
                        end

                        // ----------------------------------------------------
                        // IN (3'b010)
                        // ----------------------------------------------------
                        3'b010: begin
                            logic [4:0]  bit_count;
                            logic [31:0] in_val;
                            bit_count = (curr_instr[4:0] == 5'd0) ? 5'd31 : (curr_instr[4:0] - 5'd1);

                            case (curr_instr[7:5])
                                3'b000: begin // PINS
                                    for (int i = 0; i < 32; i++) begin
                                        in_val[i] = i_gpio[(int'(in_base) + i) % 32];
                                    end
                                end
                                3'b001: in_val = reg_x;
                                3'b010: in_val = reg_y;
                                3'b011: in_val = 32'd0;
                                3'b110: in_val = isr;
                                3'b111: in_val = osr;
                                default: in_val = 32'd0;
                            endcase

                            if (in_shift_dir) begin
                                // Shift Right
                                isr <= (in_val << (31 - bit_count)) | (isr >> (bit_count + 1));
                            end else begin
                                // Shift Left
                                isr <= (isr << (bit_count + 1)) | (in_val & ((1 << (bit_count + 1)) - 1));
                            end
                            isr_count <= isr_count + {1'b0, bit_count} + 6'd1;

                            if (autopush && (isr_count + {1'b0, bit_count} + 6'd1 >= push_thresh)) begin
                                if (!rx_full) begin
                                    sm_push_req  <= 1'b1;
                                    sm_push_data <= isr;
                                    isr          <= 32'd0;
                                    isr_count    <= 6'd0;
                                end else begin
                                    exec_stall <= 1'b1;
                                    advance_pc <= 1'b0;
                                end
                            end
                        end

                        // ----------------------------------------------------
                        // OUT (3'b011)
                        // ----------------------------------------------------
                        3'b011: begin
                            logic [4:0]  bit_count;
                            logic [31:0] out_val;
                            bit_count = (curr_instr[4:0] == 5'd0) ? 5'd31 : (curr_instr[4:0] - 5'd1);

                            // Auto-pull check if empty
                            if (autopull && (osr_count >= pull_thresh) && !tx_empty) begin
                                sm_pull_req <= 1'b1;
                                osr         <= sm_pull_data;
                                osr_count   <= 6'd0;
                            end

                            out_val = out_shift_dir ? (osr & ((1 << (bit_count + 1)) - 1)) : (osr >> (31 - bit_count));
                            osr     <= out_shift_dir ? (osr >> (bit_count + 1)) : (osr << (bit_count + 1));
                            osr_count <= osr_count + {1'b0, bit_count} + 6'd1;

                            case (curr_instr[7:5])
                                3'b000: begin // PINS
                                    for (int i = 0; i < 32; i++) begin
                                        if (6'(i) < out_count) begin
                                            logic [4:0] p_idx;
                                            p_idx = out_base + 5'(i);
                                            o_gpio_out[p_idx]    <= out_val[i];
                                            o_gpio_out_en[p_idx] <= 1'b1;
                                        end
                                    end
                                end
                                3'b001: reg_x <= out_val;
                                3'b010: reg_y <= out_val;
                                3'b011: ; // Discard
                                3'b100: begin // PINDIRS
                                    for (int i = 0; i < 32; i++) begin
                                        if (6'(i) < out_count) begin
                                            logic [4:0] p_idx;
                                            p_idx = out_base + 5'(i);
                                            o_gpio_pindir[p_idx]    <= out_val[i];
                                            o_gpio_pindir_en[p_idx] <= 1'b1;
                                        end
                                    end
                                end
                                3'b101: next_pc = out_val[4:0];
                                3'b110: isr     <= out_val;
                                3'b111: begin
                                    exec_override_valid <= 1'b1;
                                    exec_override_instr <= out_val[15:0];
                                end
                            endcase
                        end

                        // ----------------------------------------------------
                        // PUSH / PULL (3'b100)
                        // ----------------------------------------------------
                        3'b100: begin
                            if (!curr_instr[7]) begin
                                // PUSH
                                if (!rx_full) begin
                                    sm_push_req  <= 1'b1;
                                    sm_push_data <= isr;
                                    isr          <= 32'd0;
                                    isr_count    <= 6'd0;
                                end else if (curr_instr[5]) begin // Blocking
                                    exec_stall <= 1'b1;
                                    advance_pc <= 1'b0;
                                end
                            end else begin
                                // PULL
                                if (!tx_empty) begin
                                    sm_pull_req <= 1'b1;
                                    osr         <= sm_pull_data;
                                    osr_count   <= 6'd0;
                                end else if (curr_instr[5]) begin // Blocking
                                    exec_stall <= 1'b1;
                                    advance_pc <= 1'b0;
                                end
                            end
                        end

                        // ----------------------------------------------------
                        // MOV (3'b101)
                        // ----------------------------------------------------
                        3'b101: begin
                            logic [31:0] mov_src;
                            logic [31:0] mov_res;

                            case (curr_instr[2:0])
                                3'b000: begin
                                    for (int i = 0; i < 32; i++) begin
                                        mov_src[i] = i_gpio[(int'(in_base) + i) % 32];
                                    end
                                end
                                3'b001: mov_src = reg_x;
                                3'b010: mov_src = reg_y;
                                3'b011: mov_src = 32'd0;
                                3'b101: mov_src = status_sel ? {29'd0, rx_level} : {29'd0, tx_level};
                                3'b110: mov_src = isr;
                                3'b111: mov_src = osr;
                                default: mov_src = 32'd0;
                            endcase

                            case (curr_instr[4:3])
                                2'b00: mov_res = mov_src;
                                2'b01: mov_res = ~mov_src;
                                2'b10: mov_res = bit_reverse(mov_src);
                                default: mov_res = mov_src;
                            endcase

                            case (curr_instr[7:5])
                                3'b000: begin // PINS
                                    for (int i = 0; i < 32; i++) begin
                                        if (6'(i) < out_count) begin
                                            logic [4:0] p_idx;
                                            p_idx = out_base + 5'(i);
                                            o_gpio_out[p_idx]    <= mov_res[i];
                                            o_gpio_out_en[p_idx] <= 1'b1;
                                        end
                                    end
                                end
                                3'b001: reg_x <= mov_res;
                                3'b010: reg_y <= mov_res;
                                3'b100: begin
                                    exec_override_valid <= 1'b1;
                                    exec_override_instr <= mov_res[15:0];
                                end
                                3'b101: next_pc = mov_res[4:0];
                                3'b110: isr     <= mov_res;
                                3'b111: osr     <= mov_res;
                                default: ;
                            endcase
                        end

                        // ----------------------------------------------------
                        // IRQ (3'b110)
                        // RP2040: [6]=clr, [5]=wait, [4]=rel, [2:0]=index
                        // ----------------------------------------------------
                        3'b110: begin
                            logic [2:0] irq_idx;
                            irq_idx = curr_instr[4] ? (3'((int'(curr_instr[2:0]) + SM_ID) % 4)) : curr_instr[2:0];

                            if (curr_instr[6]) begin
                                o_irq_clr[irq_idx] <= 1'b1;
                            end else begin
                                o_irq_set[irq_idx] <= 1'b1;
                                if (curr_instr[5] && i_irq[irq_idx]) begin
                                    // Wait until cleared
                                    exec_stall <= 1'b1;
                                    advance_pc <= 1'b0;
                                end
                            end
                        end

                        // ----------------------------------------------------
                        // SET (3'b111)
                        // ----------------------------------------------------
                        3'b111: begin
                            logic [4:0] set_val;
                            set_val = curr_instr[4:0];

                            case (curr_instr[7:5])
                                3'b000: begin // PINS
                                    for (int i = 0; i < 5; i++) begin
                                        if (3'(i) < set_count) begin
                                            logic [4:0] p_idx;
                                            p_idx = set_base + 5'(i);
                                            o_gpio_out[p_idx]    <= set_val[i];
                                            o_gpio_out_en[p_idx] <= 1'b1;
                                        end
                                    end
                                end
                                3'b001: reg_x <= {27'd0, set_val};
                                3'b010: reg_y <= {27'd0, set_val};
                                3'b100: begin // PINDIRS
                                    for (int i = 0; i < 5; i++) begin
                                        if (3'(i) < set_count) begin
                                            logic [4:0] p_idx;
                                            p_idx = set_base + 5'(i);
                                            o_gpio_pindir[p_idx]    <= set_val[i];
                                            o_gpio_pindir_en[p_idx] <= 1'b1;
                                        end
                                    end
                                end
                                default: ;
                            endcase
                        end
                    endcase

                    if (advance_pc && !exec_stall) begin
                        pc           <= next_pc;
                        delay_cycles <= inst_delay;
                        stall_cycle  <= 1'b0;
                    end else begin
                        stall_cycle  <= 1'b1;
                    end
                end
            end
        end
    end

endmodule

`default_nettype wire
