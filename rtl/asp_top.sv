// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX Top-Level Fabric Integrator (64-Byte TLP Profile)
//
// Wires together:
// 1. Configurable SPI / Dual-SPI 64B Frontend (asp_spi_frontend)
// 2. 512-Bit Vector TLP Router (asp_router)
// 3. Wishbone Master Gateway (asp_wishbone_master)
// 4. IMU SPI Master & Auto-DMA Core (asp_imu_auto_dma)
// 5. External Interrupt Doorbell pin (o_int_req) to Host CPU

module asp_top #(
    parameter bit DUAL_SPI_ENABLE = 1'b1
) (
    input  wire        clk,
    input  wire        rst_n,

    // Physical SPI / Dual-SPI Pins
    input  wire        spi_sclk,
    input  wire        spi_cs_n,
    inout  wire        spi_io0,
    inout  wire        spi_io1,

    // External Physical IMU Pins
    output logic       imu_sclk,
    output logic       imu_cs_n,
    output logic       imu_mosi,
    input  wire        imu_miso,
    input  wire        imu_int_i,

    // Motor Outputs (4 channels) & Status NeoPixel Pin
    output logic [3:0] o_motor_pins,
    output logic       o_neopixel_pin,

    // PWM Receiver / Input Capture Pins (4 channels)
    input  wire  [3:0] i_pwm_pins,

    // Linux-Controllable Onboard LEDs (0..5)
    output logic [5:0] o_led,

    // Hardware Logic Analyzer Debug Pins (0..3)
    output logic [3:0] o_debug_pins,

    // Host CPU Interrupt Request Doorbell Pin
    output logic       o_int_req
);

    // Monotonic 64-bit nanosecond system timestamp timer
    logic [63:0] sys_timestamp;
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) sys_timestamp <= 64'd0;
        else        sys_timestamp <= sys_timestamp + 64'd1;
    end

    // Logic Analyzer Debug Signals:
    // Debug 0: Host SPI CS Active
    // Debug 1: IMU Auto-DMA Stream Valid
    // Debug 2: FPGA TLP Egress Valid
    // Debug 3: Host Doorbell IRQ Asserted
    assign o_debug_pins[0] = ~spi_cs_n;
    assign o_debug_pins[1] = imu_stream_tvalid;
    assign o_debug_pins[2] = tlp_tx_valid;
    assign o_debug_pins[3] = o_int_req;

    // ------------------------------------------------------------------------
    // SPI Frontend <-> Router Signals (512-bit Vectors)
    // ------------------------------------------------------------------------
    logic [511:0] tlp_rx_data;
    logic         tlp_rx_valid;
    logic         tlp_rx_ready;

    logic [511:0] tlp_tx_data;
    logic         tlp_tx_valid;
    logic         tlp_tx_ready;

    logic [7:0]   egress_count;
    assign egress_count = tlp_tx_valid ? 8'h01 : 8'h00;

    asp_spi_frontend #(
        .DUAL_SPI_ENABLE(DUAL_SPI_ENABLE)
    ) u_spi_frontend (
        .clk             (clk),
        .rst_n           (rst_n),
        .i_sclk          (spi_sclk),
        .i_cs_n          (spi_cs_n),
        .io_sdio0        (spi_io0),
        .io_sdio1        (spi_io1),
        .i_egress_count  (egress_count),
        .o_int_req       (o_int_req),
        .o_tlp_rx_data   (tlp_rx_data),
        .o_tlp_rx_valid  (tlp_rx_valid),
        .i_tlp_rx_ready  (tlp_rx_ready),
        .i_tlp_tx_data   (tlp_tx_data),
        .i_tlp_tx_valid  (tlp_tx_valid),
        .o_tlp_tx_ready  (tlp_tx_ready),
        .o_status_flags  ()
    );

    // ------------------------------------------------------------------------
    // Router <-> Endpoints Signals
    // ------------------------------------------------------------------------
    logic [511:0] ctrl_tdata;
    logic         ctrl_tvalid;
    logic         ctrl_tready;

    logic [511:0] wb_cpl_tdata;
    logic         wb_cpl_tvalid;
    logic         wb_cpl_tready;

    logic [511:0] imu_stream_tdata;
    logic         imu_stream_tvalid;
    logic         imu_stream_tready;

    asp_router u_router (
        .clk                 (clk),
        .rst_n               (rst_n),
        .s_tlp_tdata         (tlp_rx_data),
        .s_tlp_tvalid        (tlp_rx_valid),
        .s_tlp_tready        (tlp_rx_ready),
        .m_ctrl_tdata        (ctrl_tdata),
        .m_ctrl_tvalid       (ctrl_tvalid),
        .m_ctrl_tready       (ctrl_tready),
        .m_tel_tdata         (),
        .m_tel_tvalid        (),
        .m_tel_tready        (1'b1),
        .m_esc_tdata         (),
        .m_esc_tvalid        (),
        .m_esc_tready        (1'b1),
        .m_egr_tdata         (tlp_tx_data),
        .m_egr_tvalid        (tlp_tx_valid),
        .m_egr_tready        (tlp_tx_ready),
        .s_wb_cpl_tdata      (wb_cpl_tdata),
        .s_wb_cpl_tvalid     (wb_cpl_tvalid),
        .s_wb_cpl_tready     (wb_cpl_tready),
        .s_imu_stream_tdata  (imu_stream_tdata),
        .s_imu_stream_tvalid (imu_stream_tvalid),
        .s_imu_stream_tready (imu_stream_tready),
        .s_esc_stream_tdata  (512'd0),
        .s_esc_stream_tvalid (1'b0),
        .s_esc_stream_tready ()
    );

    // ------------------------------------------------------------------------
    // Wishbone Master & On-Chip Wishbone Interconnect
    // ------------------------------------------------------------------------
    logic [31:0] wb_adr;
    logic [31:0] wb_dat_w;
    logic [31:0] wb_dat_r;
    logic [3:0]  wb_sel;
    logic        wb_we;
    logic        wb_cyc;
    logic        wb_stb;
    logic        wb_ack;

    asp_wishbone_master u_wishbone_master (
        .clk          (clk),
        .rst_n        (rst_n),
        .s_tlp_tdata  (ctrl_tdata),
        .s_tlp_tvalid (ctrl_tvalid),
        .s_tlp_tready (ctrl_tready),
        .m_cpl_tdata  (wb_cpl_tdata),
        .m_cpl_tvalid (wb_cpl_tvalid),
        .m_cpl_tready (wb_cpl_tready),
        .wb_adr_o     (wb_adr),
        .wb_dat_o     (wb_dat_w),
        .wb_sel_o     (wb_sel),
        .wb_we_o      (wb_we),
        .wb_cyc_o     (wb_cyc),
        .wb_stb_o     (wb_stb),
        .wb_ack_i     (wb_ack),
        .wb_dat_i     (wb_dat_r)
    );

    // ------------------------------------------------------------------------
    // Wishbone Slave Address Decoding:
    // SYS:      0x400000xx (SYS_VERSION, SCRATCH, LED_CTRL)
    // IMU:      0x400001xx (IMU Auto-DMA & Timestamping Engine)
    // DShot:    0x400002xx (Motor Channel 1..4 Control)
    // PWM Dec:  0x400003xx (PWM Receiver Decoder / Input Capture)
    // NeoPixel: 0x400006xx (WS2812B RGB Status LED)
    // ------------------------------------------------------------------------
    logic sys_sel, imu_sel, dshot_sel, pwm_dec_sel, neopixel_sel;
    assign sys_sel      = (wb_adr[31:8] == 24'h400000);
    assign imu_sel      = (wb_adr[31:8] == 24'h400001);
    assign dshot_sel    = (wb_adr[31:8] == 24'h400002);
    assign pwm_dec_sel  = (wb_adr[31:8] == 24'h400003);
    assign neopixel_sel = (wb_adr[31:8] == 24'h400006);

    logic [31:0] sys_wb_dat_r, imu_wb_dat_r, dshot_wb_dat_r, pwm_dec_wb_dat_r, neopixel_wb_dat_r;
    logic        sys_wb_ack,   imu_wb_ack,   dshot_wb_ack,   pwm_dec_wb_ack,   neopixel_wb_ack;

    assign wb_ack   = sys_wb_ack | imu_wb_ack | dshot_wb_ack | pwm_dec_wb_ack | neopixel_wb_ack;
    assign wb_dat_r = sys_sel      ? sys_wb_dat_r :
                      imu_sel      ? imu_wb_dat_r :
                      dshot_sel    ? dshot_wb_dat_r :
                      pwm_dec_sel  ? pwm_dec_wb_dat_r :
                      neopixel_sel ? neopixel_wb_dat_r : 32'd0;

    // System Control, PCIe ID & Master Timestamp Registers (Base: 0x40000000)
    asp_sys_regs u_sys_regs (
        .clk             (clk),
        .rst             (!rst_n),
        .i_sys_timestamp (sys_timestamp),
        .wb_adr_i        (wb_adr),
        .wb_dat_i        (wb_dat_w),
        .wb_sel_i        (wb_sel),
        .wb_we_i         (wb_we),
        .wb_cyc_i        (wb_cyc && sys_sel),
        .wb_stb_i        (wb_stb && sys_sel),
        .wb_ack_o        (sys_wb_ack),
        .wb_dat_o        (sys_wb_dat_r),
        .o_led_bits      (o_led)
    );

    // IMU SPI Master & Auto-DMA IP Core (Base: 0x40000100)
    asp_imu_auto_dma u_imu_core (
        .clk                 (clk),
        .rst_n               (rst_n),
        .i_sys_timestamp     (sys_timestamp),
        .o_imu_sclk          (imu_sclk),
        .o_imu_cs_n          (imu_cs_n),
        .o_imu_mosi          (imu_mosi),
        .i_imu_miso          (imu_miso),
        .i_imu_int           (imu_int_i),
        .wb_cyc_i            (wb_cyc && imu_sel),
        .wb_stb_i            (wb_stb && imu_sel),
        .wb_we_i             (wb_we),
        .wb_adr_i            (wb_adr),
        .wb_dat_i            (wb_dat_w),
        .wb_dat_o            (imu_wb_dat_r),
        .wb_ack_o            (imu_wb_ack),
        .m_imu_stream_tdata  (imu_stream_tdata),
        .m_imu_stream_tvalid (imu_stream_tvalid),
        .m_imu_stream_tready (imu_stream_tready)
    );

    // DShot / PWM Motor Control IP Core (Base: 0x40000200, 4 Channels)
    asp_dshot_core #(
        .NUM_CHANNELS(4)
    ) u_dshot_core (
        .clk           (clk),
        .rst_n         (rst_n),
        .wb_cyc        (wb_cyc && dshot_sel),
        .wb_stb        (wb_stb && dshot_sel),
        .wb_we         (wb_we),
        .wb_addr       (wb_adr),
        .wb_data_i     (wb_dat_w),
        .wb_data_o     (dshot_wb_dat_r),
        .wb_ack        (dshot_wb_ack),
        .o_motor_pins  (o_motor_pins)
    );

    // PWM Receiver Decoder / Input Capture Core (Base: 0x40000300, 4 Channels)
    asp_pwm_decoder #(
        .CLK_FREQ_HZ (27_000_000),
        .NUM_CHANNELS(4)
    ) u_pwm_decoder (
        .clk        (clk),
        .rst_n      (rst_n),
        .i_pwm_pins (i_pwm_pins),
        .wb_cyc_i   (wb_cyc && pwm_dec_sel),
        .wb_stb_i   (wb_stb && pwm_dec_sel),
        .wb_we_i    (wb_we),
        .wb_adr_i   (wb_adr),
        .wb_dat_i   (wb_dat_w),
        .wb_dat_o   (pwm_dec_wb_dat_r),
        .wb_ack_o   (pwm_dec_wb_ack)
    );

    // NeoPixel WS2812B Status LED Core (Base: 0x40000600)
    asp_neopixel_core u_neopixel_core (
        .clk            (clk),
        .rst_n          (rst_n),
        .wb_cyc         (wb_cyc && neopixel_sel),
        .wb_stb         (wb_stb && neopixel_sel),
        .wb_we          (wb_we),
        .wb_addr        (wb_adr),
        .wb_data_i      (wb_dat_w),
        .wb_data_o      (neopixel_wb_dat_r),
        .wb_ack         (neopixel_wb_ack),
        .o_neopixel_pin (o_neopixel_pin)
    );

endmodule

`default_nettype wire
