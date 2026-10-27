// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// PIO tile TLP ingress: queue of TLPs in block RAM, dealt to the SM TX FIFOs in groups.
// @impl [SPEC-PIO-10] rtl/pio/SPECIFICATION.md
module asp_pio_tlp_ingress #(
    parameter int SLOTS = 32            // power of two, <= 32
) (
    input  wire          clk,
    input  wire          rst_n,

    input  wire          i_en,
    input  wire [7:0]    i_channel,

    input  wire [511:0]  s_tlp_tdata,
    input  wire          s_tlp_tvalid,
    output logic         s_tlp_tready,

    // SM TX FIFOs
    input  wire [3:0]    i_tx_ready,
    input  wire [3:0]    i_tx_block,    // Wishbone writes these TX FIFOs this clock
    output logic [3:0]   o_tx_wvalid,
    output logic [127:0] o_tx_wdata,    // {sm3, sm2, sm1, sm0}

    output logic [5:0]   o_queued,
    output logic         o_busy
);
    localparam int SW = $clog2(SLOTS);

    // Slot = 16 words: words 0..9 = payload, word 15 = {mask, len}
    logic [31:0] mem [0:SLOTS*16-1];
    logic [31:0] rdata;
    logic        we;
    logic [SW+3:0] waddr, raddr;
    logic [31:0] wdata;

    always_ff @(posedge clk) begin
        if (we) mem[waddr] <= wdata;
        rdata <= mem[raddr];
    end

    logic [SW:0] wr_ptr, rd_ptr;
    wire  [SW:0] queued = wr_ptr - rd_ptr;
    wire         full   = queued == (SW+1)'(SLOTS);
    assign o_queued = 6'(queued);

    // ---------------- writer: one word per clock straight from the held TLP ----------------
    wire        match   = s_tlp_tvalid && i_en && s_tlp_tdata[487:480] == i_channel;
    wire [3:0]  in_mask = s_tlp_tdata[499:496];
    wire [15:0] in_len  = s_tlp_tdata[447:432];
    wire [3:0]  in_cnt  = (in_len > 16'd10) ? 4'd10 : in_len[3:0];
    wire        in_void = in_mask == 4'd0 || in_cnt == 4'd0;

    logic       wr_active;
    logic [3:0] wr_idx, wr_cnt;

    always_comb begin
        we    = 1'b0;
        waddr = {wr_ptr[SW-1:0], 4'hF};
        wdata = {24'd0, in_mask, in_cnt};
        s_tlp_tready = 1'b1;                       // other channels / disabled: not ours, ignore
        if (match) begin
            s_tlp_tready = 1'b0;
            if (!wr_active) begin
                if (in_void) s_tlp_tready = 1'b1;  // nothing to deliver: consume
                else if (!full) we = 1'b1;         // slot header
            end else begin
                we    = 1'b1;
                waddr = {wr_ptr[SW-1:0], wr_idx};
                wdata = s_tlp_tdata[351 - 32 * int'(wr_idx) -: 32];
                s_tlp_tready = wr_idx == wr_cnt - 4'd1;
            end
        end
    end

    // ---------------- dealer ----------------
    typedef enum logic [2:0] { D_IDLE, D_META, D_SCAN, D_CAP, D_WRITE } dstate_t;
    dstate_t     st;
    logic [3:0]  mask, len, idx;
    logic [2:0]  s;
    logic [1:0]  cur;
    logic [3:0]  pend;
    logic [31:0] stage [0:3];

    wire  [SW-1:0] slot = rd_ptr[SW-1:0];
    wire  fire = st == D_WRITE && (pend & ~i_tx_ready) == 4'd0 && (pend & i_tx_block) == 4'd0;

    always_comb begin
        raddr = (st == D_IDLE) ? {slot, 4'hF} : {slot, idx};
    end

    assign o_tx_wvalid = fire ? pend : 4'd0;
    assign o_tx_wdata  = {stage[3], stage[2], stage[1], stage[0]};
    assign o_busy      = st != D_IDLE;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            wr_ptr    <= '0;
            rd_ptr    <= '0;
            wr_active <= 1'b0;
            wr_idx    <= 4'd0;
            wr_cnt    <= 4'd0;
            st        <= D_IDLE;
            mask      <= 4'd0;
            len       <= 4'd0;
            idx       <= 4'd0;
            s         <= 3'd0;
            cur       <= 2'd0;
            pend      <= 4'd0;
            for (int i = 0; i < 4; i++) stage[i] <= 32'd0;
        end else begin
            if (match && !wr_active && !in_void && !full) begin
                wr_active <= 1'b1;
                wr_idx    <= 4'd0;
                wr_cnt    <= in_cnt;
            end else if (wr_active) begin
                if (!match) begin
                    wr_active <= 1'b0;                 // source withdrew (not AXI-legal): drop the slot
                end else if (s_tlp_tready) begin
                    wr_active <= 1'b0;
                    wr_ptr    <= wr_ptr + 1'b1;
                end else begin
                    wr_idx <= wr_idx + 4'd1;
                end
            end

            case (st)
                D_IDLE: if (queued != '0) st <= D_META;
                D_META: begin
                    mask <= rdata[7:4];
                    len  <= rdata[3:0];
                    idx  <= 4'd0;
                    s    <= 3'd0;
                    pend <= 4'd0;
                    st   <= D_SCAN;
                end
                D_SCAN: begin
                    if (idx == len || s == 3'd4) begin
                        st <= D_WRITE;
                    end else if (mask[s[1:0]]) begin
                        cur <= s[1:0];
                        st  <= D_CAP;
                    end else begin
                        s <= s + 3'd1;
                    end
                end
                D_CAP: begin
                    stage[cur] <= rdata;
                    pend[cur]  <= 1'b1;
                    idx        <= idx + 4'd1;
                    s          <= s + 3'd1;
                    st         <= D_SCAN;
                end
                D_WRITE: if (fire) begin
                    pend <= 4'd0;
                    s    <= 3'd0;
                    if (idx == len) begin
                        rd_ptr <= rd_ptr + 1'b1;
                        st     <= D_IDLE;
                    end else begin
                        st <= D_SCAN;
                    end
                end
                default: st <= D_IDLE;
            endcase
        end
    end
endmodule

`default_nettype wire
