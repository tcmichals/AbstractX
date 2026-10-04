// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX Native 64-Byte TLP AXI4 DMA Engine
//
// Directly bridges PS DDR3 Memory (via Zynq S_AXI_HP0) to the 512-bit (64-byte) ASP switch fabric.
// Replaces heavy, bloated AMD AXI DMA IP with a zero-descriptor, hardware SPSC ring engine.
//
// Features:
// 1. AXI4-Lite Slave on M_AXI_GP0: SPSC Ring Base Addresses, Capacities, Head/Tail Pointers, and Doorbells.
// 2. AXI4 Master on S_AXI_HP0: Fixed 8-beat 64-bit bursts (AWLEN=7, AWSIZE=3) = exactly one 64-byte TLP.
// 3. Egress-to-DDR (RX) Engine: Streams incoming 64B TLPs from asp_router directly into PS DDR.
// 4. DDR-to-Ingress (TX) Engine: Fetches 64B TLPs from PS DDR and pushes into asp_router.
// 5. Zero Descriptors: Fully hardware circular ring buffer with atomic pointer updates.
//
// @impl [SPEC-ZYNQ-01] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
// @impl [SPEC-ZYNQ-02] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
// @impl [SPEC-ZYNQ-03] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-03
module asp_axi_dma #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 16,
    parameter integer C_M_AXI_DATA_WIDTH = 64,
    parameter integer C_M_AXI_ADDR_WIDTH = 32
)(
    input  wire                              clk,
    input  wire                              rst_n,

    // ========================================================================
    // AXI4-Lite Slave: CSR Interface (from PS M_AXI_GP0)
    // ========================================================================
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     s_axi_awaddr,
    input  wire [2:0]                        s_axi_awprot,
    input  wire                              s_axi_awvalid,
    output logic                             s_axi_awready,

    input  wire [C_S_AXI_DATA_WIDTH-1:0]     s_axi_wdata,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0] s_axi_wstrb,
    input  wire                              s_axi_wvalid,
    output logic                             s_axi_wready,

    output logic [1:0]                       s_axi_bresp,
    output logic                             s_axi_bvalid,
    input  wire                              s_axi_bready,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     s_axi_araddr,
    input  wire [2:0]                        s_axi_arprot,
    input  wire                              s_axi_arvalid,
    output logic                             s_axi_arready,

    output logic [C_S_AXI_DATA_WIDTH-1:0]    s_axi_rdata,
    output logic [1:0]                       s_axi_rresp,
    output logic                             s_axi_rvalid,
    input  wire                              s_axi_rready,

    // ========================================================================
    // AXI4 Master: Direct DDR3 Access (to PS S_AXI_HP0)
    // ========================================================================
    // Write Address Channel (RX: FPGA -> DDR)
    output logic [C_M_AXI_ADDR_WIDTH-1:0]    m_axi_awaddr,
    output logic [7:0]                       m_axi_awlen,
    output logic [2:0]                       m_axi_awsize,
    output logic [1:0]                       m_axi_awburst,
    output logic                             m_axi_awvalid,
    input  wire                              m_axi_awready,

    // Write Data Channel
    output logic [C_M_AXI_DATA_WIDTH-1:0]    m_axi_wdata,
    output logic [(C_M_AXI_DATA_WIDTH/8)-1:0]m_axi_wstrb,
    output logic                             m_axi_wlast,
    output logic                             m_axi_wvalid,
    input  wire                              m_axi_wready,

    // Write Response Channel
    input  wire  [1:0]                       m_axi_bresp,
    input  wire                              m_axi_bvalid,
    output logic                             m_axi_bready,

    // Read Address Channel (TX: DDR -> FPGA)
    output logic [C_M_AXI_ADDR_WIDTH-1:0]    m_axi_araddr,
    output logic [7:0]                       m_axi_arlen,
    output logic [2:0]                       m_axi_arsize,
    output logic [1:0]                       m_axi_arburst,
    output logic                             m_axi_arvalid,
    input  wire                              m_axi_arready,

    // Read Data Channel
    input  wire  [C_M_AXI_DATA_WIDTH-1:0]    m_axi_rdata,
    input  wire  [1:0]                       m_axi_rresp,
    input  wire                              m_axi_rlast,
    input  wire                              m_axi_rvalid,
    output logic                             m_axi_rready,

    // Interrupt Line to PS GIC (IRQ_F2P[0])
    output logic                             irq_f2p,

    // ========================================================================
    // AbstractX 512-Bit Switch Fabric Interfaces
    // ========================================================================
    // Ingress Stream to asp_router (from DDR TX DMA)
    output logic [511:0]                     m_tlp_tdata,
    output logic                             m_tlp_tvalid,
    input  wire                              m_tlp_tready,

    // Egress Stream from asp_router (to DDR RX DMA)
    input  wire  [511:0]                     s_egr_tdata,
    input  wire                              s_egr_tvalid,
    output logic                             s_egr_tready
);

    // Constant Register Offsets
    localparam [7:0] REG_CONTROL      = 8'h00;
    localparam [7:0] REG_STATUS       = 8'h04;
    localparam [7:0] REG_IRQ_STATUS   = 8'h08;
    localparam [7:0] REG_IRQ_ENABLE   = 8'h0C;
    localparam [7:0] REG_RX_BASE      = 8'h10;
    localparam [7:0] REG_RX_CAPACITY  = 8'h14;
    localparam [7:0] REG_RX_HEAD      = 8'h18;
    localparam [7:0] REG_RX_TAIL      = 8'h1C;
    localparam [7:0] REG_TX_BASE      = 8'h20;
    localparam [7:0] REG_TX_CAPACITY  = 8'h24;
    localparam [7:0] REG_TX_HEAD      = 8'h28;
    localparam [7:0] REG_TX_TAIL      = 8'h2C;
    localparam [7:0] REG_TX_DOORBELL  = 8'h30;
    localparam [7:0] REG_HARDWARE_ID  = 8'h40;

    localparam [31:0] HARDWARE_MAGIC  = 32'h41535036; // "ASP6"

    // Control Registers
    logic [31:0] reg_control;
    logic [31:0] reg_irq_enable;
    logic [31:0] reg_irq_status;

    logic [31:0] reg_rx_base;
    logic [31:0] reg_rx_capacity;
    logic [31:0] reg_rx_head;
    logic [31:0] reg_rx_tail;

    logic [31:0] reg_tx_base;
    logic [31:0] reg_tx_capacity;
    logic [31:0] reg_tx_head;
    logic [31:0] reg_tx_tail;

    logic        tx_doorbell_pulse;

    // SPSC Ring Full / Empty Calculations
    wire [31:0] rx_next_tail = (reg_rx_tail + 1 == reg_rx_capacity) ? 32'd0 : (reg_rx_tail + 1);
    wire        rx_ring_full = (rx_next_tail == reg_rx_head);

    wire        tx_ring_has_data = (reg_tx_head != reg_tx_tail);
    wire [31:0] tx_next_head = (reg_tx_head + 1 == reg_tx_capacity) ? 32'd0 : (reg_tx_head + 1);

    // Interrupt Generation: Assert IRQ_F2P when RX has pending unread packets
    assign irq_f2p = reg_irq_enable[0] && (reg_rx_tail != reg_rx_head);

    // ========================================================================
    // 1. AXI4-Lite Slave CSR Interface Implementation
    // ========================================================================
    logic [C_S_AXI_ADDR_WIDTH-1:0] axi_awaddr_latched;
    logic [C_S_AXI_ADDR_WIDTH-1:0] axi_araddr_latched;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            s_axi_awready     <= 1'b0;
            s_axi_wready      <= 1'b0;
            s_axi_bvalid      <= 1'b0;
            s_axi_bresp       <= 2'b00;
            s_axi_arready     <= 1'b0;
            s_axi_rvalid      <= 1'b0;
            s_axi_rresp       <= 2'b00;
            s_axi_rdata       <= 32'd0;
            reg_control       <= 32'd0;
            reg_irq_enable    <= 32'd0;
            reg_irq_status    <= 32'd0;
            reg_rx_base       <= 32'd0;
            reg_rx_capacity   <= 32'd256;
            reg_rx_head       <= 32'd0;
            reg_tx_base       <= 32'd0;
            reg_tx_capacity   <= 32'd256;
            reg_tx_tail       <= 32'd0;
            tx_doorbell_pulse <= 1'b0;
        end else begin
            tx_doorbell_pulse <= 1'b0;

            // Write Address Handshake
            if (~s_axi_awready && s_axi_awvalid && s_axi_wvalid) begin
                s_axi_awready      <= 1'b1;
                s_axi_wready       <= 1'b1;
                axi_awaddr_latched <= s_axi_awaddr;
            end else begin
                s_axi_awready      <= 1'b0;
                s_axi_wready       <= 1'b0;
            end

            // Write Data & Execution
            if (s_axi_awready && s_axi_wready) begin
                s_axi_bvalid <= 1'b1;
                s_axi_bresp  <= 2'b00;

                case (axi_awaddr_latched[7:0])
                    REG_CONTROL:     reg_control     <= s_axi_wdata;
                    REG_IRQ_STATUS:  reg_irq_status  <= reg_irq_status & ~s_axi_wdata;
                    REG_IRQ_ENABLE:  reg_irq_enable  <= s_axi_wdata;
                    REG_RX_BASE:     reg_rx_base     <= {s_axi_wdata[31:6], 6'b000000};
                    REG_RX_CAPACITY: reg_rx_capacity <= s_axi_wdata;
                    REG_RX_HEAD:     reg_rx_head     <= s_axi_wdata;
                    REG_TX_BASE:     reg_tx_base     <= {s_axi_wdata[31:6], 6'b000000};
                    REG_TX_CAPACITY: reg_tx_capacity <= s_axi_wdata;
                    REG_TX_TAIL:     reg_tx_tail     <= s_axi_wdata;
                    REG_TX_DOORBELL: tx_doorbell_pulse <= 1'b1;
                    default: ;
                endcase
            end else if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0;
            end

            // Read Address Handshake
            if (~s_axi_arready && s_axi_arvalid) begin
                s_axi_arready      <= 1'b1;
                axi_araddr_latched <= s_axi_araddr;
            end else begin
                s_axi_arready      <= 1'b0;
            end

            // Read Data Response
            if (s_axi_arready && s_axi_arvalid && ~s_axi_rvalid) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp  <= 2'b00;

                case (axi_araddr_latched[7:0])
                    REG_CONTROL:     s_axi_rdata <= reg_control;
                    REG_STATUS:      s_axi_rdata <= {28'd0, tx_ring_has_data, (reg_rx_tail != reg_rx_head), tx_dma_busy, rx_dma_busy};
                    REG_IRQ_STATUS:  s_axi_rdata <= reg_irq_status;
                    REG_IRQ_ENABLE:  s_axi_rdata <= reg_irq_enable;
                    REG_RX_BASE:     s_axi_rdata <= reg_rx_base;
                    REG_RX_CAPACITY: s_axi_rdata <= reg_rx_capacity;
                    REG_RX_HEAD:     s_axi_rdata <= reg_rx_head;
                    REG_RX_TAIL:     s_axi_rdata <= reg_rx_tail;
                    REG_TX_BASE:     s_axi_rdata <= reg_tx_base;
                    REG_TX_CAPACITY: s_axi_rdata <= reg_tx_capacity;
                    REG_TX_HEAD:     s_axi_rdata <= reg_tx_head;
                    REG_TX_TAIL:     s_axi_rdata <= reg_tx_tail;
                    REG_HARDWARE_ID: s_axi_rdata <= HARDWARE_MAGIC;
                    default:         s_axi_rdata <= 32'd0;
                endcase
            end else if (s_axi_rvalid && s_axi_rready) begin
                s_axi_rvalid <= 1'b0;
            end
        end
    end

    // ========================================================================
    // 2. RX DMA Engine: FPGA Egress (asp_router) -> PS DDR3 Ring
    // ========================================================================
    typedef enum logic [2:0] {
        RX_IDLE,
        RX_AW_ADDR,
        RX_W_BURST,
        RX_B_RESP
    } rx_state_t;

    rx_state_t rx_state;
    logic [511:0] rx_tlp_buf;
    logic [2:0]   rx_beat_cnt;
    logic         rx_dma_busy;

    assign m_axi_awlen   = 8'd7;       // 8 beats
    assign m_axi_awsize  = 3'b011;     // 8 bytes (64-bit)
    assign m_axi_awburst = 2'b01;      // INCR burst
    assign m_axi_wstrb   = 8'hFF;      // All 8 bytes valid

    // Acknowledge incoming TLP from router when idle and ring has room
    assign s_egr_tready = (rx_state == RX_IDLE) && reg_control[0] && !rx_ring_full;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rx_state      <= RX_IDLE;
            m_axi_awvalid <= 1'b0;
            m_axi_awaddr  <= 32'd0;
            m_axi_wvalid  <= 1'b0;
            m_axi_wdata   <= 64'd0;
            m_axi_wlast   <= 1'b0;
            m_axi_bready  <= 1'b0;
            reg_rx_tail   <= 32'd0;
            rx_beat_cnt   <= 3'd0;
            rx_dma_busy   <= 1'b0;
            rx_tlp_buf    <= 512'd0;
        end else begin
            case (rx_state)
                RX_IDLE: begin
                    rx_dma_busy   <= 1'b0;
                    m_axi_awvalid <= 1'b0;
                    m_axi_wvalid  <= 1'b0;
                    m_axi_bready  <= 1'b0;
                    rx_beat_cnt   <= 3'd0;

                    if (s_egr_tvalid && s_egr_tready) begin
                        rx_tlp_buf    <= s_egr_tdata;
                        m_axi_awaddr  <= reg_rx_base + (reg_rx_tail << 6); // tail * 64 bytes
                        m_axi_awvalid <= 1'b1;
                        rx_dma_busy   <= 1'b1;
                        rx_state      <= RX_AW_ADDR;
                    end
                end

                RX_AW_ADDR: begin
                    if (m_axi_awready) begin
                        m_axi_awvalid <= 1'b0;
                        m_axi_wvalid  <= 1'b1;
                        m_axi_wdata   <= rx_tlp_buf[511:448]; // Beat 0: DW0 and DW1
                        m_axi_wlast   <= 1'b0;
                        rx_beat_cnt   <= 3'd1;
                        rx_state      <= RX_W_BURST;
                    end
                end

                RX_W_BURST: begin
                    if (m_axi_wready) begin
                        m_axi_wdata <= rx_tlp_buf[511 - (rx_beat_cnt*64) -: 64];
                        if (rx_beat_cnt == 3'd7) begin
                            m_axi_wlast  <= 1'b1;
                            rx_state     <= RX_B_RESP;
                            m_axi_bready <= 1'b1;
                        end else begin
                            rx_beat_cnt <= rx_beat_cnt + 1;
                        end
                    end
                end

                RX_B_RESP: begin
                    if (m_axi_wready && m_axi_wlast) begin
                        m_axi_wvalid <= 1'b0;
                        m_axi_wlast  <= 1'b0;
                    end

                    if (m_axi_bvalid && m_axi_bready) begin
                        m_axi_bready <= 1'b0;
                        reg_rx_tail  <= rx_next_tail;
                        reg_irq_status[0] <= 1'b1;
                        rx_state     <= RX_IDLE;
                    end
                end

                default: rx_state <= RX_IDLE;
            endcase
        end
    end

    // ========================================================================
    // 3. TX DMA Engine: PS DDR3 Ring -> FPGA Ingress (asp_router)
    // ========================================================================
    typedef enum logic [2:0] {
        TX_IDLE,
        TX_AR_ADDR,
        TX_R_BURST,
        TX_PUSH_ROUTER
    } tx_state_t;

    tx_state_t tx_state;
    logic [511:0] tx_tlp_buf;
    logic [2:0]   tx_beat_cnt;
    logic         tx_dma_busy;

    assign m_axi_arlen   = 8'd7;       // 8 beats
    assign m_axi_arsize  = 3'b011;     // 8 bytes (64-bit)
    assign m_axi_arburst = 2'b01;      // INCR burst

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_state      <= TX_IDLE;
            m_axi_arvalid <= 1'b0;
            m_axi_araddr  <= 32'd0;
            m_axi_rready  <= 1'b0;
            m_tlp_tvalid  <= 1'b0;
            m_tlp_tdata   <= 512'd0;
            reg_tx_head   <= 32'd0;
            tx_beat_cnt   <= 3'd0;
            tx_dma_busy   <= 1'b0;
            tx_tlp_buf    <= 512'd0;
        end else begin
            case (tx_state)
                TX_IDLE: begin
                    tx_dma_busy   <= 1'b0;
                    m_axi_arvalid <= 1'b0;
                    m_axi_rready  <= 1'b0;
                    tx_beat_cnt   <= 3'd0;

                    // Fetch if host pushed data to TX ring
                    if (reg_control[0] && tx_ring_has_data) begin
                        m_axi_araddr  <= reg_tx_base + (reg_tx_head << 6); // head * 64 bytes
                        m_axi_arvalid <= 1'b1;
                        tx_dma_busy   <= 1'b1;
                        tx_state      <= TX_AR_ADDR;
                    end
                end

                TX_AR_ADDR: begin
                    if (m_axi_arready) begin
                        m_axi_arvalid <= 1'b0;
                        m_axi_rready  <= 1'b1;
                        tx_beat_cnt   <= 3'd0;
                        tx_state      <= TX_R_BURST;
                    end
                end

                TX_R_BURST: begin
                    if (m_axi_rvalid && m_axi_rready) begin
                        tx_tlp_buf[511 - (tx_beat_cnt*64) -: 64] <= m_axi_rdata;
                        if (m_axi_rlast || (tx_beat_cnt == 3'd7)) begin
                            m_axi_rready <= 1'b0;
                            m_tlp_tvalid <= 1'b1;
                            m_tlp_tdata  <= {tx_tlp_buf[511:64], m_axi_rdata};
                            tx_state     <= TX_PUSH_ROUTER;
                        end else begin
                            tx_beat_cnt  <= tx_beat_cnt + 1;
                        end
                    end
                end

                TX_PUSH_ROUTER: begin
                    if (m_tlp_tready) begin
                        m_tlp_tvalid <= 1'b0;
                        reg_tx_head  <= tx_next_head;
                        tx_state     <= TX_IDLE;
                    end
                end

                default: tx_state <= TX_IDLE;
            endcase
        end
    end

endmodule
`default_nettype wire
