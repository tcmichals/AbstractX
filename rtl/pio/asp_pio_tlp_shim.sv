// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// PIO tile TLP egress: rounds of RX words from the masked SMs framed into 64-byte TLPs, plus
// the barectf Stream 5 trace events.
// @impl [SPEC-PIO-04] [SPEC-PIO-07] [SPEC-PIO-10] rtl/pio/SPECIFICATION.md
module asp_pio_tlp_shim #(
    parameter logic [31:0] BASE_ADDR = 32'h40000700
) (
    input  wire          clk,
    input  wire          rst_n,

    // Monotonic 64-bit nanosecond system timebase
    input  wire [63:0]   i_sys_timestamp,

    // OUT_CFG / OUT_TIMEOUT
    input  wire          i_tlp_en,
    input  wire [3:0]    i_mask,
    input  wire [7:0]    i_tlp_tag,
    input  wire [7:0]    i_tlp_channel,
    input  wire [3:0]    i_burst_words,      // 1..10, 0 = 10
    input  wire [23:0]   i_idle_timeout,     // clocks, 0 = off
    input  wire          i_trace_en,
    input  wire [7:0]    i_tile_id,

    // SM RX FIFOs
    input  wire [127:0]  i_rx_data,          // {sm3, sm2, sm1, sm0}
    input  wire [3:0]    i_rx_valid,
    input  wire [3:0]    i_rx_block,         // Wishbone pops these RX FIFOs this clock
    output logic [3:0]   o_rx_pop,
    input  wire          i_sm_trigger,       // software trigger: trace event only

    // Hardware Event Trace Inputs from State Machines
    input  wire [3:0]    i_sm_active,
    input  wire [3:0]    i_sm_stalled,
    input  wire [19:0]   i_sm_pc,        // {pc3, pc2, pc1, pc0}, 5 bits each
    input  wire [7:0]    i_irq_pulse,

    // Egress 512-bit (64-Byte) AXI-Stream to asp_router.sv
    output logic [511:0] m_tlp_tdata,
    output logic         m_tlp_tvalid,
    input  wire          m_tlp_tready
);

    // ------------------------------------------------------------------------
    // Rounds and framing
    // ------------------------------------------------------------------------
    logic [15:0] seq_num;
    logic [63:0] latched_timestamp;
    logic [31:0] words [0:9];
    logic [3:0]  count;
    logic [23:0] idle_cnt;
    logic         tx_frame_pending;             // words/count frozen until accepted
    logic         show_trace, hold_trace;

    wire [3:0] n      = 4'(i_mask[0]) + 4'(i_mask[1]) + 4'(i_mask[2]) + 4'(i_mask[3]);
    wire [3:0] burst  = (i_burst_words == 4'd0 || i_burst_words > 4'd10) ? 4'd10 : i_burst_words;
    wire [4:0] after  = 5'(count) + 5'(n);
    wire       send   = !tx_frame_pending && count != 4'd0 &&
                        (count >= burst || after > 5'd10 ||
                         (i_idle_timeout != 24'd0 && idle_cnt >= i_idle_timeout));
    wire       round  = i_tlp_en && i_mask != 4'd0 && !send && !tx_frame_pending &&
                        (i_mask & ~i_rx_valid) == 4'd0 && (i_mask & i_rx_block) == 4'd0 &&
                        after <= 5'd10;

    assign o_rx_pop = round ? i_mask : 4'd0;

    // Words above count are zero: all words are cleared when a TLP is accepted
    wire [511:0] tx_frame = {
        8'h10, 4'h0, i_mask, i_tlp_tag, i_tlp_channel,
        BASE_ADDR,
        12'd0, count, seq_num,
        latched_timestamp,
        words[0], words[1], words[2], words[3], words[4],
        words[5], words[6], words[7], words[8], words[9],
        32'h00000000
    };

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            seq_num           <= 16'd0;
            latched_timestamp <= 64'd0;
            count             <= 4'd0;
            idle_cnt          <= 24'd0;
            tx_frame_pending  <= 1'b0;
            for (int i = 0; i < 10; i++) words[i] <= 32'd0;
        end else begin
            if (m_tlp_tvalid && m_tlp_tready && !show_trace) begin
                tx_frame_pending <= 1'b0;
                seq_num          <= seq_num + 16'd1;
                count            <= 4'd0;
                idle_cnt         <= 24'd0;
                for (int i = 0; i < 10; i++) words[i] <= 32'd0;
            end else if (round) begin
                logic [3:0] k;
                k = count;
                for (int sm = 0; sm < 4; sm++) begin
                    if (i_mask[sm]) begin
                        words[k] <= i_rx_data[32 * sm +: 32];
                        k = k + 4'd1;
                    end
                end
                if (count == 4'd0) latched_timestamp <= i_sys_timestamp;
                count    <= after[3:0];
                idle_cnt <= 24'd0;
            end else if (send) begin
                tx_frame_pending <= 1'b1;
            end else if (count != 4'd0 && idle_cnt != 24'hFF_FFFF) begin
                idle_cnt <= idle_cnt + 24'd1;
            end
        end
    end

    // ------------------------------------------------------------------------
    // barectf CTF 1.8 Timeline Trace Generation (Stream 5)
    // ------------------------------------------------------------------------
    logic [511:0] trace_frame_buf;
    logic         trace_pending;

    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused_signals = |{i_sm_active[3:2], i_sm_active[0], i_sm_stalled[3:2], i_sm_stalled[0],
                            i_sm_pc[19:15], i_sm_pc[14:10], i_sm_pc[4:0]};
    /* verilator lint_on UNUSEDSIGNAL */

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            trace_frame_buf <= 512'd0;
            trace_pending   <= 1'b0;
        end else begin
            if (m_tlp_tvalid && m_tlp_tready && show_trace) begin
                trace_pending <= 1'b0;
            end

            // Emit trace packet on IRQ pulse or trigger edge when enabled
            if (i_trace_en && !trace_pending && (i_irq_pulse != 8'd0 || i_sm_trigger)) begin
                logic [7:0]   ev_type;
                logic [7:0]   active_sm;

                ev_type    = i_sm_trigger ? 8'd0 : 8'd1; // 0=Trigger, 1=IRQ Pulse
                active_sm  = i_sm_active[1] ? 8'd1 : 8'd0;

                trace_frame_buf <= {
                    // DW0: barectf magic 0xC1, 0xFC, Stream 5, ev_type
                    8'hC1, 8'hFC, 8'd5, ev_type,
                    BASE_ADDR,
                    16'd4, seq_num,
                    i_sys_timestamp,
                    {active_sm, i_tile_id, 3'd0, i_sm_pc[5*active_sm[1:0] +: 5], 7'd0, i_sm_stalled[active_sm[1:0]]},
                    320'd0
                };
                trace_pending   <= 1'b1;
            end
        end
    end

    // Data before trace, but a presented TLP stays until accepted (AXI-Stream: tdata stable while tvalid)
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) hold_trace <= 1'b0;
        else        hold_trace <= m_tlp_tvalid && !m_tlp_tready && show_trace;
    end
    assign show_trace   = hold_trace || !tx_frame_pending;
    assign m_tlp_tvalid = tx_frame_pending || trace_pending;
    assign m_tlp_tdata  = show_trace ? trace_frame_buf : tx_frame;

endmodule

`default_nettype wire
