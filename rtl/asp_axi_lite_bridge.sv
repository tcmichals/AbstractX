// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

// AbstractX AXI4-Lite to 64-Byte TLP Bridge
//
// Bridges Zynq PS M_AXI_GP0 (AXI4-Lite) to the 512-bit (64-byte) ASP switch fabric.
// Exposes CSRs, Ingress/Egress packet FIFOs, and asserts IRQ_F2P[0] for UIO wakeups.
//
// @impl [SPEC-ZYNQ-01] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
// @impl [SPEC-ZYNQ-02] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
// @impl [SPEC-ZYNQ-PLATFORM-09] hw/zynq7000/SPECIFICATION.md
module asp_axi_lite_bridge #(
    parameter integer C_S_AXI_DATA_WIDTH = 32,
    parameter integer C_S_AXI_ADDR_WIDTH = 16,
    parameter integer FIFO_DEPTH         = 128   // 128 slots x 64 bytes = 8 KiB per direction
)(
    input  wire                              s_axi_aclk,
    input  wire                              s_axi_aresetn,

    // AXI4-Lite Write Address Channel
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

    // PS Interrupt Line to GIC
    output logic                             irq_f2p,

    // 512-bit (64-byte) Ingress TLP Stream (to asp_router.s_tlp_*)
    output logic [511:0]                     m_tlp_tdata,
    output logic                             m_tlp_tvalid,
    input  wire                              m_tlp_tready,

    // 512-bit (64-byte) Egress TLP Stream (from asp_router.m_egr_*)
    input  wire [511:0]                      s_egr_tdata,
    input  wire                              s_egr_tvalid,
    output logic                             s_egr_tready
);

    // Register Offsets (Byte-Addressed)
    localparam [7:0] REG_CONTROL       = 8'h00;
    localparam [7:0] REG_STATUS        = 8'h04;
    localparam [7:0] REG_IRQ_STATUS    = 8'h08;
    localparam [7:0] REG_IRQ_ENABLE    = 8'h0C;
    localparam [7:0] REG_TLP_IN_PORT   = 8'h10;
    localparam [7:0] REG_TLP_OUT_PORT  = 8'h14;
    localparam [7:0] REG_EGR_COUNT     = 8'h18;
    localparam [7:0] REG_ING_FREE      = 8'h1C;
    localparam [7:0] REG_HARDWARE_ID   = 8'h40;

    localparam [31:0] HARDWARE_MAGIC   = 32'h41535036; // "ASP6"

    // Internal Control Registers
    logic [31:0] reg_control;
    logic [31:0] reg_irq_enable;
    logic [31:0] reg_irq_status;

    // Ingress 64B Assembly Buffer (16 DWORDs)
    logic [31:0] ing_buf [0:15];
    logic [3:0]  ing_word_idx;

    // Egress 64B Disassembly Buffer (16 DWORDs)
    logic [31:0] egr_buf [0:15];
    logic [3:0]  egr_word_idx;
    logic        egr_buf_valid;

    // Simple Ingress FIFO (Depth = FIFO_DEPTH packets)
    logic [511:0] ing_fifo_mem [0:FIFO_DEPTH-1];
    logic [$clog2(FIFO_DEPTH):0] ing_wr_ptr;
    logic [$clog2(FIFO_DEPTH):0] ing_rd_ptr;
    logic [$clog2(FIFO_DEPTH):0] ing_count;
    wire [31:0] ing_count_u32 = {{(31-$clog2(FIFO_DEPTH)){1'b0}}, ing_count};
    wire [31:0] fifo_depth_u32 = FIFO_DEPTH;

    assign ing_count = ing_wr_ptr - ing_rd_ptr;
    wire ing_fifo_full  = (ing_count_u32 == fifo_depth_u32);
    wire ing_fifo_empty = (ing_count == 0);

    // Simple Egress FIFO (Depth = FIFO_DEPTH packets)
    logic [511:0] egr_fifo_mem [0:FIFO_DEPTH-1];
    logic [$clog2(FIFO_DEPTH):0] egr_wr_ptr;
    logic [$clog2(FIFO_DEPTH):0] egr_rd_ptr;
    logic [$clog2(FIFO_DEPTH):0] egr_count;
    wire [31:0] egr_count_u32 = {{(31-$clog2(FIFO_DEPTH)){1'b0}}, egr_count};

    assign egr_count = egr_wr_ptr - egr_rd_ptr;
    wire egr_fifo_full  = (egr_count_u32 == fifo_depth_u32);
    wire egr_fifo_empty = (egr_count == 0);

    // Push from Router into Egress FIFO
    assign s_egr_tready = !egr_fifo_full;
    always_ff @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) begin
            egr_wr_ptr <= '0;
        end else if (s_egr_tvalid && s_egr_tready) begin
            egr_fifo_mem[egr_wr_ptr[$clog2(FIFO_DEPTH)-1:0]] <= s_egr_tdata;
            egr_wr_ptr <= egr_wr_ptr + 1'b1;
        end
    end

    // Pull from Ingress FIFO out to Router
    assign m_tlp_tvalid = !ing_fifo_empty;
    assign m_tlp_tdata  = ing_fifo_mem[ing_rd_ptr[$clog2(FIFO_DEPTH)-1:0]];
    always_ff @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) begin
            ing_rd_ptr <= '0;
        end else if (m_tlp_tvalid && m_tlp_tready) begin
            ing_rd_ptr <= ing_rd_ptr + 1'b1;
        end
    end

    // Egress Buffer Latching (Pop next TLP into egr_buf when word 15 is read)
    wire [511:0] next_egr_tlp = egr_fifo_mem[egr_rd_ptr[$clog2(FIFO_DEPTH)-1:0]];
    always_ff @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) begin
            egr_rd_ptr    <= '0;
            egr_buf_valid <= 1'b0;
            for (int i = 0; i < 16; i++) egr_buf[i] <= '0;
        end else begin
            if (!egr_buf_valid && !egr_fifo_empty) begin
                for (int i = 0; i < 16; i++) begin
                    egr_buf[i] <= next_egr_tlp[i*32 +: 32];
                end
                egr_rd_ptr    <= egr_rd_ptr + 1'b1;
                egr_buf_valid <= 1'b1;
            end
        end
    end

    // Interrupt Generation: Assert IRQ when Egress FIFO is non-empty
    always_ff @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) begin
            irq_f2p        <= 1'b0;
            reg_irq_status <= '0;
        end else begin
            if (!egr_fifo_empty || egr_buf_valid) begin
                reg_irq_status[0] <= 1'b1;
            end
            irq_f2p <= reg_irq_status[0] && reg_irq_enable[0];
        end
    end

    // -------------------------------------------------------------------------
    // AXI4-Lite Slave Logic
    // -------------------------------------------------------------------------
    logic aw_en;
    logic [C_S_AXI_ADDR_WIDTH-1:0] axi_awaddr;
    logic [C_S_AXI_ADDR_WIDTH-1:0] axi_araddr;

    // AXI Write Address & Data Handshake
    always_ff @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) begin
            s_axi_awready <= 1'b0;
            s_axi_wready  <= 1'b0;
            s_axi_bvalid  <= 1'b0;
            s_axi_bresp   <= 2'b00;
            aw_en         <= 1'b1;
            axi_awaddr    <= '0;
            reg_control   <= 32'h00000001; // Enabled by default
            reg_irq_enable<= 32'h00000001; // IRQ enabled by default
            ing_word_idx  <= '0;
            ing_wr_ptr    <= '0;
            for (int i = 0; i < 16; i++) ing_buf[i] <= '0;
        end else begin
            // Address Write Ready
            if (!s_axi_awready && s_axi_awvalid && s_axi_wvalid && aw_en) begin
                s_axi_awready <= 1'b1;
                s_axi_wready  <= 1'b1;
                axi_awaddr    <= s_axi_awaddr;
                aw_en         <= 1'b0;
            end else begin
                s_axi_awready <= 1'b0;
                s_axi_wready  <= 1'b0;
            end

            // Write Execution
            if (s_axi_wready && s_axi_wvalid && s_axi_awready && s_axi_awvalid) begin
                s_axi_bvalid <= 1'b1;
                s_axi_bresp  <= 2'b00; // OKAY

                case (axi_awaddr[7:0])
                    REG_CONTROL:    reg_control    <= s_axi_wdata;
                    REG_IRQ_STATUS: reg_irq_status <= reg_irq_status & ~s_axi_wdata; // W1C
                    REG_IRQ_ENABLE: reg_irq_enable <= s_axi_wdata;
                    REG_TLP_IN_PORT: begin
                        ing_buf[ing_word_idx] <= s_axi_wdata;
                        if (ing_word_idx == 4'd15) begin
                            // Pack 16 DWORDs into 512-bit vector and commit to Ingress FIFO
                            if (!ing_fifo_full) begin
                                ing_fifo_mem[ing_wr_ptr[$clog2(FIFO_DEPTH)-1:0]] <= {
                                    s_axi_wdata,
                                    ing_buf[14], ing_buf[13], ing_buf[12],
                                    ing_buf[11], ing_buf[10], ing_buf[9], ing_buf[8],
                                    ing_buf[7], ing_buf[6], ing_buf[5], ing_buf[4],
                                    ing_buf[3], ing_buf[2], ing_buf[1], ing_buf[0]
                                };
                                ing_wr_ptr <= ing_wr_ptr + 1'b1;
                            end
                            ing_word_idx <= '0;
                        end else begin
                            ing_word_idx <= ing_word_idx + 1'b1;
                        end
                    end
                    default: ;
                endcase
            end

            // Write Response Acknowledge
            if (s_axi_bvalid && s_axi_bready) begin
                s_axi_bvalid <= 1'b0;
                aw_en        <= 1'b1;
            end
        end
    end

    // AXI Read Address & Data Handshake
    always_ff @(posedge s_axi_aclk or negedge s_axi_aresetn) begin
        if (!s_axi_aresetn) begin
            s_axi_arready <= 1'b0;
            s_axi_rvalid  <= 1'b0;
            s_axi_rresp   <= 2'b00;
            s_axi_rdata   <= '0;
            egr_word_idx  <= '0;
        end else begin
            if (!s_axi_arready && s_axi_arvalid) begin
                s_axi_arready <= 1'b1;
                axi_araddr    <= s_axi_araddr;
            end else begin
                s_axi_arready <= 1'b0;
            end

            // Read Execution
            if (s_axi_arready && s_axi_arvalid && !s_axi_rvalid) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp  <= 2'b00; // OKAY

                case (axi_araddr[7:0])
                    REG_CONTROL:     s_axi_rdata <= reg_control;
                    REG_STATUS:      s_axi_rdata <= {28'h0, ing_fifo_full, !ing_fifo_full, egr_fifo_full, (egr_buf_valid || !egr_fifo_empty)};
                    REG_IRQ_STATUS:  s_axi_rdata <= reg_irq_status;
                    REG_IRQ_ENABLE:  s_axi_rdata <= reg_irq_enable;
                    REG_EGR_COUNT:   s_axi_rdata <= {{(32-$clog2(FIFO_DEPTH)-1){1'b0}}, egr_count + egr_buf_valid};
                    REG_ING_FREE:    s_axi_rdata <= fifo_depth_u32 - ing_count_u32;
                    REG_HARDWARE_ID: s_axi_rdata <= HARDWARE_MAGIC;
                    REG_TLP_OUT_PORT: begin
                        if (egr_buf_valid) begin
                            s_axi_rdata <= egr_buf[egr_word_idx];
                            if (egr_word_idx == 4'd15) begin
                                egr_word_idx  <= '0;
                                egr_buf_valid <= 1'b0; // Consumed this 64B packet
                            end else begin
                                egr_word_idx <= egr_word_idx + 1'b1;
                            end
                        end else begin
                            s_axi_rdata <= 32'h00000000;
                        end
                    end
                    default: s_axi_rdata <= 32'h00000000;
                endcase
            end else if (s_axi_rvalid && s_axi_rready) begin
                s_axi_rvalid <= 1'b0;
            end
        end
    end

endmodule

`default_nettype wire
