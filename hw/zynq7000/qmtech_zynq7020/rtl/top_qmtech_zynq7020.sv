// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX QMTECH Zynq-7020 Top-Level FPGA Fabric Integrator
//
// Bridges Zynq PS M_AXI_GP0 (AXI4-Lite) through two 8 KiB BRAM packet FIFOs
// to the 64-Byte TLP Router and the AXI4-Lite control plane. The HP0 pins remain in
// this wrapper for optional DDR-profile compatibility but are inactive here.
//
// Integrates:
// 1. AXI4-Lite / BRAM TLP Bridge (asp_axi_lite_bridge)
// 2. 512-bit (64-byte) Vector TLP Router (asp_router)
// 3. Control plane (asp_ctrl_plane: TLP -> AXI4-Lite master -> crossbar -> asp_sys_regs + PIO tile)
// 5. PIO tile (pio2, asp_pio_wrap) driving the IMU, motor, NeoPixel and UART pins ([SPEC-TLP-07])
//
// @impl [SPEC-ZYNQ-01] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
// @impl [SPEC-ZYNQ-02] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
// @impl [SPEC-ZYNQ-03] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-03
// @impl [SPEC-ZYNQ-04] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-04
// @impl [SPEC-ZYNQ-05] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-05
// @impl [SPEC-ZYNQ-08] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-08
module top_qmtech_zynq7020 #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 16,
    parameter integer C_M_AXI_DATA_WIDTH = 64,
    parameter integer C_M_AXI_ADDR_WIDTH = 32,
    parameter integer CLK_FREQ_HZ        = 100_000_000
) (
    // Zynq PS Clocks & Resets (FCLK_CLK0 & FCLK_RESET0_N)
    input  wire                                 s_axi_aclk,
    input  wire                                 s_axi_aresetn,

    // ========================================================================
    // AXI4-Lite Slave: CSR Interface (from PS M_AXI_GP0)
    // ========================================================================
    input  wire [C_S_AXI_ADDR_WIDTH-1:0]        s_axi_awaddr,
    input  wire [2:0]                           s_axi_awprot,
    input  wire                                 s_axi_awvalid,
    output logic                                s_axi_awready,

    input  wire [C_S_AXI_DATA_WIDTH-1:0]        s_axi_wdata,
    input  wire [(C_S_AXI_DATA_WIDTH/8)-1:0]    s_axi_wstrb,
    input  wire                                 s_axi_wvalid,
    output logic                                s_axi_wready,

    output logic [1:0]                          s_axi_bresp,
    output logic                                s_axi_bvalid,
    input  wire                                 s_axi_bready,

    input  wire [C_S_AXI_ADDR_WIDTH-1:0]        s_axi_araddr,
    input  wire [2:0]                           s_axi_arprot,
    input  wire                                 s_axi_arvalid,
    output logic                                s_axi_arready,

    output logic [C_S_AXI_DATA_WIDTH-1:0]       s_axi_rdata,
    output logic [1:0]                          s_axi_rresp,
    output logic                                s_axi_rvalid,
    input  wire                                 s_axi_rready,

    // ========================================================================
    // AXI4 Master: Direct DDR3 Access (to PS S_AXI_HP0)
    // ========================================================================
    output logic [C_M_AXI_ADDR_WIDTH-1:0]       m_axi_hp_awaddr,
    output logic [7:0]                          m_axi_hp_awlen,
    output logic [2:0]                          m_axi_hp_awsize,
    output logic [1:0]                          m_axi_hp_awburst,
    output logic                                m_axi_hp_awvalid,
    input  wire                                 m_axi_hp_awready,

    output logic [C_M_AXI_DATA_WIDTH-1:0]       m_axi_hp_wdata,
    output logic [(C_M_AXI_DATA_WIDTH/8)-1:0]   m_axi_hp_wstrb,
    output logic                                m_axi_hp_wlast,
    output logic                                m_axi_hp_wvalid,
    input  wire                                 m_axi_hp_wready,

    input  wire  [1:0]                          m_axi_hp_bresp,
    input  wire                                 m_axi_hp_bvalid,
    output logic                                m_axi_hp_bready,

    output logic [C_M_AXI_ADDR_WIDTH-1:0]       m_axi_hp_araddr,
    output logic [7:0]                          m_axi_hp_arlen,
    output logic [2:0]                          m_axi_hp_arsize,
    output logic [1:0]                          m_axi_hp_arburst,
    output logic                                m_axi_hp_arvalid,
    input  wire                                 m_axi_hp_arready,

    input  wire  [C_M_AXI_DATA_WIDTH-1:0]       m_axi_hp_rdata,
    input  wire  [1:0]                          m_axi_hp_rresp,
    input  wire                                 m_axi_hp_rlast,
    input  wire                                 m_axi_hp_rvalid,
    output logic                                m_axi_hp_rready,

    // Interrupt Request Line to PS GIC (IRQ_F2P[0])
    output logic                                irq_f2p,

    // External Physical IMU Pins (PMOD / Header)
    output logic                                o_imu_sclk,
    output logic                                o_imu_cs_n,
    output logic                                o_imu_mosi,
    input  wire                                 i_imu_miso,
    input  wire                                 i_imu_int,

    // Motor Outputs (4 channels) & Status NeoPixel Pin
    output logic [3:0]                          o_motor_pins,
    output logic                                o_neopixel,

    // Onboard LEDs
    output logic                                o_led_carrier, // Carrier D3 (P22)
    output logic                                o_led_core,    // Core board D2 (M14)

    // User Key Input
    input  wire                                 i_key_carrier, // Carrier Key (P16)

    // High-Speed Serial Transceiver Pins (0x40000500 base, 256-byte FIFOs)
    input  wire                                 i_uart_rx,
    output logic                                o_uart_tx,

    // Hardware Logic Analyzer Debug Pins (0..3)
    output logic [3:0]                          o_debug_pins
);

    // Monotonic 64-bit nanosecond system timestamp timer [SPEC-ZYNQ-08]
    localparam logic [63:0] TIMER_CLK_FREQ_HZ = {32'd0, CLK_FREQ_HZ};
    localparam logic [63:0] NS_PER_CYCLE       = 64'd1_000_000_000 / TIMER_CLK_FREQ_HZ;
    localparam logic [63:0] NS_REMAINDER       = 64'd1_000_000_000 % TIMER_CLK_FREQ_HZ;

    logic [63:0] sys_timestamp;
    logic [63:0] timestamp_phase;

    generate
        if (NS_REMAINDER == 0) begin : gen_clean_timer
            // Default 100 MHz PL clock: exact 10 ns per cycle (zero remainder, no division)
            always_ff @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
                if (!s_axi_aresetn) begin
                    sys_timestamp <= 64'd0;
                end else begin
                    sys_timestamp <= sys_timestamp + NS_PER_CYCLE;
                end
            end
        end else begin : gen_fractional_timer
            // Non-integer frequency: single-cycle accumulator with subtractor (zero runtime division)
            always_ff @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
                if (!s_axi_aresetn) begin
                    sys_timestamp   <= 64'd0;
                    timestamp_phase <= 64'd0;
                end else if (timestamp_phase + NS_REMAINDER >= TIMER_CLK_FREQ_HZ) begin
                    sys_timestamp   <= sys_timestamp + NS_PER_CYCLE + 64'd1;
                    timestamp_phase <= (timestamp_phase + NS_REMAINDER) - TIMER_CLK_FREQ_HZ;
                end else begin
                    sys_timestamp   <= sys_timestamp + NS_PER_CYCLE;
                    timestamp_phase <= timestamp_phase + NS_REMAINDER;
                end
            end
        end
    endgenerate

    // ------------------------------------------------------------------------
    // AXI DMA Core <-> Router Signals (512-bit Vectors)
    // ------------------------------------------------------------------------
    logic [511:0] tlp_rx_data;
    logic         tlp_rx_valid;
    logic         tlp_rx_ready;

    logic [511:0] tlp_tx_data;
    logic         tlp_tx_valid;
    logic         tlp_tx_ready;

    // @impl [SPEC-ZYNQ-PLATFORM-09] hw/zynq7000/SPECIFICATION.md
    asp_axi_lite_bridge #(
        .C_S_AXI_DATA_WIDTH (C_S_AXI_DATA_WIDTH),
        .C_S_AXI_ADDR_WIDTH (C_S_AXI_ADDR_WIDTH),
        .FIFO_DEPTH         (128)
    ) u_axi_bram_bridge (
        .s_axi_aclk      (s_axi_aclk),
        .s_axi_aresetn   (s_axi_aresetn),

        // CSR Slave Port
        .s_axi_awaddr    (s_axi_awaddr),
        .s_axi_awprot    (s_axi_awprot),
        .s_axi_awvalid   (s_axi_awvalid),
        .s_axi_awready   (s_axi_awready),
        .s_axi_wdata     (s_axi_wdata),
        .s_axi_wstrb     (s_axi_wstrb),
        .s_axi_wvalid    (s_axi_wvalid),
        .s_axi_wready    (s_axi_wready),
        .s_axi_bresp     (s_axi_bresp),
        .s_axi_bvalid    (s_axi_bvalid),
        .s_axi_bready    (s_axi_bready),
        .s_axi_araddr    (s_axi_araddr),
        .s_axi_arprot    (s_axi_arprot),
        .s_axi_arvalid   (s_axi_arvalid),
        .s_axi_arready   (s_axi_arready),
        .s_axi_rdata     (s_axi_rdata),
        .s_axi_rresp     (s_axi_rresp),
        .s_axi_rvalid    (s_axi_rvalid),
        .s_axi_rready    (s_axi_rready),

        // IRQ & 64B TLP Streams
        .irq_f2p         (irq_f2p),
        .m_tlp_tdata     (tlp_rx_data),
        .m_tlp_tvalid    (tlp_rx_valid),
        .m_tlp_tready    (tlp_rx_ready),
        .s_egr_tdata     (tlp_tx_data),
        .s_egr_tvalid    (tlp_tx_valid),
        .s_egr_tready    (tlp_tx_ready)
    );

    // Optional DDR-profile interface is intentionally idle in BRAM mode.
    assign m_axi_hp_awaddr  = '0;
    assign m_axi_hp_awlen   = '0;
    assign m_axi_hp_awsize  = '0;
    assign m_axi_hp_awburst = '0;
    assign m_axi_hp_awvalid = 1'b0;
    assign m_axi_hp_wdata   = '0;
    assign m_axi_hp_wstrb   = '0;
    assign m_axi_hp_wlast   = 1'b0;
    assign m_axi_hp_wvalid  = 1'b0;
    assign m_axi_hp_bready  = 1'b0;
    assign m_axi_hp_araddr  = '0;
    assign m_axi_hp_arlen   = '0;
    assign m_axi_hp_arsize  = '0;
    assign m_axi_hp_arburst = '0;
    assign m_axi_hp_arvalid = 1'b0;
    assign m_axi_hp_rready  = 1'b0;

    // ------------------------------------------------------------------------
    // Router ([SPEC-TLP-07]): CONTROL -> control plane, every other channel -> PIO tile
    // ------------------------------------------------------------------------
    logic [511:0] ctrl_tdata,   cpl_tdata,   pio_in_tdata,   pio_stream_tdata;
    logic         ctrl_tvalid,  cpl_tvalid,  pio_in_tvalid,  pio_stream_tvalid;
    logic         ctrl_tready,  cpl_tready,  pio_in_tready,  pio_stream_tready;

    asp_router u_router (
        .clk                 (s_axi_aclk),
        .rst_n               (s_axi_aresetn),
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
        .s_cpl_tdata      (cpl_tdata),
        .s_cpl_tvalid     (cpl_tvalid),
        .s_cpl_tready     (cpl_tready),
        .s_pio_stream_tdata  (pio_stream_tdata),
        .s_pio_stream_tvalid (pio_stream_tvalid),
        .s_pio_stream_tready (pio_stream_tready)
    );

    // ------------------------------------------------------------------------
    // Control plane ([SPEC-TLP-07], [SPEC-TLP-04]): TLP -> AXI4-Lite master -> crossbar ->
    // system registers + PIO tile; an unmapped address completes with DECERR. No Wishbone.
    // ------------------------------------------------------------------------
    logic [5:0]  led_bits;
    assign o_led_carrier = led_bits[0];
    assign o_led_core    = led_bits[1];

    logic [31:0] pio_awaddr, pio_wdata, pio_araddr, pio_rdata;
    logic [2:0]  pio_awprot, pio_arprot;
    logic [3:0]  pio_wstrb;
    logic [1:0]  pio_bresp, pio_rresp;
    logic        pio_awvalid, pio_awready, pio_wvalid, pio_wready, pio_bvalid, pio_bready;
    logic        pio_arvalid, pio_arready, pio_rvalid, pio_rready;

    asp_ctrl_plane u_ctrl (
        .clk(s_axi_aclk), .rst_n(s_axi_aresetn), .i_sys_timestamp(sys_timestamp),
        .s_tlp_tdata(ctrl_tdata), .s_tlp_tvalid(ctrl_tvalid), .s_tlp_tready(ctrl_tready),
        .m_cpl_tdata(cpl_tdata), .m_cpl_tvalid(cpl_tvalid), .m_cpl_tready(cpl_tready),
        .m_pio_awaddr(pio_awaddr), .m_pio_awprot(pio_awprot), .m_pio_awvalid(pio_awvalid),
        .m_pio_awready(pio_awready), .m_pio_wdata(pio_wdata), .m_pio_wstrb(pio_wstrb),
        .m_pio_wvalid(pio_wvalid), .m_pio_wready(pio_wready), .m_pio_bresp(pio_bresp),
        .m_pio_bvalid(pio_bvalid), .m_pio_bready(pio_bready),
        .m_pio_araddr(pio_araddr), .m_pio_arprot(pio_arprot), .m_pio_arvalid(pio_arvalid),
        .m_pio_arready(pio_arready), .m_pio_rdata(pio_rdata), .m_pio_rresp(pio_rresp),
        .m_pio_rvalid(pio_rvalid), .m_pio_rready(pio_rready),
        .o_led_bits(led_bits)
    );

    // ------------------------------------------------------------------------
    // PIO tile (pio2) and the shared pin map ([SPEC-TLP-07]); pio2_core with its own Zynq
    // adapter replaces this path later (pio2 [SPEC-ICN-11])
    // ------------------------------------------------------------------------
    logic [31:0] gpio_out, gpio_dir, gpio_in;

    asp_pio_pins #(.NUM_PWM_INPUTS(1)) u_pio_pins (
        .i_gpio_out(gpio_out), .i_gpio_pindir(gpio_dir), .o_gpio_in(gpio_in),
        .o_motor_pins(o_motor_pins), .o_neopixel_pin(o_neopixel),
        .o_uart_tx(o_uart_tx), .i_uart_rx(i_uart_rx),
        .o_imu_sclk(o_imu_sclk), .o_imu_cs_n(o_imu_cs_n), .o_imu_mosi(o_imu_mosi),
        .i_imu_miso(i_imu_miso), .i_imu_int(i_imu_int), .i_pwm_pins(1'b0)
    );

    asp_pio_wrap #(
        .BASE_ADDR(32'h40000700),
        .TILE_ID  (0)
    ) u_pio_tile (
        .clk             (s_axi_aclk),
        .rst_n           (s_axi_aresetn),
        .i_sys_timestamp (sys_timestamp),
        .i_gpio          (gpio_in),
        .o_gpio_out      (gpio_out),
        .o_gpio_pindir   (gpio_dir),
        .s_axil_awaddr(pio_awaddr), .s_axil_awprot(pio_awprot), .s_axil_awvalid(pio_awvalid),
        .s_axil_awready(pio_awready), .s_axil_wdata(pio_wdata), .s_axil_wstrb(pio_wstrb),
        .s_axil_wvalid(pio_wvalid), .s_axil_wready(pio_wready), .s_axil_bresp(pio_bresp),
        .s_axil_bvalid(pio_bvalid), .s_axil_bready(pio_bready),
        .s_axil_araddr(pio_araddr), .s_axil_arprot(pio_arprot), .s_axil_arvalid(pio_arvalid),
        .s_axil_arready(pio_arready), .s_axil_rdata(pio_rdata), .s_axil_rresp(pio_rresp),
        .s_axil_rvalid(pio_rvalid), .s_axil_rready(pio_rready),
        .s_tlp_tdata     (pio_in_tdata),
        .s_tlp_tvalid    (pio_in_tvalid),
        .s_tlp_tready    (pio_in_tready),
        .m_tlp_tdata     (pio_stream_tdata),
        .m_tlp_tvalid    (pio_stream_tvalid),
        .m_tlp_tready    (pio_stream_tready),
        .o_pio_irq       ()
    );

    // Logic Analyzer Debug Signals:
    // Debug 0: IMU DRDY Input
    // Debug 1: PIO stream TLP valid
    // Debug 2: FPGA TLP Egress Valid
    // Debug 3: IRQ_F2P[0] Asserted
    assign o_debug_pins[0] = i_imu_int;
    assign o_debug_pins[1] = pio_stream_tvalid;
    assign o_debug_pins[2] = tlp_tx_valid;
    assign o_debug_pins[3] = irq_f2p;

    /* verilator lint_off UNUSEDSIGNAL */
    wire _unused_inputs = &{1'b0, i_key_carrier};
    /* verilator lint_on UNUSEDSIGNAL */

endmodule
`default_nettype wire
