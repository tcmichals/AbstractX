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
    logic [7:0]  burst_addr;      // IMU SPI start read register (e.g. 0x1F)
    logic [5:0]  burst_len;       // Byte count to read/write (e.g. 1 to 14 bytes)
    logic [31:0] direct_write_data;
    logic [31:0] direct_read_data;
    logic [15:0] sample_count;
    logic [63:0] latched_timestamp;

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

    // Wishbone Register Read/Write Logic
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            auto_dma_en       <= 1'b0;
            int_polarity      <= 1'b1;
            direct_spi_trig   <= 1'b0;
            direct_spi_rw     <= 1'b0;
            burst_addr        <= 8'h1F; // Default ICM-42688 Accel X1 start register
            burst_len         <= 6'd14; // Default 14 bytes (Accel + Gyro + Temp)
            direct_write_data <= 32'h0;
            wb_ack_o          <= 1'b0;
            wb_dat_o          <= 32'd0;
        end else begin
            wb_ack_o        <= 1'b0;
            direct_spi_trig <= 1'b0; // Self-clearing trigger pulse

            if (wb_cyc_i && wb_stb_i && !wb_ack_o) begin
                wb_ack_o <= 1'b1;
                if (wb_we_i) begin
                    case (wb_adr_i)
                        32'h40000100: begin
                            auto_dma_en     <= wb_dat_i[0];
                            direct_spi_trig <= wb_dat_i[1];
                            int_polarity    <= wb_dat_i[2];
                            direct_spi_rw   <= wb_dat_i[3];
                        end
                        32'h40000104: burst_addr        <= wb_dat_i[7:0];
                        32'h40000108: burst_len         <= wb_dat_i[5:0];
                        32'h4000010C: direct_write_data <= wb_dat_i;
                        default: ;
                    endcase
                end else begin
                    case (wb_adr_i)
                        32'h40000100: wb_dat_o <= {28'b0, direct_spi_rw, int_polarity, 1'b0, auto_dma_en};
                        32'h40000104: wb_dat_o <= {24'b0, burst_addr};
                        32'h40000108: wb_dat_o <= {26'b0, burst_len};
                        32'h4000010C: wb_dat_o <= direct_write_data;
                        32'h40000110: wb_dat_o <= direct_read_data;
                        32'h40000114: wb_dat_o <= latched_timestamp[63:32];
                        32'h40000118: wb_dat_o <= latched_timestamp[31:0];
                        default:      wb_dat_o <= 32'h00000000;
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
        ST_IMU_READ_DATA,
        ST_IMU_BUILD_TLP,
        ST_IMU_EMIT_TLP
    } imu_state_t;

    imu_state_t imu_state;

    logic [7:0]   spi_cmd_shift;
    logic [111:0] captured_sensor_data; // Up to 14 Bytes x 8 Bits = 112 Bits
    logic [7:0]   bit_cnt;
    logic [7:0]   target_bit_cnt;
    logic [2:0]   sclk_div;

    // Dynamic bit counter limit: (burst_len * 8) - 1
    assign target_bit_cnt = (burst_len > 6'd0) ? (({2'b0, burst_len} << 3) - 8'd1) : 8'd7;

    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            imu_state           <= ST_IMU_IDLE;
            sample_count        <= 16'd0;
            latched_timestamp   <= 64'd0;
            direct_read_data    <= 32'd0;
            o_imu_sclk          <= 1'b0;
            o_imu_cs_n          <= 1'b1;
            o_imu_mosi          <= 1'b0;
            m_imu_stream_tvalid <= 1'b0;
            m_imu_stream_tdata  <= 512'd0;
            captured_sensor_data<= 112'd0;
            spi_cmd_shift       <= 8'h00;
            bit_cnt             <= 8'd0;
            sclk_div            <= 3'd0;
        end else begin

            case (imu_state)
                ST_IMU_IDLE: begin
                    o_imu_cs_n <= 1'b1;
                    o_imu_sclk <= 1'b0;
                    if (auto_dma_en && imu_int_trig) begin
                        // Mode B: Hardware DRDY Interrupt Auto-DMA Sequence Replay
                        latched_timestamp <= i_sys_timestamp;
                        spi_cmd_shift     <= burst_addr | 8'h80; // SPI Read Command
                        imu_state         <= ST_IMU_START_BURST;
                    end else if (direct_spi_trig) begin
                        // Mode A: Direct Host Transparent SPI Passthrough Read/Write
                        latched_timestamp <= i_sys_timestamp;
                        spi_cmd_shift     <= direct_spi_rw ? (burst_addr & 8'h7F) : (burst_addr | 8'h80);
                        imu_state         <= ST_IMU_START_BURST;
                    end
                end

                ST_IMU_START_BURST: begin
                    o_imu_cs_n <= 1'b0; // Assert CS low
                    bit_cnt    <= 8'd0;
                    sclk_div   <= 3'd0;
                    imu_state  <= ST_IMU_SEND_CMD;
                end

                // Send 8-bit Command Byte over MOSI
                ST_IMU_SEND_CMD: begin
                    sclk_div <= sclk_div + 3'd1;
                    if (sclk_div == 3'd3) begin
                        o_imu_sclk <= 1'b1;
                        o_imu_mosi <= spi_cmd_shift[7];
                    end else if (sclk_div == 3'd7) begin
                        o_imu_sclk    <= 1'b0;
                        spi_cmd_shift <= {spi_cmd_shift[6:0], 1'b0};
                        if (bit_cnt == 8'd7) begin
                            bit_cnt   <= 8'd0;
                            imu_state <= ST_IMU_READ_DATA;
                        end else begin
                            bit_cnt   <= bit_cnt + 8'd1;
                        end
                    end
                end

                // Read / Write Data Bits over SPI (Dynamic target_bit_cnt)
                ST_IMU_READ_DATA: begin
                    sclk_div <= sclk_div + 3'd1;
                    if (sclk_div == 3'd3) begin
                        o_imu_sclk <= 1'b1;
                        captured_sensor_data <= {captured_sensor_data[110:0], i_imu_miso};
                    end else if (sclk_div == 3'd7) begin
                        o_imu_sclk <= 1'b0;
                        if (bit_cnt == target_bit_cnt) begin
                            o_imu_cs_n       <= 1'b1; // Deassert CS
                            sample_count     <= sample_count + 16'd1;
                            direct_read_data <= captured_sensor_data[31:0];
                            imu_state        <= ST_IMU_BUILD_TLP;
                        end else begin
                            bit_cnt <= bit_cnt + 8'd1;
                        end
                    end
                end

                // Builds 64-Byte TLP (DW0-DW15)
                ST_IMU_BUILD_TLP: begin
                    m_imu_stream_tdata <= {
                        // DW0: Type=0x10 (DMA_Stream), Flags=0x00, Tag=0x00, Channel=0x02 (TELEMETRY)
                        8'h10, 8'h00, 8'h00, 8'h02,
                        // DW1: Target Address = 0x40000100 (Wishbone Device Base Address Parity!)
                        IMU_WB_BASE,
                        // DW2: Length = 4 DWs (14B payload), Seq = sample_count
                        16'd4, sample_count,
                        // DW3-DW4: Latched Hardware Timestamp (64-bit)
                        latched_timestamp,
                        // DW5-DW8: Captured 14 Bytes raw IMU payload + 2 Bytes zero-padding
                        captured_sensor_data[111:0], 16'h0000,
                        // DW9-DW14: Unused payload zero-padded (24 Bytes)
                        192'd0,
                        // DW15: CRC32 Placeholder
                        32'hDEADBEEF
                    };
                    imu_state <= ST_IMU_EMIT_TLP;
                end

                ST_IMU_EMIT_TLP: begin
                    m_imu_stream_tvalid <= 1'b1;
                    if (m_imu_stream_tready) begin
                        m_imu_stream_tvalid <= 1'b0;
                        imu_state           <= ST_IMU_IDLE;
                    end
                end

                default: imu_state <= ST_IMU_IDLE;
            endcase
        end
    end

endmodule

`default_nettype wire
