// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
`default_nettype none
// Control TLPs -> AXI4-Lite ([SPEC-TLP-04]). MemWrite: N posted writes; MemRead: N reads, one CplD.
// Consecutive word addresses from target_address, N = length_dw (0 -> 1, > 9 -> 9). One transaction
// outstanding; every AXI valid is held until its ready; the completion is held until accepted.
// @impl [SPEC-TLP-04] [SPEC-TLP-07] docs/DESIGN_SPECIFICATION.md
module asp_tlp_axil_master
(
    input  wire          clk,
    input  wire          rst_n,

    // CONTROL TLPs from the router
    input  wire  [511:0] s_tlp_tdata,
    input  wire          s_tlp_tvalid,
    output logic         s_tlp_tready,

    // Completions to the router
    output logic [511:0] m_cpl_tdata,
    output logic         m_cpl_tvalid,
    input  wire          m_cpl_tready,

    // AXI4-Lite master
    output logic [31:0]  m_axil_awaddr,
    output logic [2:0]   m_axil_awprot,
    output logic         m_axil_awvalid,
    input  wire          m_axil_awready,
    output logic [31:0]  m_axil_wdata,
    output logic [3:0]   m_axil_wstrb,
    output logic         m_axil_wvalid,
    input  wire          m_axil_wready,
    input  wire  [1:0]   m_axil_bresp,
    input  wire          m_axil_bvalid,
    output logic         m_axil_bready,
    output logic [31:0]  m_axil_araddr,
    output logic [2:0]   m_axil_arprot,
    output logic         m_axil_arvalid,
    input  wire          m_axil_arready,
    input  wire  [31:0]  m_axil_rdata,
    input  wire  [1:0]   m_axil_rresp,
    input  wire          m_axil_rvalid,
    output logic         m_axil_rready,

    output logic         o_wr_err            // one clock per failed write (counted in asp_sys_regs)
);
    localparam int MAXN = 9;

    typedef enum logic [2:0] { S_IDLE, S_WRITE, S_WRESP, S_READ, S_RDATA, S_CPL } state_t;
    state_t state;

    logic [511:0] req;                 // the TLP being served
    logic [3:0]   n, idx;              // words requested, current word
    logic [31:0]  wdata [0:MAXN-1];
    logic [31:0]  rdata [0:MAXN-1];
    logic [1:0]   rd_resp;
    logic         aw_done, w_done;

    wire  [15:0] len_in = asp_tlp_pkg::tlp_len(s_tlp_tdata);
    wire  [3:0]  n_in   = (len_in == 16'd0) ? 4'd1 : (len_in > 16'd9) ? 4'd9 : len_in[3:0];

    assign m_axil_awprot = 3'b000;
    assign m_axil_arprot = 3'b000;
    assign m_axil_wstrb  = 4'hF;
    assign m_axil_bready = (state == S_WRESP);
    assign m_axil_rready = (state == S_RDATA);
    assign s_tlp_tready  = (state == S_IDLE);

    // Completion: words read, length = count, flags[1:0] = response of the failing beat (0 OK)
    function automatic logic [511:0] build_cpl(input logic [511:0] r, input logic [3:0] count,
                                               input logic [1:0] resp);
        logic [511:0] t;
        t = asp_tlp_pkg::tlp_header(asp_tlp_pkg::TLP_CPL_D, {6'd0, resp}, asp_tlp_pkg::tlp_tag(r), asp_tlp_pkg::tlp_channel(r), asp_tlp_pkg::tlp_addr(r),
                       {12'd0, count}, asp_tlp_pkg::tlp_seq(r), asp_tlp_pkg::tlp_ts(r));
        for (int k = 0; k < MAXN; k++)
            if (k < int'(count)) t = asp_tlp_pkg::tlp_set_word(t, k, rdata[k]);
        build_cpl = t;
    endfunction

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state          <= S_IDLE;
            req            <= '0;
            n              <= 4'd0;
            idx            <= 4'd0;
            rd_resp        <= 2'b00;
            aw_done        <= 1'b0;
            w_done         <= 1'b0;
            m_axil_awaddr  <= 32'd0;
            m_axil_awvalid <= 1'b0;
            m_axil_wdata   <= 32'd0;
            m_axil_wvalid  <= 1'b0;
            m_axil_araddr  <= 32'd0;
            m_axil_arvalid <= 1'b0;
            m_cpl_tdata    <= '0;
            m_cpl_tvalid   <= 1'b0;
            o_wr_err       <= 1'b0;
            for (int k = 0; k < MAXN; k++) begin
                wdata[k] <= 32'd0;
                rdata[k] <= 32'd0;
            end
        end else begin
            o_wr_err <= 1'b0;
            unique case (state)
                S_IDLE: if (s_tlp_tvalid) begin
                    req <= s_tlp_tdata;
                    n   <= n_in;
                    idx <= 4'd0;
                    for (int k = 0; k < MAXN; k++) wdata[k] <= asp_tlp_pkg::tlp_word(s_tlp_tdata, k);
                    if (asp_tlp_pkg::tlp_type(s_tlp_tdata) == asp_tlp_pkg::TLP_MEM_WR) begin
                        m_axil_awaddr  <= asp_tlp_pkg::tlp_addr(s_tlp_tdata);
                        m_axil_awvalid <= 1'b1;
                        m_axil_wdata   <= asp_tlp_pkg::tlp_word(s_tlp_tdata, 0);
                        m_axil_wvalid  <= 1'b1;
                        aw_done        <= 1'b0;
                        w_done         <= 1'b0;
                        state          <= S_WRITE;
                    end else if (asp_tlp_pkg::tlp_type(s_tlp_tdata) == asp_tlp_pkg::TLP_MEM_RD) begin
                        m_axil_araddr  <= asp_tlp_pkg::tlp_addr(s_tlp_tdata);
                        m_axil_arvalid <= 1'b1;
                        rd_resp        <= 2'b00;
                        state          <= S_READ;
                    end
                    // any other type: consumed and dropped
                end

                S_WRITE: begin
                    if (m_axil_awvalid && m_axil_awready) begin
                        m_axil_awvalid <= 1'b0;
                        aw_done        <= 1'b1;
                    end
                    if (m_axil_wvalid && m_axil_wready) begin
                        m_axil_wvalid <= 1'b0;
                        w_done        <= 1'b1;
                    end
                    if ((aw_done || (m_axil_awvalid && m_axil_awready)) &&
                        (w_done  || (m_axil_wvalid  && m_axil_wready)))
                        state <= S_WRESP;
                end

                S_WRESP: if (m_axil_bvalid) begin
                    if (m_axil_bresp[1]) begin            // SLVERR / DECERR: stop the burst
                        o_wr_err <= 1'b1;
                        state    <= S_IDLE;
                    end else if (idx + 4'd1 < n) begin
                        idx            <= idx + 4'd1;
                        m_axil_awaddr  <= m_axil_awaddr + 32'd4;
                        m_axil_awvalid <= 1'b1;
                        m_axil_wdata   <= wdata[idx + 4'd1];
                        m_axil_wvalid  <= 1'b1;
                        aw_done        <= 1'b0;
                        w_done         <= 1'b0;
                        state          <= S_WRITE;
                    end else begin
                        state <= S_IDLE;
                    end
                end

                S_READ: if (m_axil_arready) begin
                    m_axil_arvalid <= 1'b0;
                    state          <= S_RDATA;
                end

                S_RDATA: if (m_axil_rvalid) begin
                    if (m_axil_rresp[1]) begin            // error: completion with the words so far
                        rd_resp      <= m_axil_rresp;
                        m_cpl_tdata  <= build_cpl(req, idx, m_axil_rresp);
                        m_cpl_tvalid <= 1'b1;
                        state        <= S_CPL;
                    end else begin
                        rdata[idx] <= m_axil_rdata;
                        if (idx + 4'd1 < n) begin
                            idx            <= idx + 4'd1;
                            m_axil_araddr  <= m_axil_araddr + 32'd4;
                            m_axil_arvalid <= 1'b1;
                            state          <= S_READ;
                        end else begin
                            state <= S_CPL;        // completion built next clock (rdata[idx] lands now)
                        end
                    end
                end

                S_CPL: begin
                    if (!m_cpl_tvalid) begin
                        m_cpl_tdata  <= build_cpl(req, n, 2'b00);
                        m_cpl_tvalid <= 1'b1;
                    end else if (m_cpl_tready) begin
                        m_cpl_tvalid <= 1'b0;
                        state        <= S_IDLE;
                    end
                end

                default: state <= S_IDLE;
            endcase
        end
    end

    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused = &{1'b0, rd_resp};
    /* verilator lint_on UNUSEDSIGNAL */
endmodule
`default_nettype wire
