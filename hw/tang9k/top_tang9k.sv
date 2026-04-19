`default_nettype none

module top_tang9k (
    input  wire  i_clk,       // 27MHz
    input  wire  i_reset_n,   // Button S2
    
    input  wire  i_spi_sclk,
    input  wire  i_spi_cs_n,
    input  wire  i_spi_mosi,
    output wire  o_spi_miso,
    
    output wire [5:0] o_led   // 6 LEDs on Tang Nano 9K
);

    wire clk_logic;
    wire lock;

    // Use a PLL to get ~100MHz from 27MHz
    // GW1NR-LV9QN88PC6/I5
    // For now, let's just use the 27MHz clock directly or a simple divider
    // to prove the pipeline works, but 100MHz is better for performance.
    
    // Simple bypass for now, or I can generate a PLL module.
    // Given I don't have the gowin_pll config yet, I'll use i_clk.
    assign clk_logic = i_clk;
    assign lock = 1'b1;

    wire rst_n = i_reset_n & lock;

    asp_top u_asp_top (
        .clk        (clk_logic),
        .rst_n      (rst_n),
        .spi_sclk   (i_spi_sclk),
        .spi_cs_n   (i_spi_cs_n),
        .spi_mosi   (i_spi_mosi),
        .spi_miso   (o_spi_miso),
        .int_req    ()
    );

    // Map some internal state to LEDs for visibility
    assign o_led[0] = ~rst_n;
    assign o_led[1] = i_spi_cs_n;
    assign o_led[2] = i_spi_sclk;
    assign o_led[3] = 1'b1; // Power on
    assign o_led[4] = 1'b1;
    assign o_led[5] = 1'b1;

endmodule
