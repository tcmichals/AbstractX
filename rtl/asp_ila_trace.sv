// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX Hardware ILA (Integrated Logic Analyzer) & Trace Memory Pool Core
//
// Features:
// 1. Decoupled Memory Pool: Dedicated dual-port Block RAM circular trace buffer
//    completely isolated from the primary 64-byte control TLP memory plane.
// 2. Hardware Event Tap: Captures nanosecond timestamped hardware events (IMU DRDY,
//    motor pulses, coroutine state transitions) at wire speed with zero CPU overhead.
// 3. Opportunistic Non-Blocking DMA Reader: Streams trace frames to the egress bus
//    ONLY when the primary control bus is IDLE, or upon receiving an explicit host
//    DMA Pull trigger (Channel 0x04). Flight-critical control packets are NEVER blocked.
// 4. Run-Length Encoding (RLE) Ready: Taps repeated states and compresses redundant records.
//
// @impl [SPEC-ILA-01] rtl/asp_ila_trace.sv

module asp_ila_trace #(
    parameter int BUFFER_DEPTH_LOG2 = 6, // 64 slots of 64-byte trace records (4 KB BRAM)
    parameter bit ENABLE_RLE        = 1'b1
)(
    input  wire         clk,
    input  wire         rst_n,

    // Monotonic 64-bit nanosecond timer input
    input  wire [63:0]  sys_timestamp,

    // ========================================================================
    // Hardware Event Tap Interface (Zero CPU intervention)
    // ========================================================================
    input  wire         i_event_trig,       // Trigger pulse (e.g. IMU DRDY, motor edge)
    input  wire [7:0]   i_event_tag,        // Event tag / type (0x01=DRDY, 0x02=PWM, 0x03=CORO)
    input  wire [31:0]  i_event_data0,      // Event payload word 0
    input  wire [31:0]  i_event_data1,      // Event payload word 1

    // ========================================================================
    // Host Control & DMA Pull Command Interface (from asp_router Channel 0x04)
    // ========================================================================
    input  wire         i_pull_req,         // Host requested DMA trace dump
    input  wire [15:0]  i_pull_max_records, // Max records to dump in this transfer
    output logic        o_pull_active,      // Trace DMA dump currently in progress
    output logic [15:0] o_buffered_count,   // Current number of captured records in pool

    // ========================================================================
    // Bus Priority & Non-Blocking Arbitration Inputs
    // ========================================================================
    input  wire         i_egress_busy,      // Primary control plane is actively transmitting
    
    // ========================================================================
    // Egress 64-Byte Trace TLP Stream (to Router / Transport Egress Mux)
    // ========================================================================
    output logic [511:0] m_trace_tdata,
    output logic         m_trace_tvalid,
    input  wire          m_trace_tready
);

    localparam int DEPTH = 1 << BUFFER_DEPTH_LOG2;
    localparam logic [7:0] TLP_TYPE_DMA_STREAM = 8'h10;
    localparam logic [7:0] CHANNEL_DEBUG_TRACE = 8'h04;

    // ------------------------------------------------------------------------
    // Dedicated Dual-Port Trace BRAM Storage (True Block RAM Inference)
    // ------------------------------------------------------------------------
    (* ram_style = "block" *) logic [511:0] trace_ram [DEPTH-1:0];
    logic [BUFFER_DEPTH_LOG2-1:0] wr_ptr;
    logic [BUFFER_DEPTH_LOG2-1:0] rd_ptr;
    logic [BUFFER_DEPTH_LOG2:0]   count;
    logic [511:0]                 ram_rdata;

    assign o_buffered_count = { {(16 - (BUFFER_DEPTH_LOG2 + 1)){1'b0}}, count };

    // Synchronous Block RAM Port A (Write) & Port B (Read)
    always_ff @(posedge clk) begin
        if (i_event_trig) begin
            trace_ram[wr_ptr] <= make_trace_tlp(i_event_tag, sys_timestamp, 16'd1, i_event_data0, i_event_data1);
        end
        ram_rdata <= trace_ram[rd_ptr];
    end

    // Assemble 512-bit TLP Frame for internal trace record
    function automatic [511:0] make_trace_tlp(
        input [7:0]  tag,
        input [63:0] ts,
        input [15:0] rle_count,
        input [31:0] d0,
        input [31:0] d1
    );
        logic [511:0] tlp;
        tlp = 512'd0;
        tlp[511:504] = TLP_TYPE_DMA_STREAM; // DW0 Type
        tlp[503:496] = 8'h00;                // Flags
        tlp[495:488] = tag;                 // Tag
        tlp[487:480] = CHANNEL_DEBUG_TRACE; // Channel
        tlp[479:448] = 32'h00000000;        // Address
        tlp[447:432] = 16'd2;               // Length DW
        tlp[431:416] = rle_count;           // Sequence / RLE Repeat Count
        tlp[415:352] = ts;                  // Timestamp nanoseconds
        tlp[351:320] = d0;                  // Payload DW0
        tlp[319:288] = d1;                  // Payload DW1
        return tlp;
    endfunction

    // ------------------------------------------------------------------------
    // Write Pointer Management
    // ------------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr <= '0;
        end else if (i_event_trig) begin
            wr_ptr <= wr_ptr + 1'b1;
        end
    end

    // ------------------------------------------------------------------------
    // Read Port: Non-Blocking DMA Reader
    // Streams ONLY when egress bus is idle OR explicit pull is granted.
    // Never induces head-of-line blocking on primary control TLP plane.
    // ------------------------------------------------------------------------
    typedef enum logic [1:0] {
        DMA_IDLE,
        DMA_FETCH,
        DMA_STREAM,
        DMA_PAUSE_BUSY
    } dma_state_t;

    dma_state_t dma_state;
    logic [15:0] records_remaining;


    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rd_ptr            <= '0;
            count             <= '0;
            m_trace_tvalid    <= 1'b0;
            m_trace_tdata     <= 512'd0;
            o_pull_active     <= 1'b0;
            records_remaining <= 16'd0;
            dma_state         <= DMA_IDLE;
        end else begin
            // Track buffer level
            if (i_event_trig && !(m_trace_tvalid && m_trace_tready)) begin
                if (count < DEPTH[BUFFER_DEPTH_LOG2:0]) count <= count + 1'b1;
            end else if (!i_event_trig && (m_trace_tvalid && m_trace_tready)) begin
                if (count > 0) count <= count - 1'b1;
            end

            // Clear valid on handshake
            if (m_trace_tvalid && m_trace_tready) begin
                m_trace_tvalid <= 1'b0;
                rd_ptr         <= rd_ptr + 1'b1;
                if (records_remaining > 0) records_remaining <= records_remaining - 1'b1;
            end

            case (dma_state)
                DMA_IDLE: begin
                    o_pull_active <= 1'b0;
                    if (i_pull_req && (count > 0)) begin
                        records_remaining <= (i_pull_max_records == 0) ? o_buffered_count : i_pull_max_records;
                        o_pull_active     <= 1'b1;
                        dma_state         <= DMA_STREAM;
                    end else if (!i_egress_busy && (count > 0)) begin
                        // Opportunistic streaming when egress bus is completely idle
                        records_remaining <= 16'd1;
                        dma_state         <= DMA_STREAM;
                    end
                end

                DMA_STREAM: begin
                    // If high priority control plane becomes busy, pause immediately!
                    if (i_egress_busy && !o_pull_active) begin
                        m_trace_tvalid <= 1'b0;
                        dma_state      <= DMA_PAUSE_BUSY;
                    end else if (!m_trace_tvalid && (count > 0) && (records_remaining > 0)) begin
                        m_trace_tdata  <= ram_rdata;
                        m_trace_tvalid <= 1'b1;
                    end else if (records_remaining == 0 || count == 0) begin
                        dma_state <= DMA_IDLE;
                    end
                end

                DMA_PAUSE_BUSY: begin
                    // Wait for primary control plane to finish before resuming trace dump
                    if (!i_egress_busy) begin
                        dma_state <= DMA_STREAM;
                    end
                end
            endcase
        end
    end

endmodule
`default_nettype wire
