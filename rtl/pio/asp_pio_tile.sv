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
// @impl [SPEC-PIO-01] [SPEC-PIO-02] [SPEC-PIO-03] [SPEC-PIO-04] [SPEC-PIO-05] [SPEC-PIO-07] [SPEC-PIO-10] rtl/pio/SPECIFICATION.md
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

    // Ingress 512-bit TLPs from asp_router.sv ([SPEC-PIO-10])
    input  wire [511:0]  s_tlp_tdata,
    input  wire          s_tlp_tvalid,
    output logic         s_tlp_tready,

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

    // Instruction memory in LUT RAM, no reset (the host loads it): one write port, one read
    // port per SM plus the bus read, so synthesis maps copies of RAM16SDP4 (EL-1027-41)
    wire        iram_we    = wb_cyc_i && wb_stb_i && !wb_ack_o && wb_we_i &&
                             (wb_adr_i[11:0] - BASE_ADDR[11:0]) >= 12'h010 &&
                             (wb_adr_i[11:0] - BASE_ADDR[11:0]) <= 12'h08C;
    wire [11:0] iram_off   = (wb_adr_i[11:0] - BASE_ADDR[11:0]) - 12'h010;
    wire [4:0]  iram_waddr = iram_off[6:2];
    /* verilator lint_off UNUSEDSIGNAL */
    wire        _unused_iram_off = |{iram_off[11:7], iram_off[1:0]};
    /* verilator lint_on UNUSEDSIGNAL */

    always_ff @(posedge clk) begin
        if (iram_we) iram[iram_waddr] <= wb_dat_i[15:0];
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

    // What the SMs see: a set or clear is visible one clock after the instruction ([SPEC-PIO-02] rule 6)
    logic [7:0] irq_view;
    assign irq_view = (irq_flags | combined_irq_set) & ~combined_irq_clr;

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

    // TLP data path registers ([SPEC-PIO-10])
    logic        tlp_en;
    logic [3:0]  out_mask;
    logic [7:0]  tlp_tag;
    logic [7:0]  tlp_channel;
    logic [3:0]  burst_words;
    logic        trace_en;
    logic [23:0] out_timeout;
    logic        in_en;
    logic [7:0]  in_channel;

    // FIFO Interface wires per State Machine: Wishbone and the TLP path share each FIFO port
    logic [31:0] wb_tx_wdata  [0:3];
    logic [3:0]  wb_tx_wvalid;
    logic [31:0] sm_tx_wdata  [0:3];
    logic [3:0]  sm_tx_wvalid;
    logic [3:0]  sm_tx_ready;

    logic [31:0] sm_rx_rdata  [0:3];
    logic [3:0]  sm_rx_rvalid;
    logic [3:0]  wb_rx_ren;
    logic [3:0]  sm_rx_ren;

    logic [3:0]   in_tx_wvalid;
    logic [127:0] in_tx_wdata;
    logic [3:0]   out_rx_pop;
    logic [5:0]   in_queued;
    logic         in_busy;

    always_comb begin
        for (int s = 0; s < 4; s++) begin
            sm_tx_wvalid[s] = wb_tx_wvalid[s] | in_tx_wvalid[s];
            sm_tx_wdata[s]  = in_tx_wvalid[s] ? in_tx_wdata[32 * s +: 32] : wb_tx_wdata[s];
            sm_rx_ren[s]    = wb_rx_ren[s] | out_rx_pop[s];
        end
    end

    // Pin writes per State Machine (one-clock strobes)
    logic [31:0] sm_out_wmask [0:3];
    logic [31:0] sm_out_wval  [0:3];
    logic [31:0] sm_dir_wmask [0:3];
    logic [31:0] sm_dir_wval  [0:3];

    // Status per State Machine
    logic [3:0]  sm_stalled;
    logic [3:0]  sm_exec_latched;
    logic [3:0]  sm_active;

    // SMx_INSTR: immediate instruction per state machine
    logic [15:0] sm_exec_instr [0:3];
    logic [3:0]  sm_exec_valid;

    // Pin latches, one per pin for the whole tile ([SPEC-PIO-02] rule 10): each clock the write of
    // the highest-numbered SM writing a pin is applied; unwritten pins keep their value
    logic [31:0] out_q, dir_q;
    always_comb begin
        o_gpio_out    = out_q;
        o_gpio_pindir = dir_q;
        for (int s = 0; s < 4; s++) begin
            o_gpio_out    = (o_gpio_out    & ~sm_out_wmask[s]) | (sm_out_wval[s] & sm_out_wmask[s]);
            o_gpio_pindir = (o_gpio_pindir & ~sm_dir_wmask[s]) | (sm_dir_wval[s] & sm_dir_wmask[s]);
        end
    end
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            out_q <= '0;
            dir_q <= '0;                 // every pin an input after reset ([SPEC-PIO-09])
        end else begin
            out_q <= o_gpio_out;
            dir_q <= o_gpio_pindir;
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
                .i_exec_instr     (sm_exec_instr[s]),
                .i_exec_valid     (sm_exec_valid[s]),
                .i_clkdiv         (sm_clkdiv[s]),
                .i_execctrl       (sm_execctrl[s]),
                .i_shiftctrl      (sm_shiftctrl[s]),
                .i_pinctrl        (sm_pinctrl[s]),
                .o_pc             (sm_pc[s]),
                .i_instr          (sm_instr[s]),
                .i_gpio           (i_gpio),
                .o_out_wmask      (sm_out_wmask[s]),
                .o_out_wval       (sm_out_wval[s]),
                .o_dir_wmask      (sm_dir_wmask[s]),
                .o_dir_wval       (sm_dir_wval[s]),
                .i_tx_wdata       (sm_tx_wdata[s]),
                .i_tx_wvalid      (sm_tx_wvalid[s]),
                .o_tx_ready       (sm_tx_ready[s]),
                .o_rx_rdata       (sm_rx_rdata[s]),
                .o_rx_rvalid      (sm_rx_rvalid[s]),
                .i_rx_ren         (sm_rx_ren[s]),
                .i_irq            (irq_view),
                .o_irq_set        (sm_irq_set[s]),
                .o_irq_clr        (sm_irq_clr[s]),
                .o_exec_stalled   (sm_stalled[s]),
                .o_exec_latched   (sm_exec_latched[s]),
                .o_sm_active      (sm_active[s])
            );
        end
    endgenerate

    // ------------------------------------------------------------------------
    // TLP data path ([SPEC-PIO-10]): ingress queue -> TX FIFOs, RX FIFOs -> egress TLPs + trace
    // ------------------------------------------------------------------------
    logic sm_trigger_pulse;

    asp_pio_tlp_ingress u_tlp_ingress (
        .clk          (clk),
        .rst_n        (rst_n),
        .i_en         (in_en),
        .i_channel    (in_channel),
        .s_tlp_tdata  (s_tlp_tdata),
        .s_tlp_tvalid (s_tlp_tvalid),
        .s_tlp_tready (s_tlp_tready),
        .i_tx_ready   (sm_tx_ready),
        .i_tx_block   (wb_tx_wvalid),
        .o_tx_wvalid  (in_tx_wvalid),
        .o_tx_wdata   (in_tx_wdata),
        .o_queued     (in_queued),
        .o_busy       (in_busy)
    );

    asp_pio_tlp_shim #(
        .BASE_ADDR(BASE_ADDR)
    ) u_tlp_shim (
        .clk             (clk),
        .rst_n           (rst_n),
        .i_sys_timestamp (i_sys_timestamp),
        .i_tlp_en        (tlp_en),
        .i_mask          (out_mask),
        .i_tlp_tag       (tlp_tag),
        .i_tlp_channel   (tlp_channel),
        .i_burst_words   (burst_words),
        .i_idle_timeout  (out_timeout),
        .i_trace_en      (trace_en),
        .i_tile_id       (8'(TILE_ID)),
        .i_rx_data       ({sm_rx_rdata[3], sm_rx_rdata[2], sm_rx_rdata[1], sm_rx_rdata[0]}),
        .i_rx_valid      (sm_rx_rvalid),
        .i_rx_block      (wb_rx_ren),
        .o_rx_pop        (out_rx_pop),
        .i_sm_trigger    (sm_trigger_pulse),
        .i_sm_active     (sm_active),
        .i_sm_stalled    (sm_stalled),
        .i_sm_pc         ({sm_pc[3], sm_pc[2], sm_pc[1], sm_pc[0]}),
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
            wb_tx_wvalid        <= 4'd0;
            wb_rx_ren           <= 4'd0;
            sm_exec_valid       <= 4'd0;
            tlp_en              <= 1'b0;
            out_mask            <= 4'd0;
            tlp_tag             <= 8'd0;
            tlp_channel         <= 8'd0;
            burst_words         <= 4'd0;
            trace_en            <= 1'b0;
            out_timeout         <= 24'd0;
            in_en               <= 1'b0;
            in_channel          <= 8'd0;
            sm_trigger_pulse    <= 1'b0;

            for (int s = 0; s < 4; s++) begin
                sm_clkdiv[s]    <= {16'd1, 8'd0}; // Div = 1.0 (full speed)
                sm_execctrl[s]  <= (32'd31 << 12) | (32'd0 << 7); // Wrap bottom=0, top=31
                sm_shiftctrl[s] <= 32'd0;
                sm_pinctrl[s]   <= 32'd0;
                wb_tx_wdata[s]  <= 32'd0;
                sm_exec_instr[s] <= 16'd0;
            end
        end else begin
            wb_ack_o         <= 1'b0;
            wb_irq_clr       <= 8'd0;
            sm_restart       <= 4'd0;
            sm_clkdiv_restart<= 4'd0;
            wb_tx_wvalid     <= 4'd0;
            wb_rx_ren        <= 4'd0;
            sm_exec_valid    <= 4'd0;
            sm_trigger_pulse <= 1'b0;

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
                        12'h00C: begin // OUT_CFG ([SPEC-PIO-10])
                            tlp_en      <= wb_dat_i[0];
                            out_mask    <= wb_dat_i[7:4];
                            tlp_tag     <= wb_dat_i[15:8];
                            tlp_channel <= wb_dat_i[23:16];
                            burst_words <= wb_dat_i[27:24];
                            trace_en    <= wb_dat_i[31];
                        end
                        12'h090: out_timeout <= wb_dat_i[23:0];   // OUT_TIMEOUT
                        12'h094: begin                             // IN_CFG
                            in_en      <= wb_dat_i[0];
                            in_channel <= wb_dat_i[15:8];
                        end
                        default: begin
                            // Instruction Memory (0x010 .. 0x08C)
                            // (instruction memory writes: see the iram block below)

                            // State Machine Registers (0x100 + 0x20 * N)
                            for (int s = 0; s < 4; s++) begin
                                int base_sm;
                                base_sm = 32'h100 + s * 32'h020;
                                if (reg_offset == 12'(base_sm + 12'h00)) begin
                                    sm_clkdiv[s] <= {wb_dat_i[31:16], wb_dat_i[15:8]};  // RP2040 CLKDIV: INT[31:16], FRAC[15:8]
                                end else if (reg_offset == 12'(base_sm + 12'h04)) begin
                                    sm_execctrl[s] <= wb_dat_i;
                                end else if (reg_offset == 12'(base_sm + 12'h08)) begin
                                    sm_shiftctrl[s] <= wb_dat_i;
                                end else if (reg_offset == 12'(base_sm + 12'h0C)) begin
                                    sm_pinctrl[s] <= wb_dat_i;
                                end else if (reg_offset == 12'(base_sm + 12'h10)) begin
                                    // TX FIFO Write
                                    wb_tx_wdata[s]  <= wb_dat_i;
                                    wb_tx_wvalid[s] <= 1'b1;
                                end else if (reg_offset == 12'(base_sm + 12'h18)) begin
                                    // SMx_INSTR: execute immediately ([SPEC-PIO-05])
                                    sm_exec_instr[s] <= wb_dat_i[15:0];
                                    sm_exec_valid[s] <= 1'b1;
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
                        12'h00C: wb_dat_o <= {trace_en, 3'd0, burst_words, tlp_channel, tlp_tag,
                                              out_mask, 3'd0, tlp_en};          // OUT_CFG
                        12'h090: wb_dat_o <= {8'd0, out_timeout};               // OUT_TIMEOUT
                        12'h094: wb_dat_o <= {16'd0, in_channel, 7'd0, in_en};  // IN_CFG
                        12'h098: wb_dat_o <= {23'd0, in_busy, 2'd0, in_queued}; // IN_STATUS
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
                                        wb_dat_o <= {sm_clkdiv[s][23:8], sm_clkdiv[s][7:0], 8'd0};
                                    end else if (reg_offset == 12'(base_sm + 12'h04)) begin
                                        wb_dat_o <= {sm_exec_latched[s], sm_execctrl[s][30:0]};  // EXEC_STALLED [31]
                                    end else if (reg_offset == 12'(base_sm + 12'h08)) begin
                                        wb_dat_o <= sm_shiftctrl[s];
                                    end else if (reg_offset == 12'(base_sm + 12'h0C)) begin
                                        wb_dat_o <= sm_pinctrl[s];
                                    end else if (reg_offset == 12'(base_sm + 12'h14)) begin
                                        // RX FIFO Read
                                        wb_dat_o     <= sm_rx_rdata[s];
                                        wb_rx_ren[s] <= 1'b1;
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
