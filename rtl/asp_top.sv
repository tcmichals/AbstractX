// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX Top-Level Fabric Integrator (64-Byte TLP Profile)
//
// Wires together:
// 1. Configurable SPI / Dual-SPI 64B Frontend (asp_spi_frontend)
// 2. 512-Bit Vector TLP Router (asp_router)
// 3. Wishbone Master Gateway (asp_wishbone_master) -> System registers (asp_sys_regs)
// 4. PIO tile (pio2, asp_pio_wrap) driving every peripheral pin ([SPEC-TLP-07], all-PIO)
// 5. External Interrupt Doorbell pin (o_int_req) to Host CPU
//
// Shared by every serial-link board; board tops only map pins and clocks.
// @impl [SPEC-A5E-LINK-02] hw/tang_nano20k/SPECIFICATION.md
// @impl [SPEC-TLP-07] docs/DESIGN_SPECIFICATION.md

module asp_top #(
    // @impl [SPEC-FPGA-CLK-01] [SPEC-TN20K-03] hw/tang_nano20k/SPECIFICATION.md
    parameter bit DUAL_SPI_ENABLE = 1'b1,
    // Real frequency of `clk`; PIO clock dividers (DShot, serial) are computed from it by the host.
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
    // Debug 1: PIO stream TLP valid
    // Debug 2: FPGA TLP Egress Valid
    // Debug 3: Host Doorbell IRQ Asserted
    assign o_debug_pins[0] = ~spi_cs_n;
    assign o_debug_pins[1] = pio_stream_tvalid;
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
    // Router ([SPEC-TLP-07]): CONTROL -> Wishbone master, every other channel -> PIO tile
    // ------------------------------------------------------------------------
    logic [511:0] ctrl_tdata,   wb_cpl_tdata,   pio_in_tdata,   pio_stream_tdata;
    logic         ctrl_tvalid,  wb_cpl_tvalid,  pio_in_tvalid,  pio_stream_tvalid;
    logic         ctrl_tready,  wb_cpl_tready,  pio_in_tready,  pio_stream_tready;

    asp_router u_router (
        .clk                 (clk),
        .rst_n               (sync_rst_n),
        .s_tlp_tdata         (tlp_rx_data),
        .s_tlp_tvalid        (tlp_rx_valid),
        .s_tlp_tready        (tlp_rx_ready),
        .m_ctrl_tdata        (ctrl_tdata),
        .m_ctrl_tvalid       (ctrl_tvalid),
        .m_ctrl_tready       (ctrl_tready),
        .m_pio_tdata         (pio_in_tdata),
        .m_pio_tvalid        (pio_in_tvalid),
        .m_pio_tready        (pio_in_tready),
        .m_egr_tdata         (tlp_tx_data),
        .m_egr_tvalid        (tlp_tx_valid),
        .m_egr_tready        (tlp_tx_ready),
        .s_wb_cpl_tdata      (wb_cpl_tdata),
        .s_wb_cpl_tvalid     (wb_cpl_tvalid),
        .s_wb_cpl_tready     (wb_cpl_tready),
        .s_pio_stream_tdata  (pio_stream_tdata),
        .s_pio_stream_tvalid (pio_stream_tvalid),
        .s_pio_stream_tready (pio_stream_tready)
    );

    // ------------------------------------------------------------------------
    // Wishbone master and slaves: SYS 0x400000xx, PIO 0x400007xx / 0x400008xx ([SPEC-TLP-07])
    // ------------------------------------------------------------------------
    logic [31:0] wb_adr, wb_dat_w, wb_dat_r;
    logic [3:0]  wb_sel;
    logic        wb_we, wb_cyc, wb_stb, wb_ack;

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

    wire sys_sel   = wb_adr[31:8] == 24'h400000;
    wire pio_sel   = wb_adr[31:8] == 24'h400007 || wb_adr[31:8] == 24'h400008;
    wire none_sel  = !sys_sel && !pio_sel;
    logic [31:0] sys_wb_dat_r, pio_wb_dat_r;
    logic        sys_wb_ack, pio_wb_ack, none_ack;

    // An address no slave decodes is acknowledged with data 0, so the master never waits forever.
    always_ff @(posedge clk or negedge sync_rst_n) begin
        if (!sync_rst_n) none_ack <= 1'b0;
        else             none_ack <= wb_cyc && wb_stb && none_sel && !none_ack;
    end

    assign wb_ack   = sys_wb_ack | pio_wb_ack | none_ack;
    assign wb_dat_r = sys_sel ? sys_wb_dat_r : pio_sel ? pio_wb_dat_r : 32'd0;

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

    // ------------------------------------------------------------------------
    // PIO tile and its pins ([SPEC-TLP-07] pin table)
    // ------------------------------------------------------------------------
    logic [31:0] gpio_out, gpio_dir, gpio_in;

    asp_pio_pins #(.NUM_PWM_INPUTS(NUM_PWM_INPUTS)) u_pio_pins (
        .i_gpio_out(gpio_out), .i_gpio_pindir(gpio_dir), .o_gpio_in(gpio_in),
        .o_motor_pins(o_motor_pins), .o_neopixel_pin(o_neopixel_pin),
        .o_uart_tx(o_uart_tx), .i_uart_rx(i_uart_rx),
        .o_imu_sclk(imu_sclk), .o_imu_cs_n(imu_cs_n), .o_imu_mosi(imu_mosi),
        .i_imu_miso(imu_miso), .i_imu_int(imu_int_i), .i_pwm_pins(i_pwm_pins)
    );

    asp_pio_wrap #(
        .BASE_ADDR(32'h40000700),
        .TILE_ID  (0)
    ) u_pio_tile (
        .clk             (clk),
        .rst_n           (sync_rst_n),
        .i_sys_timestamp (sys_timestamp),
        .i_gpio          (gpio_in),
        .o_gpio_out      (gpio_out),
        .o_gpio_pindir   (gpio_dir),
        .wb_cyc_i        (wb_cyc && pio_sel),
        .wb_stb_i        (wb_stb && pio_sel),
        .wb_we_i         (wb_we),
        .wb_adr_i        (wb_adr),
        .wb_dat_i        (wb_dat_w),
        .wb_dat_o        (pio_wb_dat_r),
        .wb_ack_o        (pio_wb_ack),
        .s_tlp_tdata     (pio_in_tdata),
        .s_tlp_tvalid    (pio_in_tvalid),
        .s_tlp_tready    (pio_in_tready),
        .m_tlp_tdata     (pio_stream_tdata),
        .m_tlp_tvalid    (pio_stream_tvalid),
        .m_tlp_tready    (pio_stream_tready),
        .o_pio_irq       ()
    );

endmodule

`default_nettype wire
