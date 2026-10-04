// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX QMTECH Zynq-7020 Top-Level FPGA Fabric Integrator
//
// Bridges Zynq PS M_AXI_GP0 (AXI4-Lite) to the 64-Byte TLP Router and Wishbone Interconnect.
// Integrates:
// 1. AXI4-Lite to 64-byte TLP Bridge (asp_axi_lite_bridge)
// 2. 512-bit (64-byte) Vector TLP Router (asp_router)
// 3. Wishbone Master Gateway (asp_wishbone_master)
// 4. System Identification & Timestamp Registers (asp_sys_regs)
// 5. Autonomous IMU SPI Master & Auto-DMA Core (asp_imu_auto_dma)
// 6. DShot / PWM Motor Core (asp_dshot_core)
// 7. WS2812B NeoPixel Status Core (asp_neopixel_core)
//
// @impl [SPEC-ZYNQ-01] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
// @impl [SPEC-ZYNQ-02] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
// @impl [SPEC-ZYNQ-03] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-03
// @impl [SPEC-ZYNQ-04] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-04
// @impl [SPEC-ZYNQ-05] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-05
module top_qmtech_zynq7020 #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 16,
    parameter integer CLK_FREQ_HZ        = 100_000_000
) (
    // Zynq PS Clocks & Resets (FCLK_CLK0 & FCLK_RESET0_N)
    input  wire                              s_axi_aclk,
    input  wire                              s_axi_aresetn,

    // AXI4-Lite Write Address Channel (from PS M_AXI_GP0)
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     s_axi_awaddr,
    input  wire [2:0]                        s_axi_awprot,
    input  wire                              s_axi_awvalid,
    output logic                             s_axi_awready,

    // AXI4-Lite Write Data Channel
    input  wire [C_S_AXI_DATA_WIDTH-1:0]     s_axi_wdata,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0] s_axi_wstrb,
    input  wire                              s_axi_wvalid,
    output logic                             s_axi_wready,

    // AXI4-Lite Write Response Channel
    output logic [1:0]                       s_axi_bresp,
    output logic                             s_axi_bvalid,
    input  wire                              s_axi_bready,

    // AXI4-Lite Read Address Channel
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]     s_axi_araddr,
    input  wire [2:0]                        s_axi_arprot,
    input  wire                              s_axi_arvalid,
    output logic                             s_axi_arready,

    // AXI4-Lite Read Data Channel
    output logic [C_S_AXI_DATA_WIDTH-1:0]    s_axi_rdata,
    output logic [1:0]                       s_axi_rresp,
    output logic                             s_axi_rvalid,
    input  wire                              s_axi_rready,

    // Interrupt Request Line to PS GIC (IRQ_F2P[0])
    output logic                             irq_f2p,

    // External Physical IMU Pins (PMOD / Header)
    output logic                             o_imu_sclk,
    output logic                             o_imu_cs_n,
    output logic                             o_imu_mosi,
    input  wire                              i_imu_miso,
    input  wire                              i_imu_int,

    // Motor Outputs (4 channels) & Status NeoPixel Pin
    output logic [3:0]                       o_motor_pins,
    output logic                             o_neopixel,

    // Onboard LEDs
    output logic                             o_led_carrier, // Carrier D3 (P22)
    output logic                             o_led_core,    // Core board D2 (M14)

    // User Key Input
    input  wire                              i_key_carrier, // Carrier Key (P16)

    // Hardware Logic Analyzer Debug Pins (0..3)
    output logic [3:0]                       o_debug_pins
);

    // Monotonic 64-bit nanosecond system timestamp timer
    logic [63:0] sys_timestamp;
    always_ff @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) sys_timestamp <= 64'd0;
        else                sys_timestamp <= sys_timestamp + 64'd1;
    end

    // ------------------------------------------------------------------------
    // AXI-Lite Bridge <-> Router Signals (512-bit Vectors)
    // ------------------------------------------------------------------------
    logic [511:0] tlp_rx_data;
    logic         tlp_rx_valid;
    logic         tlp_rx_ready;

    logic [511:0] tlp_tx_data;
    logic         tlp_tx_valid;
    logic         tlp_tx_ready;

    asp_axi_lite_bridge #(
        .C_S_AXI_DATA_WIDTH (C_S_AXI_DATA_WIDTH),
        .C_S_AXI_ADDR_WIDTH (C_S_AXI_ADDR_WIDTH),
        .FIFO_DEPTH         (16)
    ) u_axi_bridge (
        .s_axi_aclk     (s_axi_aclk),
        .s_axi_aresetn  (s_axi_aresetn),
        .s_axi_awaddr   (s_axi_awaddr),
        .s_axi_awprot   (s_axi_awprot),
        .s_axi_awvalid  (s_axi_awvalid),
        .s_axi_awready  (s_axi_awready),
        .s_axi_wdata    (s_axi_wdata),
        .s_axi_wstrb    (s_axi_wstrb),
        .s_axi_wvalid   (s_axi_wvalid),
        .s_axi_wready   (s_axi_wready),
        .s_axi_bresp    (s_axi_bresp),
        .s_axi_bvalid   (s_axi_bvalid),
        .s_axi_bready   (s_axi_bready),
        .s_axi_araddr   (s_axi_araddr),
        .s_axi_arprot   (s_axi_arprot),
        .s_axi_arvalid  (s_axi_arvalid),
        .s_axi_arready  (s_axi_arready),
        .s_axi_rdata    (s_axi_rdata),
        .s_axi_rresp    (s_axi_rresp),
        .s_axi_rvalid   (s_axi_rvalid),
        .s_axi_rready   (s_axi_rready),
        .irq_f2p        (irq_f2p),
        .m_tlp_tdata    (tlp_rx_data),
        .m_tlp_tvalid   (tlp_rx_valid),
        .m_tlp_tready   (tlp_rx_ready),
        .s_egr_tdata    (tlp_tx_data),
        .s_egr_tvalid   (tlp_tx_valid),
        .s_egr_tready   (tlp_tx_ready)
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
        .clk                 (s_axi_aclk),
        .rst_n               (s_axi_aresetn),
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
        .clk          (s_axi_aclk),
        .rst_n        (s_axi_aresetn),
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
    // NeoPixel: 0x400006xx (WS2812B RGB Status LED)
    // ------------------------------------------------------------------------
    logic sys_sel, imu_sel, dshot_sel, neopixel_sel;
    assign sys_sel      = (wb_adr[31:8] == 24'h400000);
    assign imu_sel      = (wb_adr[31:8] == 24'h400001);
    assign dshot_sel    = (wb_adr[31:8] == 24'h400002);
    assign neopixel_sel = (wb_adr[31:8] == 24'h400006);

    logic [31:0] sys_wb_dat_r, imu_wb_dat_r, dshot_wb_dat_r, neopixel_wb_dat_r;
    logic        sys_wb_ack,   imu_wb_ack,   dshot_wb_ack,   neopixel_wb_ack;

    assign wb_ack   = sys_wb_ack | imu_wb_ack | dshot_wb_ack | neopixel_wb_ack;
    assign wb_dat_r = sys_sel      ? sys_wb_dat_r :
                      imu_sel      ? imu_wb_dat_r :
                      dshot_sel    ? dshot_wb_dat_r :
                      neopixel_sel ? neopixel_wb_dat_r : 32'd0;

    logic [5:0] led_bits;

    // System Control, PCIe ID & Master Timestamp Registers (Base: 0x40000000)
    asp_sys_regs u_sys_regs (
        .clk             (s_axi_aclk),
        .rst             (!s_axi_aresetn),
        .i_sys_timestamp (sys_timestamp),
        .wb_adr_i        (wb_adr),
        .wb_dat_i        (wb_dat_w),
        .wb_sel_i        (wb_sel),
        .wb_we_i         (wb_we),
        .wb_cyc_i        (wb_cyc && sys_sel),
        .wb_stb_i        (wb_stb && sys_sel),
        .wb_ack_o        (sys_wb_ack),
        .wb_dat_o        (sys_wb_dat_r),
        .o_led_bits      (led_bits)
    );

    assign o_led_carrier = led_bits[0];
    assign o_led_core    = led_bits[1];

    // IMU SPI Master & Auto-DMA IP Core (Base: 0x40000100)
    asp_imu_auto_dma u_imu_core (
        .clk                 (s_axi_aclk),
        .rst_n               (s_axi_aresetn),
        .i_sys_timestamp     (sys_timestamp),
        .o_imu_sclk          (o_imu_sclk),
        .o_imu_cs_n          (o_imu_cs_n),
        .o_imu_mosi          (o_imu_mosi),
        .i_imu_miso          (i_imu_miso),
        .i_imu_int           (i_imu_int),
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

    // Motor Output Core (DShot600 / DShot300 / PWM, Base: 0x40000200)
    asp_dshot_core #(
        .CLK_FREQ_HZ  (CLK_FREQ_HZ),
        .NUM_CHANNELS (4)
    ) u_motor_core (
        .clk          (s_axi_aclk),
        .rst_n        (s_axi_aresetn),
        .wb_cyc       (wb_cyc && dshot_sel),
        .wb_stb       (wb_stb && dshot_sel),
        .wb_we        (wb_we),
        .wb_addr      (wb_adr),
        .wb_data_i    (wb_dat_w),
        .wb_data_o    (dshot_wb_dat_r),
        .wb_ack       (dshot_wb_ack),
        .o_motor_pins (o_motor_pins)
    );

    // WS2812B NeoPixel Controller (Base: 0x40000600)
    asp_neopixel_core #(
        .CLK_FREQ_HZ (CLK_FREQ_HZ)
    ) u_neopixel (
        .clk          (s_axi_aclk),
        .rst_n        (s_axi_aresetn),
        .wb_cyc       (wb_cyc && neopixel_sel),
        .wb_stb       (wb_stb && neopixel_sel),
        .wb_we        (wb_we),
        .wb_addr      (wb_adr),
        .wb_data_i    (wb_dat_w),
        .wb_data_o    (neopixel_wb_dat_r),
        .wb_ack       (neopixel_wb_ack),
        .o_pixel_pin  (o_neopixel)
    );

    // Logic Analyzer Debug Signals:
    // Debug 0: IMU DRDY Input
    // Debug 1: IMU Auto-DMA Stream Valid
    // Debug 2: FPGA TLP Egress Valid
    // Debug 3: IRQ_F2P[0] Asserted
    assign o_debug_pins[0] = i_imu_int;
    assign o_debug_pins[1] = imu_stream_tvalid;
    assign o_debug_pins[2] = tlp_tx_valid;
    assign o_debug_pins[3] = irq_f2p;

endmodule
`default_nettype wire
