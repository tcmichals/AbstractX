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
//
// Shared by every serial-link board; board tops only map pins and clocks.
// @impl [SPEC-A5E-LINK-02] hw/tang_nano20k/SPECIFICATION.md

module asp_top #(
    // @impl [SPEC-FPGA-CLK-01] [SPEC-TN20K-03] hw/tang_nano20k/SPECIFICATION.md
    parameter bit DUAL_SPI_ENABLE = 1'b1,
    // Real frequency of `clk`; DShot, WS2812 and PWM timing derive from it.
    // Every board top sets it explicitly. [SPEC-FPGA-CLK-01]
    parameter int CLK_FREQ_HZ    = 27_000_000,
    parameter int NUM_PWM_INPUTS = 4
) (
    input  wire        clk,
    input  wire        rst_n,

    // Physical SPI / Dual-SPI Pins
    input  wire        spi_sclk,
    input  wire        spi_cs_n,
`ifdef VERILATOR
    input  wire        spi_io0,
    input  wire        spi_io1,
    output wire        spi_io0_o,
    output wire        spi_io1_o,
    output wire        spi_io0_oe,
    output wire        spi_io1_oe,
`else
    inout  wire        spi_io0,
    inout  wire        spi_io1,
`endif

    // External Physical IMU Pins
    output logic       imu_sclk,
    output logic       imu_cs_n,
    output logic       imu_mosi,
    input  wire        imu_miso,
    input  wire        imu_int_i,

    // Motor Outputs (4 channels) & Status NeoPixel Pin
    output logic [3:0] o_motor_pins,
    output logic       o_neopixel_pin,

    // PWM Receiver / Input Capture Pins (NUM_PWM_INPUTS channels)
    input  wire  [NUM_PWM_INPUTS-1:0] i_pwm_pins,

    // High-Speed Serial Transceiver Pins (0x40000500 base, 256-byte FIFOs)
    input  wire        i_uart_rx = 1'b1,
    output logic       o_uart_tx,

    // Linux-Controllable Onboard LEDs (0..5)
    output logic [5:0] o_led,

    // Hardware Logic Analyzer Debug Pins (0..3)
    output logic [3:0] o_debug_pins,

    // Host CPU Interrupt Request Doorbell Pin
    output logic       o_int_req
);

    // @impl [SPEC-A5E-LINK-02] hw/tang_nano20k/SPECIFICATION.md

    // 2-flop reset synchronizer for asynchronous active-low rst_n
    // @impl [SPEC-A5E-LINK-02] hw/tang_nano20k/SPECIFICATION.md
    /* verilator lint_off SYNCASYNCNET */
    logic rst_n_meta;
    logic sync_rst_n;
    /* verilator lint_on SYNCASYNCNET */
    always_ff @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            rst_n_meta <= 1'b0;
            sync_rst_n <= 1'b0;
        end else begin
            rst_n_meta <= 1'b1;
            sync_rst_n <= rst_n_meta;
        end
    end

    // Monotonic 64-bit nanosecond system timestamp timer
    logic [63:0] sys_timestamp;
    always_ff @(posedge clk or negedge sync_rst_n) begin
        if (!sync_rst_n) sys_timestamp <= 64'd0;
        else             sys_timestamp <= sys_timestamp + 64'd1;
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
        .rst_n           (sync_rst_n),
        .i_sclk          (spi_sclk),
        .i_cs_n          (spi_cs_n),
        .io_sdio0        (spi_io0),
        .io_sdio1        (spi_io1),
`ifdef VERILATOR
        .io_sdio0_o      (spi_io0_o),
        .io_sdio1_o      (spi_io1_o),
        .io_sdio0_oe     (spi_io0_oe),
        .io_sdio1_oe     (spi_io1_oe),
`endif
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

    logic [511:0] serial_ingress_tdata;
    logic         serial_ingress_tvalid;
    logic         serial_ingress_tready;

    logic [511:0] serial_stream_tdata;
    logic         serial_stream_tvalid;
    logic         serial_stream_tready;

    logic [511:0] pio_stream_tdata;
    logic         pio_stream_tvalid;
    logic         pio_stream_tready;

    asp_router u_router (
        .clk                 (clk),
        .rst_n               (sync_rst_n),
        .s_tlp_tdata         (tlp_rx_data),
        .s_tlp_tvalid        (tlp_rx_valid),
        .s_tlp_tready        (tlp_rx_ready),
        .m_ctrl_tdata        (ctrl_tdata),
        .m_ctrl_tvalid       (ctrl_tvalid),
        .m_ctrl_tready       (ctrl_tready),
        .m_tel_tdata         (),
        .m_tel_tvalid        (),
        .m_tel_tready        (1'b1),
        .m_esc_tdata         (serial_ingress_tdata),
        .m_esc_tvalid        (serial_ingress_tvalid),
        .m_esc_tready        (serial_ingress_tready),
        .m_egr_tdata         (tlp_tx_data),
        .m_egr_tvalid        (tlp_tx_valid),
        .m_egr_tready        (tlp_tx_ready),
        .s_wb_cpl_tdata      (wb_cpl_tdata),
        .s_wb_cpl_tvalid     (wb_cpl_tvalid),
        .s_wb_cpl_tready     (wb_cpl_tready),
        .s_imu_stream_tdata  (imu_stream_tdata),
        .s_imu_stream_tvalid (imu_stream_tvalid),
        .s_imu_stream_tready (imu_stream_tready),
        .s_esc_stream_tdata  (serial_stream_tdata),
        .s_esc_stream_tvalid (serial_stream_tvalid),
        .s_esc_stream_tready (serial_stream_tready),
        .s_pio_stream_tdata  (pio_stream_tdata),
        .s_pio_stream_tvalid (pio_stream_tvalid),
        .s_pio_stream_tready (pio_stream_tready)
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
        .rst_n        (sync_rst_n),
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
    // Serial:   0x400005xx (High-Speed UART with 256B FIFOs & Auto-TLP)
    // NeoPixel: 0x400006xx (WS2812B RGB Status LED)
    // PIO:      0x400007xx (Quad-State Machine Tile with RP2040/RP2350 ISA)
    // ------------------------------------------------------------------------
    logic sys_sel, imu_sel, dshot_sel, pwm_dec_sel, serial_sel, neopixel_sel, pio_sel;
    assign sys_sel      = (wb_adr[31:8] == 24'h400000);
    assign imu_sel      = (wb_adr[31:8] == 24'h400001);
    assign dshot_sel    = (wb_adr[31:8] == 24'h400002);
    assign pwm_dec_sel  = (wb_adr[31:8] == 24'h400003);
    assign serial_sel   = (wb_adr[31:8] == 24'h400005);
    assign neopixel_sel = (wb_adr[31:8] == 24'h400006);
    assign pio_sel      = (wb_adr[31:8] == 24'h400007) || (wb_adr[31:8] == 24'h400008);

    logic [31:0] sys_wb_dat_r, imu_wb_dat_r, dshot_wb_dat_r, pwm_dec_wb_dat_r, serial_wb_dat_r, neopixel_wb_dat_r, pio_wb_dat_r;
    logic        sys_wb_ack,   imu_wb_ack,   dshot_wb_ack,   pwm_dec_wb_ack,   serial_wb_ack,   neopixel_wb_ack,   pio_wb_ack;

    assign wb_ack   = sys_wb_ack | imu_wb_ack | dshot_wb_ack | pwm_dec_wb_ack | serial_wb_ack | neopixel_wb_ack | pio_wb_ack;
    assign wb_dat_r = sys_sel      ? sys_wb_dat_r :
                      imu_sel      ? imu_wb_dat_r :
                      dshot_sel    ? dshot_wb_dat_r :
                      pwm_dec_sel  ? pwm_dec_wb_dat_r :
                      serial_sel   ? serial_wb_dat_r :
                      neopixel_sel ? neopixel_wb_dat_r :
                      pio_sel      ? pio_wb_dat_r : 32'd0;

    // System Control, PCIe ID & Master Timestamp Registers (Base: 0x40000000)
    asp_sys_regs u_sys_regs (
        .clk             (clk),
        .rst             (!sync_rst_n),
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
        .rst_n               (sync_rst_n),
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
    // @impl [SPEC-FPGA-CLK-01] hw/tang_nano20k/SPECIFICATION.md
    asp_dshot_core #(
        .CLK_FREQ_HZ (CLK_FREQ_HZ),
        .NUM_CHANNELS(4)
    ) u_dshot_core (
        .clk           (clk),
        .rst_n         (sync_rst_n),
        .wb_cyc        (wb_cyc && dshot_sel),
        .wb_stb        (wb_stb && dshot_sel),
        .wb_we         (wb_we),
        .wb_addr       (wb_adr),
        .wb_data_i     (wb_dat_w),
        .wb_data_o     (dshot_wb_dat_r),
        .wb_ack        (dshot_wb_ack),
        .o_motor_pins  (o_motor_pins)
    );

    // PWM Receiver Decoder / Input Capture Core (Base: 0x40000300, NUM_PWM_INPUTS channels)
    asp_pwm_decoder #(
        .CLK_FREQ_HZ (CLK_FREQ_HZ),
        .NUM_CHANNELS(NUM_PWM_INPUTS)
    ) u_pwm_decoder (
        .clk        (clk),
        .rst_n      (sync_rst_n),
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
    asp_neopixel_core #(
        .CLK_FREQ_HZ(CLK_FREQ_HZ)
    ) u_neopixel_core (
        .clk            (clk),
        .rst_n          (sync_rst_n),
        .wb_cyc         (wb_cyc && neopixel_sel),
        .wb_stb         (wb_stb && neopixel_sel),
        .wb_we          (wb_we),
        .wb_addr        (wb_adr),
        .wb_data_i      (wb_dat_w),
        .wb_data_o      (neopixel_wb_dat_r),
        .wb_ack         (neopixel_wb_ack),
        .o_neopixel_pin (o_neopixel_pin)
    );

    // High-Speed UART / Serial Core with 256-Byte FIFOs (Base: 0x40000500)
    asp_uart_core #(
        .CLK_FREQ_HZ (CLK_FREQ_HZ),
        .DEFAULT_BAUD(115_200),
        .FIFO_DEPTH  (256)
    ) u_uart_core (
        .clk             (clk),
        .rst_n           (sync_rst_n),
        .i_sys_timestamp (sys_timestamp),
        .i_uart_rx       (i_uart_rx),
        .o_uart_tx       (o_uart_tx),
        .wb_cyc_i        (wb_cyc && serial_sel),
        .wb_stb_i        (wb_stb && serial_sel),
        .wb_we_i         (wb_we),
        .wb_adr_i        (wb_adr[7:0]),
        .wb_dat_i        (wb_dat_w),
        .wb_dat_o        (serial_wb_dat_r),
        .wb_ack_o        (serial_wb_ack),
        .s_tlp_tdata     (serial_ingress_tdata),
        .s_tlp_tvalid    (serial_ingress_tvalid),
        .s_tlp_tready    (serial_ingress_tready),
        .m_tlp_tdata     (serial_stream_tdata),
        .m_tlp_tvalid    (serial_stream_tvalid),
        .m_tlp_tready    (serial_stream_tready),
        .o_uart_irq      ()
    );

    // PIO Subsystem Tile 0 (Base: 0x40000700)
    asp_pio_tile #(
        .BASE_ADDR(32'h40000700),
        .TILE_ID  (0)
    ) u_pio_tile (
        .clk             (clk),
        .rst_n           (sync_rst_n),
        .i_sys_timestamp (sys_timestamp),
        .i_gpio          ({16'd0, i_pwm_pins, imu_int_i, imu_miso, 10'd0}),
        .o_gpio_out      (),
        .o_gpio_pindir   (),
        .wb_cyc_i        (wb_cyc && pio_sel),
        .wb_stb_i        (wb_stb && pio_sel),
        .wb_we_i         (wb_we),
        .wb_adr_i        (wb_adr),
        .wb_dat_i        (wb_dat_w),
        .wb_dat_o        (pio_wb_dat_r),
        .wb_ack_o        (pio_wb_ack),
        .s_tlp_tdata     (512'd0),          // router port for PIO channels: EL-1027-39
        .s_tlp_tvalid    (1'b0),
        .s_tlp_tready    (),
        .m_tlp_tdata     (pio_stream_tdata),
        .m_tlp_tvalid    (pio_stream_tvalid),
        .m_tlp_tready    (pio_stream_tready),
        .o_pio_irq       ()
    );

endmodule

`default_nettype wire
