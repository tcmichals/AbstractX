// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// AbstractX RP2040-compatible PIO state machine: runs pioasm output unchanged
// ([SPEC-PIO-02] conformance list). FIFOs, shift registers and the execution
// pipeline update in one always_ff so a pull/push and the FIFO pointer move in
// the same cycle.
//
// @impl [SPEC-PIO-01] [SPEC-PIO-02] [SPEC-PIO-03] [SPEC-PIO-15] rtl/pio/SPECIFICATION.md
module asp_pio_sm #(
    parameter int SM_ID = 0
) (
    input  wire         clk,
    input  wire         rst_n,

    // Enable and Control Signals
    input  wire         i_en,
    input  wire         i_restart,
    input  wire         i_clkdiv_restart,

    // Immediate instruction (SMx_INSTR write): runs next cycle, even when disabled
    input  wire  [15:0] i_exec_instr,
    input  wire         i_exec_valid,

    // Configuration Registers
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
    // Pin writes of this clock, one-clock strobes; the tile keeps the latches ([SPEC-PIO-02] rule 10)
    output logic [31:0] o_out_wmask,
    output logic [31:0] o_out_wval,
    output logic [31:0] o_dir_wmask,
    output logic [31:0] o_dir_wval,

    // TX FIFO Interface (Host / TLP ingress write -> SM read)
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
    output logic        o_exec_latched,     // EXECCTRL.EXEC_STALLED: an SMx_INSTR waits
    output logic        o_sm_active
);

    // ------------------------------------------------------------------------
    // Configuration fields (RP2040 register layout)
    // ------------------------------------------------------------------------
    wire [3:0] status_n        = i_execctrl[3:0];
    wire       status_sel      = i_execctrl[4];
    wire [4:0] wrap_bottom     = i_execctrl[11:7];
    wire [4:0] wrap_top        = i_execctrl[16:12];
    wire [4:0] jmp_pin         = i_execctrl[28:24];
    wire       sideset_pindirs = i_execctrl[29];
    wire       sideset_opt     = i_execctrl[30];

    wire       autopush        = i_shiftctrl[16];
    wire       autopull        = i_shiftctrl[17];
    wire       in_shift_right  = i_shiftctrl[18];
    wire       out_shift_right = i_shiftctrl[19];
    wire [5:0] push_thresh     = (i_shiftctrl[24:20] == 5'd0) ? 6'd32 : {1'b0, i_shiftctrl[24:20]};
    wire [5:0] pull_thresh     = (i_shiftctrl[29:25] == 5'd0) ? 6'd32 : {1'b0, i_shiftctrl[29:25]};
    wire       join_tx         = i_shiftctrl[30];   // FJOIN_TX: 8-deep TX, no RX
    wire       join_rx         = i_shiftctrl[31] && !join_tx;
    wire [3:0] tx_cap          = join_tx ? 4'd8 : (join_rx ? 4'd0 : 4'd4);
    wire [3:0] rx_cap          = join_rx ? 4'd8 : (join_tx ? 4'd0 : 4'd4);

    wire [4:0] in_base         = i_pinctrl[19:15];
    wire [4:0] out_base        = i_pinctrl[4:0];
    wire [4:0] set_base        = i_pinctrl[9:5];
    wire [4:0] side_base       = i_pinctrl[14:10];
    wire [5:0] out_count       = i_pinctrl[25:20];
    wire [2:0] set_count       = i_pinctrl[28:26];
    wire [2:0] sideset_count   = i_pinctrl[31:29];  // includes the enable bit when SIDE_EN

    // ------------------------------------------------------------------------
    // Clock divider (16.8 fractional)
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
        end else if (div_int <= 16'd1 && div_frac == 8'd0) begin
            sm_clk_enable   <= 1'b1;  // divider 0 or 1.0: every clock
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
    // State
    // ------------------------------------------------------------------------
    logic [4:0]  pc;
    logic [31:0] reg_x, reg_y, isr, osr;
    logic [5:0]  isr_count, osr_count;   // osr_count 32 = empty
    logic [4:0]  delay_cycles;
    logic        exec_pending;           // EXEC (OUT/MOV EXEC or SMx_INSTR) runs next
    logic [15:0] exec_instr;
    logic        exec_forced;            // SMx_INSTR: run even when disabled
    logic        push_pending;           // autopush waiting for RX FIFO space
    logic [4:0]  pend_delay;             // delay of the IN whose autopush is pending
    logic        irq_waiting;            // `irq wait` set its flag, waiting for clear
    logic        stalled;

    // TX and RX share 8 words: TX 0..3 and RX 4..7, or all 8 for a joined FIFO ([SPEC-PIO-02] rule 8)
    logic [31:0] fifo_mem [0:7];
    logic [2:0]  tx_wr_ptr, tx_rd_ptr;
    logic [3:0]  tx_level;
    logic [2:0]  rx_wr_ptr, rx_rd_ptr;
    logic [3:0]  rx_level;
    logic [1:0]  join_q;

    function automatic logic [2:0] tx_idx(input logic [2:0] ptr, input logic jt);
        tx_idx = jt ? ptr : {1'b0, ptr[1:0]};
    endfunction
    function automatic logic [2:0] rx_idx(input logic [2:0] ptr, input logic jr);
        rx_idx = jr ? ptr : {1'b1, ptr[1:0]};
    endfunction

    assign o_pc           = pc;
    assign o_sm_active    = i_en;
    assign o_exec_stalled = stalled;
    assign o_exec_latched = exec_pending && exec_forced;
    assign o_tx_ready     = tx_level != tx_cap;
    assign o_rx_rvalid    = rx_level != 4'd0;
    assign o_rx_rdata     = fifo_mem[rx_idx(rx_rd_ptr, join_rx)];

    function automatic logic [31:0] bit_reverse(input logic [31:0] v);
        logic [31:0] r;
        for (int i = 0; i < 32; i++) r[i] = v[31 - i];
        bit_reverse = r;
    endfunction

    // Area: one rotator per pin path instead of a 32x32 crossbar per instruction (EL-1027-41)
    function automatic logic [31:0] rotl(input logic [31:0] v, input logic [4:0] n);
        rotl = (n == 5'd0) ? v : ((v << n) | (v >> (6'd32 - {1'b0, n})));
    endfunction

    function automatic logic [31:0] rotr(input logic [31:0] v, input logic [4:0] n);
        rotr = (n == 5'd0) ? v : ((v >> n) | (v << (6'd32 - {1'b0, n})));
    endfunction

    // IN / MOV / WAIT pin view: pin in_base is bit 0
    wire [31:0] pins_rot = rotr(i_gpio, in_base);

    // n = 1..32 bits
    function automatic logic [31:0] low_mask(input logic [5:0] n);
        low_mask = (n >= 6'd32) ? 32'hFFFF_FFFF : ((32'd1 << n) - 32'd1);
    endfunction

    // RP2040 IRQ index: bit 4 = relative, {i[2], (i[1:0] + sm) % 4}; bit 3 is unused on RP2040
    /* verilator lint_off UNUSEDSIGNAL */
    function automatic logic [2:0] irq_index(input logic [4:0] f);
        logic [1:0] lo;
        lo = f[4] ? 2'(f[1:0] + 2'(SM_ID)) : f[1:0];
        irq_index = {f[2], lo};
    endfunction
    /* verilator lint_on UNUSEDSIGNAL */

    // ------------------------------------------------------------------------
    // Execution, FIFOs and pins
    // ------------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            pc <= 5'd0; reg_x <= '0; reg_y <= '0; isr <= '0; osr <= '0;
            isr_count <= 6'd0; osr_count <= 6'd32; delay_cycles <= 5'd0;
            exec_pending <= 1'b0; exec_instr <= 16'd0; exec_forced <= 1'b0;
            push_pending <= 1'b0; pend_delay <= 5'd0; irq_waiting <= 1'b0; stalled <= 1'b0;
            tx_wr_ptr <= '0; tx_rd_ptr <= '0; tx_level <= '0;
            rx_wr_ptr <= '0; rx_rd_ptr <= '0; rx_level <= '0;
            o_out_wmask <= '0; o_out_wval <= '0; o_dir_wmask <= '0; o_dir_wval <= '0;
            o_irq_set <= '0; o_irq_clr <= '0;
            join_q <= 2'd0;
            for (int i = 0; i < 8; i++) fifo_mem[i] <= '0;
        end else if ({join_rx, join_tx} != join_q) begin
            // Changing FJOIN flushes both FIFOs (RP2040; pio_sm_clear_fifos relies on it)
            join_q <= {join_rx, join_tx};
            tx_wr_ptr <= '0; tx_rd_ptr <= '0; tx_level <= '0;
            rx_wr_ptr <= '0; rx_rd_ptr <= '0; rx_level <= '0;
        end else begin
            // FIFO bookkeeping in this cycle (blocking locals)
            logic [3:0]  tx_lvl, rx_lvl;
            logic [2:0]  tx_rp, rx_wp;
            logic        do_pull, do_push;
            logic [31:0] push_word;
            logic        step;

            tx_lvl = tx_level;  rx_lvl = rx_level;
            tx_rp  = tx_rd_ptr; rx_wp  = rx_wr_ptr;
            do_pull = 1'b0; do_push = 1'b0; push_word = '0;

            o_irq_set <= '0;
            o_irq_clr <= '0;
            o_out_wmask <= '0;
            o_dir_wmask <= '0;

            // Host side: RX read frees a slot before the SM looks at the level
            if (i_rx_ren && rx_lvl != 4'd0) begin
                rx_rd_ptr <= rx_rd_ptr + 3'd1;
                rx_lvl = rx_lvl - 4'd1;
            end

            if (i_exec_valid) begin
                exec_pending <= 1'b1;
                exec_instr   <= i_exec_instr;
                exec_forced  <= 1'b1;
            end

            step = (exec_pending && exec_forced) || (i_en && sm_clk_enable);

            if (step && delay_cycles != 5'd0 && !(exec_pending && exec_forced)) begin
                delay_cycles <= delay_cycles - 5'd1;
            end else if (step && push_pending) begin
                // Autopush stalled on a full RX FIFO: retry the push, not the IN
                if (rx_lvl != rx_cap) begin
                    do_push = 1'b1; push_word = isr;
                    isr <= '0; isr_count <= 6'd0;
                    push_pending <= 1'b0;
                    stalled <= 1'b0;
                    delay_cycles <= pend_delay;
                    pc <= (pc == wrap_top) ? wrap_bottom : pc + 5'd1;
                end else begin
                    stalled <= 1'b1;
                end
            end else if (step) begin
                logic [15:0] ins;
                logic [4:0]  next_pc, delay, side_val;
                logic [2:0]  side_bits;
                logic        side_en, stall, from_exec, pc_jump, hold_for_push, is_exec;
                logic        d_en, d_dir;            // one data pin write per instruction
                logic [31:0] d_val;
                logic [5:0]  d_cnt;
                logic [4:0]  d_base;
                logic [31:0] w_mask, w_val, n_out, n_out_en, n_dir, n_dir_en;
                logic [31:0] n_x, n_y, n_isr, n_osr, cur_osr;
                logic [5:0]  n_isr_cnt, n_osr_cnt, cur_osr_cnt;

                from_exec = exec_pending;
                d_en = 1'b0; d_dir = 1'b0; d_val = '0; d_cnt = 6'd0; d_base = 5'd0;
                ins       = exec_pending ? exec_instr : i_instr;
                next_pc   = 5'd0;
                pc_jump   = 1'b0;
                stall     = 1'b0;
                hold_for_push = 1'b0;
                is_exec   = (ins[15:13] == 3'b011 && ins[7:5] == 3'b111) ||
                            (ins[15:13] == 3'b101 && ins[7:5] == 3'b100);

                // Autopull refills an empty OSR in the background ([SPEC-PIO-02] rule 3)
                cur_osr = osr; cur_osr_cnt = osr_count;
                if (autopull && osr_count >= pull_thresh && tx_lvl != 4'd0 &&
                    !(ins[15:13] == 3'b100 && ins[7])) begin  // a PULL takes the word itself
                    do_pull = 1'b1; cur_osr = fifo_mem[tx_idx(tx_rp, join_tx)]; cur_osr_cnt = 6'd0;
                end
                n_x = reg_x; n_y = reg_y; n_isr = isr; n_osr = cur_osr;
                n_isr_cnt = isr_count; n_osr_cnt = cur_osr_cnt;

                // Delay / side-set field: SIDESET_COUNT includes the enable bit when SIDE_EN
                side_bits = sideset_opt ? (sideset_count == 3'd0 ? 3'd0 : sideset_count - 3'd1) : sideset_count;
                side_en   = (sideset_count != 3'd0) && (!sideset_opt || ins[12]);
                side_val  = 5'(ins[12:8] >> (3'd5 - sideset_count)) & 5'(low_mask(6'(side_bits)));
                delay     = ins[12:8] & 5'(low_mask(6'(3'd5 - sideset_count)));

                case (ins[15:13])
                    3'b000: begin  // JMP
                        logic c;
                        case (ins[7:5])
                            3'b000: c = 1'b1;
                            3'b001: c = (reg_x == 32'd0);
                            3'b010: begin c = (reg_x != 32'd0); n_x = reg_x - 32'd1; end
                            3'b011: c = (reg_y == 32'd0);
                            3'b100: begin c = (reg_y != 32'd0); n_y = reg_y - 32'd1; end
                            3'b101: c = (reg_x != reg_y);
                            3'b110: c = i_gpio[jmp_pin];
                            default: c = (cur_osr_cnt < pull_thresh);  // !OSRE
                        endcase
                        if (c) begin pc_jump = 1'b1; next_pc = ins[4:0]; end
                    end

                    3'b001: begin  // WAIT
                        logic v;
                        case (ins[6:5])
                            2'b00:   v = i_gpio[ins[4:0]];
                            2'b01:   v = i_gpio[5'(in_base + ins[4:0])];
                            2'b10:   v = i_irq[irq_index(ins[4:0])];
                            default: v = i_gpio[jmp_pin];
                        endcase
                        if (v != ins[7]) begin
                            stall = 1'b1;
                        end else if (ins[6:5] == 2'b10 && ins[7]) begin
                            o_irq_clr[irq_index(ins[4:0])] <= 1'b1;
                        end
                    end

                    3'b010: begin  // IN
                        logic [5:0]  n;
                        logic [31:0] v;
                        n = (ins[4:0] == 5'd0) ? 6'd32 : {1'b0, ins[4:0]};
                        case (ins[7:5])
                            3'b000:  v = pins_rot;
                            3'b001:  v = reg_x;
                            3'b010:  v = reg_y;
                            3'b110:  v = isr;
                            3'b111:  v = cur_osr;
                            default: v = 32'd0;
                        endcase
                        v = v & low_mask(n);
                        if (n == 6'd32)          n_isr = v;
                        else if (in_shift_right) n_isr = (isr >> n) | (v << (6'd32 - n));
                        else                     n_isr = (isr << n) | v;
                        n_isr_cnt = (isr_count + n > 6'd32) ? 6'd32 : isr_count + n;
                        if (autopush && n_isr_cnt >= push_thresh) begin
                            if (rx_lvl != rx_cap) begin
                                do_push = 1'b1; push_word = n_isr;
                                n_isr = '0; n_isr_cnt = 6'd0;
                            end else begin
                                hold_for_push = 1'b1;  // the shift commits; only the push waits
                            end
                        end
                    end

                    3'b011: begin  // OUT
                        logic [5:0]  n;
                        logic [31:0] v;
                        n = (ins[4:0] == 5'd0) ? 6'd32 : {1'b0, ins[4:0]};
                        if (autopull && cur_osr_cnt >= pull_thresh) begin
                            stall = 1'b1;  // empty OSR and empty FIFO
                        end else begin
                            if (out_shift_right) begin
                                v = cur_osr & low_mask(n);
                                n_osr = (n == 6'd32) ? 32'd0 : (cur_osr >> n);
                            end else begin
                                v = (n == 6'd32) ? cur_osr : (cur_osr >> (6'd32 - n));
                                n_osr = (n == 6'd32) ? 32'd0 : (cur_osr << n);
                            end
                            n_osr_cnt = (cur_osr_cnt + n > 6'd32) ? 6'd32 : cur_osr_cnt + n;
                            case (ins[7:5])
                                3'b000: begin d_en = 1'b1; d_val = v; d_cnt = out_count; d_base = out_base; end
                                3'b001: n_x = v;
                                3'b010: n_y = v;
                                3'b100: begin d_en = 1'b1; d_dir = 1'b1; d_val = v; d_cnt = out_count; d_base = out_base; end
                                3'b101: begin pc_jump = 1'b1; next_pc = v[4:0]; end
                                3'b110: begin n_isr = v; n_isr_cnt = n; end
                                3'b111: begin exec_pending <= 1'b1; exec_instr <= v[15:0]; end
                                default: ;
                            endcase
                        end
                    end

                    3'b100: begin
                        if (!ins[7]) begin  // PUSH: [6] iffull, [5] block
                            if (ins[6] && isr_count < push_thresh) begin
                                // below threshold: no-op
                            end else if (rx_lvl != rx_cap) begin
                                do_push = 1'b1; push_word = isr;
                                n_isr = '0; n_isr_cnt = 6'd0;
                            end else if (ins[5]) begin
                                stall = 1'b1;
                            end else begin
                                n_isr = '0; n_isr_cnt = 6'd0;  // noblock, full: dropped
                            end
                        end else begin      // PULL: [6] ifempty, [5] block
                            if (ins[6] && cur_osr_cnt < pull_thresh) begin
                                // not empty yet: no-op
                            end else if (tx_lvl != 4'd0) begin
                                do_pull = 1'b1; n_osr = fifo_mem[tx_idx(tx_rp, join_tx)]; n_osr_cnt = 6'd0;
                            end else if (ins[5]) begin
                                stall = 1'b1;
                            end else begin
                                n_osr = reg_x; n_osr_cnt = 6'd0;  // noblock, empty: OSR = X
                            end
                        end
                    end

                    3'b101: begin  // MOV
                        logic [31:0] s, r;
                        case (ins[2:0])
                            3'b000:  s = pins_rot;
                            3'b001:  s = reg_x;
                            3'b010:  s = reg_y;
                            3'b101:  s = ((status_sel ? rx_lvl : tx_lvl) < status_n) ? 32'hFFFF_FFFF : 32'd0;
                            3'b110:  s = isr;
                            3'b111:  s = cur_osr;
                            default: s = 32'd0;
                        endcase
                        case (ins[4:3])
                            2'b01:   r = ~s;
                            2'b10:   r = bit_reverse(s);
                            default: r = s;
                        endcase
                        case (ins[7:5])
                            3'b000: begin d_en = 1'b1; d_val = r; d_cnt = out_count; d_base = out_base; end
                            3'b001: n_x = r;
                            3'b010: n_y = r;
                            3'b011: begin d_en = 1'b1; d_dir = 1'b1; d_val = r; d_cnt = out_count; d_base = out_base; end  // PINDIRS (RP2350)
                            3'b100: begin exec_pending <= 1'b1; exec_instr <= r[15:0]; end
                            3'b101: begin pc_jump = 1'b1; next_pc = r[4:0]; end
                            3'b110: begin n_isr = r; n_isr_cnt = 6'd0; end
                            default: begin n_osr = r; n_osr_cnt = 6'd0; end
                        endcase
                    end

                    3'b110: begin  // IRQ: [6] clr, [5] wait, [4:0] index
                        logic [2:0] k;
                        k = irq_index(ins[4:0]);
                        if (ins[6]) begin
                            o_irq_clr[k] <= 1'b1;
                        end else if (!ins[5]) begin
                            o_irq_set[k] <= 1'b1;
                        end else if (!irq_waiting) begin
                            o_irq_set[k] <= 1'b1;   // set, then wait for someone to clear it
                            irq_waiting  <= 1'b1;
                            stall = 1'b1;
                        end else if (i_irq[k]) begin
                            stall = 1'b1;
                        end else begin
                            irq_waiting <= 1'b0;
                        end
                    end

                    default: begin  // SET
                        case (ins[7:5])
                            3'b000: begin d_en = 1'b1; d_val = {27'd0, ins[4:0]}; d_cnt = {3'd0, set_count}; d_base = set_base; end
                            3'b001: n_x = {27'd0, ins[4:0]};
                            3'b010: n_y = {27'd0, ins[4:0]};
                            3'b100: begin d_en = 1'b1; d_dir = 1'b1; d_val = {27'd0, ins[4:0]}; d_cnt = {3'd0, set_count}; d_base = set_base; end
                            default: ;
                        endcase
                    end
                endcase

                // Pin writers: data (only when the instruction completes), then side-set
                // (asserted even while stalled; wins on a shared pin, as on the RP2040)
                n_out = '0; n_out_en = '0; n_dir = '0; n_dir_en = '0;   // values / masks written this clock
                if (d_en && !stall) begin
                    w_mask = rotl(low_mask(d_cnt), d_base) & ((d_cnt == 6'd0) ? 32'd0 : 32'hFFFF_FFFF);
                    w_val  = rotl(d_val, d_base);
                    if (d_dir) begin
                        n_dir = w_val; n_dir_en = w_mask;
                    end else begin
                        n_out = w_val; n_out_en = w_mask;
                    end
                end
                if (side_en && side_bits != 3'd0) begin
                    w_mask = rotl(low_mask(6'(side_bits)), side_base);
                    w_val  = rotl({27'd0, side_val}, side_base);
                    if (sideset_pindirs) begin
                        n_dir = (n_dir & ~w_mask) | (w_val & w_mask); n_dir_en = n_dir_en | w_mask;
                    end else begin
                        n_out = (n_out & ~w_mask) | (w_val & w_mask); n_out_en = n_out_en | w_mask;
                    end
                end
                o_out_wmask <= n_out_en; o_out_wval <= n_out;
                o_dir_wmask <= n_dir_en; o_dir_wval <= n_dir;

                stalled <= stall || hold_for_push;
                if (!stall) begin
                    // Commit: a stalled instruction changes nothing except side-set
                    reg_x <= n_x; reg_y <= n_y; isr <= n_isr; osr <= n_osr;
                    isr_count <= n_isr_cnt; osr_count <= n_osr_cnt;
                    if (from_exec) begin
                        exec_pending <= is_exec;
                        exec_forced  <= 1'b0;
                        if (pc_jump) pc <= next_pc;  // an EXEC'd instruction moves the PC only by jumping
                    end else if (hold_for_push) begin
                        push_pending <= 1'b1;    // PC and delay wait for the push
                        pend_delay   <= delay;
                    end else begin
                        pc <= pc_jump ? next_pc : ((pc == wrap_top) ? wrap_bottom : pc + 5'd1);
                    end
                    if (!is_exec && !hold_for_push) begin
                        delay_cycles <= delay;   // OUT/MOV EXEC ignore their own delay
                    end
                end else if (do_pull) begin
                    osr <= cur_osr; osr_count <= cur_osr_cnt;  // background refill lands even when stalled
                end
            end

            // FIFO commits
            if (do_pull) begin
                tx_rd_ptr <= tx_rp + 3'd1;
                tx_lvl = tx_lvl - 4'd1;
            end
            if (do_push) begin
                fifo_mem[rx_idx(rx_wp, join_rx)] <= push_word;
                rx_wr_ptr <= rx_wp + 3'd1;
                rx_lvl = rx_lvl + 4'd1;
            end
            if (i_tx_wvalid && tx_lvl != tx_cap) begin
                fifo_mem[tx_idx(tx_wr_ptr, join_tx)] <= i_tx_wdata;
                tx_wr_ptr <= tx_wr_ptr + 3'd1;
                tx_lvl = tx_lvl + 4'd1;
            end
            tx_level <= tx_lvl;
            rx_level <= rx_lvl;

            // SM_RESTART ([SPEC-PIO-02] rule 7): PC, OSR, X, Y, FIFOs and pins are kept
            if (i_restart) begin
                isr <= '0; isr_count <= 6'd0; osr_count <= 6'd32;
                delay_cycles <= 5'd0; exec_pending <= 1'b0; exec_forced <= 1'b0;
                push_pending <= 1'b0; irq_waiting <= 1'b0; stalled <= 1'b0;
            end
        end
    end

endmodule

`default_nettype wire
