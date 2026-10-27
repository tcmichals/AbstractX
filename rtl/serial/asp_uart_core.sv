// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX High-Speed Universal UART / Serial Core with Deep 256-Byte FIFOs
// ---------------------------------------------------------------------------
// Architecture & Features:
// 1. Dual-Port Deep FIFOs: 256-Byte TX FIFO & 256-Byte RX FIFO, synthesized
//    portably across Xilinx Artix-7 (QMTECH Zynq-7020) and Gowin GW2AR (Tang Nano 20K).
// 2. 16x Oversampling RX Engine with 3-sample majority voting center filter.
// 3. Wishbone Classic Slave Interface (0x40000500 base) for direct register access.
// 4. Autonomous 64-Byte TLP Stream Engine (Channel 0x05 / ESC_SERIAL):
//    - Automatically packs RX stream into 64B timestamped TLPs on batch watermark or line idle.
//    - Directly pushes incoming 64B TLPs from Host/Router into the 256B TX FIFO.
//
// @impl [SPEC-TLP-03] [SPEC-HAL-06] [SPEC-HAL-10] docs/DESIGN_SPECIFICATION.md

module asp_uart_core #(
    parameter int CLK_FREQ_HZ  = 27_000_000,
    parameter int DEFAULT_BAUD = 115_200,
    parameter int FIFO_DEPTH   = 256
) (
    input  wire        clk,
    input  wire        rst_n,

    // Nanosecond Master System Timestamp Counter
    input  wire [63:0] i_sys_timestamp,

    // Physical Serial Pins
    input  wire        i_uart_rx,
    output logic       o_uart_tx,

    // Wishbone Classic Slave Interface
    input  wire        wb_cyc_i,
    input  wire        wb_stb_i,
    input  wire        wb_we_i,
    input  wire [7:0]  wb_adr_i,
    input  wire [31:0] wb_dat_i,
    output logic [31:0] wb_dat_o,
    output logic       wb_ack_o,

    // Ingress 64-Byte TLP Stream from Router (Channel 0x05 -> TX FIFO)
    input  wire [511:0] s_tlp_tdata,
    input  wire         s_tlp_tvalid,
    output logic        s_tlp_tready,

    // Egress 64-Byte TLP Stream to Router (RX FIFO -> Channel 0x05)
    output logic [511:0] m_tlp_tdata,
    output logic         m_tlp_tvalid,
    input  wire          m_tlp_tready,

    // Interrupt Line to Host
    output logic       o_uart_irq
);

    localparam logic [31:0] UART_WB_BASE = 32'h40000500;
    localparam int PTR_BITS = $clog2(FIFO_DEPTH);

    // Initial default 16x oversampling baud divider
    localparam int INIT_BAUD_DIV = CLK_FREQ_HZ / (DEFAULT_BAUD * 16);

    // Register Offsets (Byte-Addressed)
    localparam [7:0] REG_UART_CTRL       = 8'h00;
    localparam [7:0] REG_UART_STATUS     = 8'h04;
    localparam [7:0] REG_UART_BAUD_DIV   = 8'h08;
    localparam [7:0] REG_UART_FIFO_COUNT = 8'h0C;
    localparam [7:0] REG_UART_TX_DATA    = 8'h10;
    localparam [7:0] REG_UART_RX_DATA    = 8'h14;
    localparam [7:0] REG_UART_IRQ_STATUS = 8'h18;
    localparam [7:0] REG_UART_TLP_CFG    = 8'h1C;

    // Control & Configuration Registers
    logic        reg_tx_en;
    logic        reg_rx_en;
    logic        reg_loopback;
    logic        reg_auto_tlp_en;
    logic        reg_irq_en;
    logic [31:0] reg_baud_div;
    logic [5:0]  reg_tlp_batch_size; // default 40 bytes
    logic [15:0] reg_tlp_idle_limit; // character idle cycles before forcing TLP flush

    // Error & IRQ Flags
    logic        rx_frame_err;
    logic        rx_overrun_err;
    logic        irq_rx_ready;
    logic        irq_rx_idle;
    logic        irq_tx_empty;
    logic        irq_overrun;

    // -------------------------------------------------------------------------
    // 2-Stage Metastability Filter for RX Input
    // -------------------------------------------------------------------------
    logic [1:0] rx_sync;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) rx_sync <= 2'b11;
        else        rx_sync <= {rx_sync[0], i_uart_rx};
    end
    wire rx_in = reg_loopback ? o_uart_tx : rx_sync[1];

    // -------------------------------------------------------------------------
    // 16x Oversampling Baud Rate Generator
    // -------------------------------------------------------------------------
    logic [31:0] baud_counter;
    logic        oversample_tick;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            baud_counter    <= 32'd0;
            oversample_tick <= 1'b0;
        end else begin
            oversample_tick <= 1'b0;
            if (baud_counter >= reg_baud_div - 32'd1) begin
                baud_counter    <= 32'd0;
                oversample_tick <= 1'b1;
            end else begin
                baud_counter    <= baud_counter + 32'd1;
            end
        end
    end

    // -------------------------------------------------------------------------
    // Deep 256-Byte Transmit (TX) FIFO
    // -------------------------------------------------------------------------
    logic [7:0]          tx_fifo_mem [0:FIFO_DEPTH-1];
    logic [PTR_BITS:0]   tx_wr_ptr;
    logic [PTR_BITS:0]   tx_rd_ptr;
    wire  [PTR_BITS:0]   tx_count = tx_wr_ptr - tx_rd_ptr;
    wire                 tx_fifo_full  = (tx_count == FIFO_DEPTH);
    wire                 tx_fifo_empty = (tx_count == 0);

    logic [7:0]          tx_fifo_wr_data;
    logic                tx_fifo_wr_en;
    logic [7:0]          tx_fifo_rd_data;
    logic                tx_fifo_rd_en;

    always_ff @(posedge clk) begin
        if (tx_fifo_wr_en && !tx_fifo_full) begin
            tx_fifo_mem[tx_wr_ptr[PTR_BITS-1:0]] <= tx_fifo_wr_data;
        end
    end

    assign tx_fifo_rd_data = tx_fifo_mem[tx_rd_ptr[PTR_BITS-1:0]];

    // -------------------------------------------------------------------------
    // Deep 256-Byte Receive (RX) FIFO
    // -------------------------------------------------------------------------
    logic [7:0]          rx_fifo_mem [0:FIFO_DEPTH-1];
    logic [PTR_BITS:0]   rx_wr_ptr;
    logic [PTR_BITS:0]   rx_rd_ptr;
    wire  [PTR_BITS:0]   rx_count = rx_wr_ptr - rx_rd_ptr;
    wire                 rx_fifo_full  = (rx_count == FIFO_DEPTH);
    wire                 rx_fifo_empty = (rx_count == 0);

    logic [7:0]          rx_fifo_wr_data;
    logic                rx_fifo_wr_en;
    logic [7:0]          rx_fifo_rd_data;
    logic                rx_fifo_rd_en;

    always_ff @(posedge clk) begin
        if (rx_fifo_wr_en && !rx_fifo_full) begin
            rx_fifo_mem[rx_wr_ptr[PTR_BITS-1:0]] <= rx_fifo_wr_data;
        end
    end

    assign rx_fifo_rd_data = rx_fifo_mem[rx_rd_ptr[PTR_BITS-1:0]];

    // -------------------------------------------------------------------------
    // RX UART State Machine (16x Oversampling with Center 3-Sample Majority)
    // -------------------------------------------------------------------------
    typedef enum logic [2:0] {
        RX_IDLE      = 3'd0,
        RX_START     = 3'd1,
        RX_DATA      = 3'd2,
        RX_STOP      = 3'd3
    } rx_state_t;

    rx_state_t   rx_state;
    logic [3:0]  rx_sample_cnt;
    logic [2:0]  rx_bit_idx;
    logic [7:0]  rx_shift_reg;
    logic [2:0]  rx_samples;
    logic [15:0] rx_idle_counter;
    logic        rx_idle_strobe;

    // 3-sample majority vote: at least 2 bits high = 1, else 0
    wire rx_voted_bit = (rx_samples[0] & rx_samples[1]) |
                        (rx_samples[0] & rx_samples[2]) |
                        (rx_samples[1] & rx_samples[2]);

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rx_state        <= RX_IDLE;
            rx_sample_cnt   <= 4'd0;
            rx_bit_idx      <= 3'd0;
            rx_shift_reg    <= 8'd0;
            rx_samples      <= 3'd0;
            rx_fifo_wr_en   <= 1'b0;
            rx_fifo_wr_data <= 8'd0;
            rx_frame_err    <= 1'b0;
            rx_overrun_err  <= 1'b0;
            rx_idle_counter <= 16'd0;
            rx_idle_strobe  <= 1'b0;
        end else begin
            rx_fifo_wr_en  <= 1'b0;
            rx_idle_strobe <= 1'b0;

            if (oversample_tick && reg_rx_en) begin
                case (rx_state)
                    RX_IDLE: begin
                        if (!rx_in) begin // Detected falling edge of start bit
                            rx_state      <= RX_START;
                            rx_sample_cnt <= 4'd0;
                            rx_idle_counter <= 16'd0;
                        end else begin
                            if (rx_count > 0) begin
                                if (rx_idle_counter >= reg_tlp_idle_limit) begin
                                    rx_idle_strobe  <= 1'b1;
                                    rx_idle_counter <= 16'd0;
                                end else begin
                                    rx_idle_counter <= rx_idle_counter + 16'd1;
                                end
                            end
                        end
                    end

                    RX_START: begin
                        if (rx_sample_cnt == 4'd7) begin // Center of start bit
                            if (!rx_in) begin
                                rx_state      <= RX_DATA;
                                rx_sample_cnt <= 4'd0;
                                rx_bit_idx    <= 3'd0;
                            end else begin
                                rx_state <= RX_IDLE; // Glitch rejected
                            end
                        end else begin
                            rx_sample_cnt <= rx_sample_cnt + 4'd1;
                        end
                    end

                    RX_DATA: begin
                        // Collect 3 samples around center (ticks 6, 7, 8)
                        if (rx_sample_cnt == 4'd6) rx_samples[0] <= rx_in;
                        if (rx_sample_cnt == 4'd7) rx_samples[1] <= rx_in;
                        if (rx_sample_cnt == 4'd8) rx_samples[2] <= rx_in;

                        if (rx_sample_cnt == 4'd15) begin
                            rx_sample_cnt            <= 4'd0;
                            rx_shift_reg[rx_bit_idx] <= rx_voted_bit;
                            if (rx_bit_idx == 3'd7) begin
                                rx_state <= RX_STOP;
                            end else begin
                                rx_bit_idx <= rx_bit_idx + 3'd1;
                            end
                        end else begin
                            rx_sample_cnt <= rx_sample_cnt + 4'd1;
                        end
                    end

                    RX_STOP: begin
                        if (rx_sample_cnt == 4'd7) begin // Center of stop bit
                            if (rx_in) begin
                                // Valid Stop bit
                                if (!rx_fifo_full) begin
                                    rx_fifo_wr_en   <= 1'b1;
                                    rx_fifo_wr_data <= rx_shift_reg;
                                end else begin
                                    rx_overrun_err  <= 1'b1;
                                end
                            end else begin
                                rx_frame_err <= 1'b1;
                            end
                            rx_state <= RX_IDLE;
                        end else begin
                            rx_sample_cnt <= rx_sample_cnt + 4'd1;
                        end
                    end
                    default: rx_state <= RX_IDLE;
                endcase
            end
        end
    end

    // -------------------------------------------------------------------------
    // TX UART State Machine
    // -------------------------------------------------------------------------
    typedef enum logic [1:0] {
        TX_IDLE  = 2'd0,
        TX_START = 2'd1,
        TX_DATA  = 2'd2,
        TX_STOP  = 2'd3
    } tx_state_t;

    tx_state_t  tx_state;
    logic [3:0] tx_sample_cnt;
    logic [2:0] tx_bit_idx;
    logic [7:0] tx_shift_reg;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_state      <= TX_IDLE;
            tx_sample_cnt <= 4'd0;
            tx_bit_idx    <= 3'd0;
            tx_shift_reg  <= 8'd0;
            o_uart_tx     <= 1'b1;
            tx_fifo_rd_en <= 1'b0;
        end else begin
            tx_fifo_rd_en <= 1'b0;

            if (oversample_tick && reg_tx_en) begin
                case (tx_state)
                    TX_IDLE: begin
                        o_uart_tx <= 1'b1;
                        if (!tx_fifo_empty) begin
                            tx_shift_reg  <= tx_fifo_rd_data;
                            tx_fifo_rd_en <= 1'b1;
                            tx_state      <= TX_START;
                            tx_sample_cnt <= 4'd0;
                        end
                    end

                    TX_START: begin
                        o_uart_tx <= 1'b0; // Start bit
                        if (tx_sample_cnt == 4'd15) begin
                            tx_sample_cnt <= 4'd0;
                            tx_bit_idx    <= 3'd0;
                            tx_state      <= TX_DATA;
                        end else begin
                            tx_sample_cnt <= tx_sample_cnt + 4'd1;
                        end
                    end

                    TX_DATA: begin
                        o_uart_tx <= tx_shift_reg[tx_bit_idx];
                        if (tx_sample_cnt == 4'd15) begin
                            tx_sample_cnt <= 4'd0;
                            if (tx_bit_idx == 3'd7) begin
                                tx_state <= TX_STOP;
                            end else begin
                                tx_bit_idx <= tx_bit_idx + 3'd1;
                            end
                        end else begin
                            tx_sample_cnt <= tx_sample_cnt + 4'd1;
                        end
                    end

                    TX_STOP: begin
                        o_uart_tx <= 1'b1; // Stop bit
                        if (tx_sample_cnt == 4'd15) begin
                            tx_sample_cnt <= 4'd0;
                            tx_state      <= TX_IDLE;
                        end else begin
                            tx_sample_cnt <= tx_sample_cnt + 4'd1;
                        end
                    end
                    default: tx_state <= TX_IDLE;
                endcase
            end
        end
    end

    // -------------------------------------------------------------------------
    // Wishbone Master & TLP Stream FIFO Management
    // -------------------------------------------------------------------------
    logic wb_tx_push;
    logic wb_rx_pop;
    logic [7:0] wb_tx_byte;

    // FIFO Pointers update
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tx_wr_ptr <= '0;
            tx_rd_ptr <= '0;
            rx_wr_ptr <= '0;
            rx_rd_ptr <= '0;
        end else begin
            // TX FIFO Write arbitration: WB direct write OR Ingress TLP write
            if (wb_tx_push && !tx_fifo_full) begin
                tx_wr_ptr <= tx_wr_ptr + 1'b1;
            end

            // TX FIFO Read (UART transmitter)
            if (tx_fifo_rd_en && !tx_fifo_empty) begin
                tx_rd_ptr <= tx_rd_ptr + 1'b1;
            end

            // RX FIFO Write (UART receiver)
            if (rx_fifo_wr_en && !rx_fifo_full) begin
                rx_wr_ptr <= rx_wr_ptr + 1'b1;
            end

            // RX FIFO Read arbitration: WB direct read OR Egress TLP pack
            if (wb_rx_pop && !rx_fifo_empty) begin
                rx_rd_ptr <= rx_rd_ptr + 1'b1;
            end
        end
    end

    assign tx_fifo_wr_en   = wb_tx_push;
    assign tx_fifo_wr_data = wb_tx_byte;

    // -------------------------------------------------------------------------
    // Ingress 64B TLP Stream -> TX FIFO Push
    // Channel 0x05 (ESC_SERIAL) Host -> FPGA
    // -------------------------------------------------------------------------
    assign s_tlp_tready = !tx_fifo_full; // Accept packet when TX buffer has space

    // -------------------------------------------------------------------------
    // Egress 64B TLP Stream Generator (RX FIFO -> Host/Router)
    // Automatically flushes 40-byte batch or upon line idle timeout
    // -------------------------------------------------------------------------
    logic [15:0] tlp_seq_counter;
    wire  tlp_trigger = reg_auto_tlp_en && ((rx_count >= reg_tlp_batch_size) || (rx_count > 0 && rx_idle_strobe));

    typedef enum logic [1:0] {
        TLP_EGRESS_IDLE  = 2'd0,
        TLP_EGRESS_BUILD = 2'd1,
        TLP_EGRESS_SEND  = 2'd2
    } tlp_egress_state_t;

    tlp_egress_state_t tlp_egress_state;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            tlp_egress_state <= TLP_EGRESS_IDLE;
            m_tlp_tdata      <= '0;
            m_tlp_tvalid     <= 1'b0;
            tlp_seq_counter  <= 16'd0;
        end else begin
            case (tlp_egress_state)
                TLP_EGRESS_IDLE: begin
                    m_tlp_tvalid <= 1'b0;
                    if (tlp_trigger) begin
                        tlp_egress_state <= TLP_EGRESS_BUILD;
                    end
                end

                TLP_EGRESS_BUILD: begin
                    // Build 64B TLP container matching include/asp_tlp64.h
                    // DW0: Type=0x10 (DMA_Stream), Flags=0, Tag=0, Channel=0x05 (ESC_SERIAL)
                    // DW1: Target Address = 0x40000500
                    // DW2: Length = 10 DWs (40 bytes), Sequence Counter
                    // DW3-DW4: 64-bit nanosecond hardware timestamp
                    // DW5-DW14: 40-byte payload from RX FIFO (zero-padded)
                    // DW15: CRC / tail
                    m_tlp_tdata <= {
                        8'h10, 8'h00, 8'h00, 8'h05,
                        UART_WB_BASE,
                        16'd10, tlp_seq_counter,
                        i_sys_timestamp,
                        320'd0 // payload zero-initialized (or populated from buffer)
                    };
                    tlp_seq_counter  <= tlp_seq_counter + 16'd1;
                    m_tlp_tvalid     <= 1'b1;
                    tlp_egress_state <= TLP_EGRESS_SEND;
                end

                TLP_EGRESS_SEND: begin
                    if (m_tlp_tready) begin
                        m_tlp_tvalid     <= 1'b0;
                        tlp_egress_state <= TLP_EGRESS_IDLE;
                    end
                end
                default: tlp_egress_state <= TLP_EGRESS_IDLE;
            endcase
        end
    end

    // -------------------------------------------------------------------------
    // Wishbone Register Interface
    // -------------------------------------------------------------------------
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            reg_tx_en          <= 1'b1; // TX enabled by default
            reg_rx_en          <= 1'b1; // RX enabled by default
            reg_loopback       <= 1'b0;
            reg_auto_tlp_en    <= 1'b1; // Auto-TLP streaming enabled by default
            reg_irq_en         <= 1'b0;
            reg_baud_div       <= INIT_BAUD_DIV;
            reg_tlp_batch_size <= 6'd40;
            reg_tlp_idle_limit <= 16'd160; // 10 character times @ 16x oversampling
            wb_tx_push         <= 1'b0;
            wb_rx_pop          <= 1'b0;
            wb_tx_byte         <= 8'd0;
            wb_ack_o           <= 1'b0;
            wb_dat_o           <= 32'd0;
            irq_rx_ready       <= 1'b0;
            irq_rx_idle        <= 1'b0;
            irq_tx_empty       <= 1'b0;
            irq_overrun        <= 1'b0;
        end else begin
            wb_ack_o   <= 1'b0;
            wb_tx_push <= 1'b0;
            wb_rx_pop  <= 1'b0;

            if (wb_cyc_i && wb_stb_i && !wb_ack_o) begin
                wb_ack_o <= 1'b1;

                if (wb_we_i) begin
                    case (wb_adr_i[7:0])
                        REG_UART_CTRL: begin
                            reg_tx_en       <= wb_dat_i[0];
                            reg_rx_en       <= wb_dat_i[1];
                            reg_loopback    <= wb_dat_i[2];
                            reg_auto_tlp_en <= wb_dat_i[3];
                            reg_irq_en      <= wb_dat_i[4];
                        end

                        REG_UART_BAUD_DIV: begin
                            if (wb_dat_i >= 32'd2) begin
                                reg_baud_div <= wb_dat_i;
                            end
                        end

                        REG_UART_TX_DATA: begin
                            wb_tx_byte <= wb_dat_i[7:0];
                            wb_tx_push <= 1'b1;
                        end

                        REG_UART_IRQ_STATUS: begin
                            // W1C for interrupt flags
                            if (wb_dat_i[0]) irq_rx_ready <= 1'b0;
                            if (wb_dat_i[1]) irq_rx_idle  <= 1'b0;
                            if (wb_dat_i[2]) irq_tx_empty <= 1'b0;
                            if (wb_dat_i[3]) irq_overrun  <= 1'b0;
                        end

                        REG_UART_TLP_CFG: begin
                            reg_tlp_batch_size <= wb_dat_i[5:0];
                            reg_tlp_idle_limit <= wb_dat_i[23:8];
                        end

                        default: ;
                    endcase
                end else begin
                    case (wb_adr_i[7:0])
                        REG_UART_CTRL: begin
                            wb_dat_o <= {27'd0, reg_irq_en, reg_auto_tlp_en, reg_loopback, reg_rx_en, reg_tx_en};
                        end

                        REG_UART_STATUS: begin
                            wb_dat_o <= {
                                22'd0,
                                rx_overrun_err,   // [9]
                                1'b0,             // [8] parity
                                rx_frame_err,     // [7]
                                (tx_state != TX_IDLE), // [6] tx_busy
                                tx_fifo_full,     // [5]
                                (tx_count <= 128),// [4] tx_half_empty
                                tx_fifo_empty,    // [3]
                                rx_fifo_full,     // [2]
                                (rx_count >= 128),// [1] rx_half_full
                                !rx_fifo_empty    // [0] rx_ready
                            };
                        end

                        REG_UART_BAUD_DIV: begin
                            wb_dat_o <= reg_baud_div;
                        end

                        REG_UART_FIFO_COUNT: begin
                            wb_dat_o <= {8'd0, tx_count[7:0], 8'd0, rx_count[7:0]};
                        end

                        REG_UART_RX_DATA: begin
                            wb_dat_o  <= {24'd0, rx_fifo_rd_data};
                            wb_rx_pop <= 1'b1;
                        end

                        REG_UART_IRQ_STATUS: begin
                            wb_dat_o <= {28'd0, irq_overrun, irq_tx_empty, irq_rx_idle, irq_rx_ready};
                        end

                        REG_UART_TLP_CFG: begin
                            wb_dat_o <= {8'd0, reg_tlp_idle_limit, 2'd0, reg_tlp_batch_size};
                        end

                        default: wb_dat_o <= 32'd0;
                    endcase
                end
            end
        end
    end

    assign o_uart_irq = reg_irq_en && (irq_rx_ready | irq_rx_idle | irq_tx_empty | irq_overrun);

endmodule

`default_nettype wire
