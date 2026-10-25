// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// AbstractX PIO Autonomous 64-Byte TLP Packetizer & barectf Trace Shim
//
// Functions:
// 1. Ingests 32-bit burst words from a designated PIO State Machine (e.g. SM1 Supervisor).
// 2. Latches 64-bit nanosecond monotonic system timestamp at the start of a burst.
// 3. Formats 64-byte (512-bit) Tlp64 telemetry frames (Type=0x10, Target=0x40000700).
// 4. Autonomous barectf CTF 1.8 Timeline Trace generation (Stream 5).
// 5. Direct AXI-Stream connection to asp_router.sv with tvalid/tready backpressure.
//
// @impl [SPEC-PIO-04] [SPEC-PIO-06] [SPEC-PIO-07] rtl/pio/SPECIFICATION.md
module asp_pio_tlp_shim #(
    parameter logic [31:0] BASE_ADDR = 32'h40000700
) (
    input  wire          clk,
    input  wire          rst_n,

    // Monotonic 64-bit nanosecond system timebase
    input  wire [63:0]   i_sys_timestamp,

    // Configuration from Tile CSRs
    input  wire          i_tlp_en,
    input  wire [7:0]    i_tlp_tag,
    input  wire [7:0]    i_tlp_channel,
    input  wire [2:0]    i_burst_words,      // Number of 32-bit words per burst (1..8 words)
    input  wire          i_trace_en,
    input  wire [7:0]    i_tile_id,

    // PIO State Machine Ingress Stream (typically SM1 Supervisor)
    input  wire [31:0]   i_sm_data,
    input  wire          i_sm_valid,
    output logic         o_sm_ready,
    input  wire          i_sm_trigger,       // Pulse from DRDY edge or start of burst

    // Hardware Event Trace Inputs from State Machines
    input  wire [3:0]    i_sm_active,
    input  wire [3:0]    i_sm_stalled,
    input  wire [4:0]    i_sm_pc [0:3],
    input  wire [7:0]    i_irq_pulse,

    // Egress 512-bit (64-Byte) AXI-Stream to asp_router.sv
    output logic [511:0] m_tlp_tdata,
    output logic         m_tlp_tvalid,
    input  wire          m_tlp_tready
);

    // ------------------------------------------------------------------------
    // Sensor Telemetry Accumulator & Framing
    // ------------------------------------------------------------------------
    logic [15:0] seq_num;
    logic [63:0] latched_timestamp;
    logic [31:0] payload_words [0:7];
    logic [2:0]  word_count;
    logic        burst_active;

    // Buffer holding the 512-bit outgoing packet
    logic [511:0] tx_frame_buf;
    logic         tx_frame_pending;
    logic         fifo_popping;

    assign o_sm_ready = !tx_frame_pending && i_tlp_en && !fifo_popping;

    // Timestamp Latch on Trigger Edge
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            latched_timestamp <= 64'd0;
            fifo_popping      <= 1'b0;
        end else begin
            fifo_popping <= i_tlp_en && i_sm_valid && o_sm_ready;

            if (i_sm_trigger || (!burst_active && i_sm_valid)) begin
                latched_timestamp <= i_sys_timestamp;
            end
        end
    end

    // Word Ingestion & Burst Assembly
    wire [2:0] target_words = (i_burst_words == 3'd0) ? 3'd4 : i_burst_words;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            seq_num          <= 16'd0;
            word_count       <= 3'd0;
            burst_active     <= 1'b0;
            tx_frame_buf     <= 512'd0;
            tx_frame_pending <= 1'b0;
            for (int i = 0; i < 8; i++) begin
                payload_words[i] <= 32'd0;
            end
        end else begin
            // Clear pending flag when router accepts frame
            if (m_tlp_tvalid && m_tlp_tready) begin
                tx_frame_pending <= 1'b0;
            end

            if (i_tlp_en && i_sm_valid && o_sm_ready) begin
                burst_active <= 1'b1;
                payload_words[word_count] <= i_sm_data;

                if (word_count + 3'd1 >= target_words) begin
                    // Assemble 64-byte TLP Frame (512 bits) matching asp_imu_auto_dma / asp_tlp64.hpp layout:
                    // DW0: [511:480] Type=0x10, Flags=0x00, Tag, Channel
                    // DW1: [479:448] Base Address (0x40000700)
                    // DW2: [447:416] Length (4 DWs = 16B), Sequence Number
                    // DW3-DW4: [415:352] Latched 64-bit nanosecond timestamp
                    // DW5-DW8: [351:224] 4 Words payload
                    // DW9-DW14: [223:32] Zero-padding / completion
                    // DW15: [31:0] Footer / CRC32
                    tx_frame_buf <= {
                        8'h10, 8'h00, i_tlp_tag, i_tlp_channel,
                        BASE_ADDR,
                        16'd4, seq_num,
                        latched_timestamp,
                        payload_words[0], payload_words[1], payload_words[2], i_sm_data,
                        64'd0, 128'd0,
                        32'h00000000
                    };
                    tx_frame_pending <= 1'b1;
                    seq_num          <= seq_num + 16'd1;
                    word_count       <= 3'd0;
                    burst_active     <= 1'b0;
                end else begin
                    word_count <= word_count + 3'd1;
                end
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
                            i_sm_pc[3], i_sm_pc[2], i_sm_pc[0]};
    /* verilator lint_on UNUSEDSIGNAL */

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            trace_frame_buf <= 512'd0;
            trace_pending   <= 1'b0;
        end else begin
            if (m_tlp_tvalid && m_tlp_tready && !tx_frame_pending) begin
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
                    {active_sm, i_tile_id, 3'd0, i_sm_pc[active_sm[1:0]], 7'd0, i_sm_stalled[active_sm[1:0]]},
                    320'd0
                };
                trace_pending   <= 1'b1;
            end
        end
    end

    // Priority arbitration: Sensor Telemetry > Trace Packet
    assign m_tlp_tvalid = tx_frame_pending || trace_pending;
    assign m_tlp_tdata  = tx_frame_pending ? tx_frame_buf : trace_frame_buf;

endmodule

`default_nettype wire
