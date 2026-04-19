`default_nettype wire

// AbstractX SPI frontend
//
// Responsibility: convert SPI physical pins to a core-clock byte stream.
// This module intentionally avoids parsing semantics.
module asp_spi_frontend #(
    parameter int DATA_WIDTH = 8
) (
    input  wire                  clk,
    input  wire                  rst,

    // SPI pins
    input  wire                  i_sclk,
    input  wire                  i_cs_n,
    input  wire                  i_mosi,
    output logic                 o_miso,

    // Core-facing RX stream
    output logic [DATA_WIDTH-1:0] o_rx_byte,
    output logic                  o_rx_valid,
    input  wire                  i_rx_ready,

    // Core-facing TX stream
    input  wire [DATA_WIDTH-1:0] i_tx_byte,
    input  wire                  i_tx_valid,
    output wire                  o_tx_ready,

    output logic                 o_busy
);
    // SPI mode-0 style behavior (CPOL=0, CPHA=0):
    // - sample MOSI on SCLK rising edge
    // - shift MISO on SCLK falling edge
    // Inputs are synchronized to `clk` for robust edge detection.

    logic [2:0] cs_sync;
    logic [2:0] sclk_sync;
    logic [1:0] mosi_sync;

    logic       cs_active;
    logic       cs_fall;
    logic       cs_rise;
    logic       sclk_rise;
    logic       sclk_fall;

    logic [2:0] bit_cnt;
    logic [7:0] rx_shift;
    logic [7:0] tx_shift;

    logic [7:0] tx_hold;
    logic       tx_hold_valid;

    logic [7:0] rx_byte_pending;
    logic       rx_valid_pending;

    assign cs_active = ~cs_sync[2];
    assign cs_fall   = (cs_sync[2:1] == 2'b10);
    assign cs_rise   = (cs_sync[2:1] == 2'b01);
    assign sclk_rise = (sclk_sync[2:1] == 2'b01);
    assign sclk_fall = (sclk_sync[2:1] == 2'b10);

    assign o_tx_ready = ~tx_hold_valid;

    always_ff @(posedge clk) begin
        if (rst) begin
            cs_sync   <= 3'b111;
            sclk_sync <= 3'b000;
            mosi_sync <= 2'b00;
            o_busy    <= 1'b0;
            o_rx_byte <= '0;
            o_rx_valid <= 1'b0;
            o_miso <= 1'b0;
            bit_cnt <= 3'd0;
            rx_shift <= 8'h00;
            tx_shift <= 8'h00;
            tx_hold <= 8'h00;
            tx_hold_valid <= 1'b0;
            rx_byte_pending <= 8'h00;
            rx_valid_pending <= 1'b0;
        end else begin
            // Synchronize async SPI inputs to core clock.
            cs_sync <= {cs_sync[1:0], i_cs_n};
            sclk_sync <= {sclk_sync[1:0], i_sclk};
            mosi_sync <= {mosi_sync[0], i_mosi};

            o_busy <= cs_active;

            // Host-facing RX handshake register.
            if (o_rx_valid && i_rx_ready) begin
                o_rx_valid <= 1'b0;
            end

            // Queue one TX byte from core when available.
            if (i_tx_valid && o_tx_ready) begin
                tx_hold <= i_tx_byte;
                tx_hold_valid <= 1'b1;
            end

            // When CS deasserts, reset bit-level state for next transfer.
            if (cs_rise) begin
                bit_cnt <= 3'd0;
                rx_shift <= 8'h00;
                tx_shift <= 8'h00;
                o_miso <= 1'b0;
            end

            // On CS assert, prime MISO with first bit of queued TX byte.
            if (cs_fall) begin
                bit_cnt <= 3'd0;
                rx_shift <= 8'h00;
                if (tx_hold_valid) begin
                    tx_shift <= tx_hold;
                    tx_hold_valid <= 1'b0;
                    o_miso <= tx_hold[7];
                end else begin
                    tx_shift <= 8'h00;
                    o_miso <= 1'b0;
                end
            end

            if (cs_active) begin
                // Sample MOSI on rising edge.
                if (sclk_rise) begin
                    rx_shift <= {rx_shift[6:0], mosi_sync[1]};

                    if (bit_cnt == 3'd7) begin
                        rx_byte_pending  <= {rx_shift[6:0], mosi_sync[1]};
                        rx_valid_pending <= 1'b1;
                        bit_cnt <= 3'd0;
                    end else begin
                        bit_cnt <= bit_cnt + 3'd1;
                    end
                end

                // Shift MISO on falling edge.
                if (sclk_fall) begin
                    tx_shift <= {tx_shift[6:0], 1'b0};
                    o_miso <= tx_shift[6];

                    // On byte boundary, load next transmit byte if queued.
                    if (bit_cnt == 3'd0) begin
                        if (tx_hold_valid) begin
                            tx_shift <= tx_hold;
                            tx_hold_valid <= 1'b0;
                            o_miso <= tx_hold[7];
                        end
                    end
                end
            end

            // Publish completed RX byte when output register is available.
            if (rx_valid_pending && !o_rx_valid) begin
                o_rx_byte <= rx_byte_pending;
                o_rx_valid <= 1'b1;
                rx_valid_pending <= 1'b0;
            end
        end
    end
endmodule

`default_nettype wire
