// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype none

// AbstractX IMU SPI Master & Auto-DMA IP Core
//
// Features:
// 1. Hardware SPI Master controller interfacing with external IMU sensor ICs.
// 2. Hardware Interrupt Trigger input (imu_int_i) connected to IMU DRDY pin.
// 3. Sub-microsecond 64-bit nanosecond hardware timestamp capture.
// 4. Autonomous 64-byte TLP generation (Type=0x10, Channel=0x02 TELEMETRY).
// 5. Device Address Parity: Output TLP sets Target Address = 32'h40000100 (Wishbone base address).
// 6. Real-time SPI clocking & MISO bit sampling into TLP payload buffer.
//
// @impl [SPEC-IMU-01] rtl/imu/asp_imu_auto_dma.sv
// @impl [SPEC-TLP-02] rtl/imu/asp_imu_auto_dma.sv
module asp_imu_auto_dma (
    input  wire         clk,
    input  wire         rst_n,

    // Monotonic 64-bit system nanosecond timebase input
    input  wire [63:0]  i_sys_timestamp,

    // External Physical IMU Pins
    output logic        o_imu_sclk,
    output logic        o_imu_cs_n,
    output logic        o_imu_mosi,
    input  wire         i_imu_miso,
    input  wire         i_imu_int, // IMU Data-Ready Interrupt Pin

    // Wishbone Bus Target Interface (Base Address: 0x40000100)
    input  wire         wb_cyc_i,
    input  wire         wb_stb_i,
    input  wire         wb_we_i,
    input  wire [31:0]  wb_adr_i,
    input  wire [31:0]  wb_dat_i,
    output logic [31:0] wb_dat_o,
    output logic        wb_ack_o,

    // Egress 512-bit (64-Byte) TLP Stream Output to Transport Queue
    output logic [511:0] m_imu_stream_tdata,
    output logic         m_imu_stream_tvalid,
    input  wire          m_imu_stream_tready
);

    localparam logic [31:0] IMU_WB_BASE = 32'h40000100;

    // Registers
    logic        auto_dma_en;
    logic        int_polarity;    // 0 = Active Low, 1 = Active High
    logic        direct_spi_trig; // Self-clearing manual SPI trigger
    logic        direct_spi_rw;   // 0 = Read, 1 = Write
    logic [7:0]  burst_addr;      // ICM-42688-P sample window starts at 0x1D
    logic [5:0]  burst_len;       // Byte count to read/write (e.g. 1 to 14 bytes)
    logic [31:0] direct_write_data;
    logic [31:0] direct_read_data;
    logic [31:0] spi_half_period;
    logic [31:0] drdy_overrun_count;
    logic        engine_busy;
    logic        direct_busy;
    logic        direct_done;
    logic        direct_done_clear;
    logic [15:0] sample_count;
    logic [15:0] active_sample_sequence;
    logic [63:0] latched_timestamp;
    logic [63:0] completion_timestamp;

    // Interrupt edge detection
    logic [2:0] imu_int_sync;
    logic       imu_int_trig;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            imu_int_sync <= 3'b000;
        end else begin
            imu_int_sync <= {imu_int_sync[1:0], i_imu_int};
        end
    end

    assign imu_int_trig = int_polarity ? (imu_int_sync[2:1] == 2'b01) : (imu_int_sync[2:1] == 2'b10);

// @impl [SPEC-ZYNQ-04] [SPEC-ZYNQ-IMU-POC-03] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-04
    // Wishbone Register Read/Write Logic
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            auto_dma_en       <= 1'b0;
            int_polarity      <= 1'b1;
            direct_spi_trig   <= 1'b0;
            direct_spi_rw     <= 1'b0;
            burst_addr        <= 8'h1D; // TEMP_DATA1 through GYRO_DATA_Z0
            burst_len         <= 6'd14;
            direct_write_data <= 32'h0;
            spi_half_period   <= 32'd4;
            direct_done_clear <= 1'b0;
            wb_ack_o          <= 1'b0;
            wb_dat_o          <= 32'd0;
        end else begin
            wb_ack_o        <= 1'b0;
            direct_spi_trig <= 1'b0; // Self-clearing trigger pulse
            direct_done_clear <= 1'b0;

            if (wb_cyc_i && wb_stb_i && !wb_ack_o) begin
                wb_ack_o <= 1'b1;
                if (wb_we_i) begin
                    case (wb_adr_i[7:0])
                        8'h00: begin
                            auto_dma_en     <= wb_dat_i[0]; // 1 = Start Auto-DMA, 0 = Stop Auto-DMA
                            direct_spi_trig <= wb_dat_i[1]; // 1 = Pulse single manual SPI transaction
                            int_polarity    <= wb_dat_i[2]; // 1 = Active High DRDY
                            direct_spi_rw   <= wb_dat_i[3]; // 0 = Direct Read, 1 = Direct Write
                        end
                        8'h04: burst_addr        <= wb_dat_i[7:0];
                        8'h08: burst_len         <= wb_dat_i[5:0];
                        8'h0C: direct_write_data <= wb_dat_i;
                        8'h1C: direct_done_clear <= wb_dat_i[1];
                        8'h38: spi_half_period <= (wb_dat_i < 32'd2) ? 32'd2 : wb_dat_i;
                        default: ;
                    endcase
                end else begin
                    case (wb_adr_i[7:0])
                        8'h00: wb_dat_o <= {28'b0, direct_spi_rw, int_polarity, 1'b0, auto_dma_en};
                        8'h04: wb_dat_o <= {24'b0, burst_addr};
                        8'h08: wb_dat_o <= {26'b0, burst_len};
                        8'h0C: wb_dat_o <= direct_write_data;
                        8'h10: wb_dat_o <= direct_read_data;
                        8'h14: wb_dat_o <= latched_timestamp[63:32];
                        8'h18: wb_dat_o <= latched_timestamp[31:0];
                        8'h1C: wb_dat_o <= {
                            28'b0,
                            engine_busy,
                            auto_dma_en,
                            direct_done,
                            direct_busy
                        };
                        8'h20: wb_dat_o <= captured_sensor_data[111:80];
                        8'h24: wb_dat_o <= captured_sensor_data[79:48];
                        8'h28: wb_dat_o <= captured_sensor_data[47:16];
                        8'h2C: wb_dat_o <= {captured_sensor_data[15:0], 16'b0};
                        8'h30: wb_dat_o <= completion_timestamp[63:32];
                        8'h34: wb_dat_o <= completion_timestamp[31:0];
                        8'h38: wb_dat_o <= spi_half_period;
                        8'h3C: wb_dat_o <= drdy_overrun_count;
                        default: wb_dat_o <= 32'h00000000;
                    endcase
                end
            end
        end
    end

    // SPI Master State Machine & Bit Counter
    typedef enum logic [2:0] {
        ST_IMU_IDLE,
        ST_IMU_START_BURST,
        ST_IMU_SEND_CMD,
        ST_IMU_TRANSFER_DATA,
        ST_IMU_BUILD_TLP,
        ST_IMU_EMIT_TLP
    } imu_state_t;

    imu_state_t imu_state;
    assign engine_busy = (imu_state != ST_IMU_IDLE);

    logic         is_direct_trans;
    logic [7:0]   spi_cmd_shift;
    logic [7:0]   direct_write_shift;
    logic [111:0] captured_sensor_data; // Up to 14 Bytes x 8 Bits = 112 Bits
    logic [7:0]   bit_cnt;
    logic [7:0]   target_bit_cnt;
    logic [31:0]  sclk_div;

    wire _unused_ok = &{1'b0, wb_adr_i[31:8], direct_write_data[31:8], 1'b0};

    // Dynamic bit counter limit: (burst_len * 8) - 1
    assign target_bit_cnt = (burst_len > 6'd0) ? (({2'b0, burst_len} << 3) - 8'd1) : 8'd7;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            imu_state           <= ST_IMU_IDLE;
            sample_count        <= 16'd0;
            active_sample_sequence <= 16'd0;
            drdy_overrun_count  <= 32'd0;
            latched_timestamp   <= 64'd0;
            completion_timestamp<= 64'd0;
            direct_read_data    <= 32'd0;
            direct_busy        <= 1'b0;
            direct_done        <= 1'b0;
            is_direct_trans     <= 1'b0;
            o_imu_sclk          <= 1'b0;
            o_imu_cs_n          <= 1'b1;
            o_imu_mosi          <= 1'b0;
            m_imu_stream_tvalid <= 1'b0;
            m_imu_stream_tdata  <= 512'd0;
            captured_sensor_data<= 112'd0;
            spi_cmd_shift       <= 8'h00;
            direct_write_shift  <= 8'h00;
            bit_cnt             <= 8'd0;
            sclk_div            <= 32'd0;
        end else begin
            if (auto_dma_en && imu_int_trig && imu_state != ST_IMU_IDLE) begin
                drdy_overrun_count <= drdy_overrun_count + 32'd1;
                sample_count       <= sample_count + 16'd1;
            end

            if (!auto_dma_en && !is_direct_trans && imu_state != ST_IMU_IDLE) begin
                imu_state           <= ST_IMU_IDLE;
                o_imu_cs_n          <= 1'b1;
                o_imu_sclk          <= 1'b0;
                o_imu_mosi          <= 1'b0;
                m_imu_stream_tvalid <= 1'b0;
            end else begin
            case (imu_state)
                ST_IMU_IDLE: begin
                    o_imu_cs_n <= 1'b1;
                    o_imu_sclk <= 1'b0;
                    o_imu_mosi <= 1'b0;
                    if (direct_done_clear)
                        direct_done <= 1'b0;
                    if (auto_dma_en && imu_int_trig) begin
                        // Mode B: Hardware DRDY Interrupt Auto-DMA Sequence
                        is_direct_trans   <= 1'b0;
                        active_sample_sequence <= sample_count;
                        sample_count      <= sample_count + 16'd1;
                        latched_timestamp <= i_sys_timestamp;
                        spi_cmd_shift     <= burst_addr | 8'h80; // SPI Read Burst Command (MSB=1)
                        imu_state         <= ST_IMU_START_BURST;
                    end else if (direct_spi_trig) begin
                        // Mode A: Direct Host Transparent SPI Passthrough Read/Write
                        is_direct_trans   <= 1'b1;
                        direct_busy       <= 1'b1;
                        direct_done       <= 1'b0;
                        latched_timestamp <= i_sys_timestamp;
                        spi_cmd_shift     <= direct_spi_rw ? (burst_addr & 8'h7F) : (burst_addr | 8'h80);
                        imu_state         <= ST_IMU_START_BURST;
                    end
                end

                ST_IMU_START_BURST: begin
                    o_imu_cs_n         <= 1'b0; // Assert CS low
                    o_imu_mosi         <= spi_cmd_shift[7];
                    bit_cnt            <= 8'd0;
                    sclk_div           <= 32'd0;
                    direct_write_shift <= direct_write_data[7:0];
                    captured_sensor_data <= 112'd0;
                    imu_state          <= ST_IMU_SEND_CMD;
                end

                // Send 8-bit Command Byte over MOSI
                ST_IMU_SEND_CMD: begin
                    if (sclk_div == spi_half_period - 32'd1) begin
                        sclk_div   <= 32'd0;
                        o_imu_sclk <= ~o_imu_sclk;
                        if (o_imu_sclk) begin
                            spi_cmd_shift <= {spi_cmd_shift[6:0], 1'b0};
                            if (bit_cnt == 8'd7) begin
                                bit_cnt   <= 8'd0;
                                if (is_direct_trans && direct_spi_rw) begin
                                    o_imu_mosi         <= direct_write_shift[7];
                                    direct_write_shift <= {direct_write_shift[6:0], 1'b0};
                                end else begin
                                    o_imu_mosi <= 1'b0;
                                end
                                imu_state <= ST_IMU_TRANSFER_DATA;
                            end else begin
                                bit_cnt   <= bit_cnt + 8'd1;
                                o_imu_mosi <= spi_cmd_shift[6];
                            end
                        end
                    end else begin
                        sclk_div <= sclk_div + 32'd1;
                    end
                end

                // Transfer Data Bits over SPI (Dynamic target_bit_cnt)
                ST_IMU_TRANSFER_DATA: begin
                    if (sclk_div == spi_half_period - 32'd1) begin
                        sclk_div   <= 32'd0;
                        o_imu_sclk <= ~o_imu_sclk;
                        if (!o_imu_sclk) begin
                            captured_sensor_data <= {captured_sensor_data[110:0], i_imu_miso};
                        end else begin
                            if (is_direct_trans && direct_spi_rw) begin
                                o_imu_mosi         <= direct_write_shift[7];
                                direct_write_shift <= {direct_write_shift[6:0], 1'b0};
                            end
                            if (bit_cnt == target_bit_cnt) begin
                                o_imu_cs_n       <= 1'b1;
                                direct_read_data <= {24'b0, captured_sensor_data[7:0]};
                                completion_timestamp <= i_sys_timestamp;
                                if (is_direct_trans) begin
                                    direct_busy <= 1'b0;
                                    direct_done <= 1'b1;
                                    imu_state <= ST_IMU_IDLE;
                                end else begin
                                    imu_state <= ST_IMU_BUILD_TLP;
                                end
                            end else begin
                                bit_cnt <= bit_cnt + 8'd1;
                            end
                        end
                    end else begin
                        sclk_div <= sclk_div + 32'd1;
                    end
                end

                // Builds 64-Byte TLP (DW0-DW15)
                ST_IMU_BUILD_TLP: begin
                    m_imu_stream_tdata <= {
                        // DW0: Type=0x10 (DMA_Stream), Flags=0x00, Tag=0x00, Channel=0x02 (TELEMETRY)
                        8'h10, 8'h00, 8'h00, 8'h02,
                        // DW1: Target Address = 0x40000100 (Wishbone Device Base Address Parity!)
                        IMU_WB_BASE,
                        // DW2: Length = 4 DWs (14B payload), Seq = DRDY sequence
                        16'd4, active_sample_sequence,
                        // DW3-DW4: Latched Hardware Timestamp (64-bit)
                        latched_timestamp,
                        // DW5-DW8: Captured 14 Bytes raw IMU payload + 2 Bytes zero-padding
                        captured_sensor_data[111:0], 16'h0000,
                        // DW9-DW10: SPI completion timestamp
                        completion_timestamp,
                        // DW11-DW14: Unused payload zero-padded (16 Bytes)
                        128'd0,
                        // DW15: trusted-internal transport footer (reserved zero)
                        32'h00000000
                    };
                    imu_state <= ST_IMU_EMIT_TLP;
                end

                ST_IMU_EMIT_TLP: begin
                    if (m_imu_stream_tvalid && m_imu_stream_tready) begin
                        m_imu_stream_tvalid <= 1'b0;
                        imu_state           <= ST_IMU_IDLE;
                    end else begin
                        m_imu_stream_tvalid <= 1'b1;
                    end
                end

                default: imu_state <= ST_IMU_IDLE;
            endcase
            end
        end
    end

endmodule

`default_nettype wire
