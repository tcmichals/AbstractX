module asp_spi_reg_bank #(
    parameter logic [7:0] P_ASP_VERSION = 8'h01
) (
    input  logic        i_clk,
    input  logic        i_rst_n,

    // SPI byte seam: SPI transport shim -> reg-bank
    input  logic [7:0]  i_spi_rx_data,
    input  logic        i_spi_rx_valid,
    output logic        o_spi_rx_ready,

    // SPI byte seam: reg-bank -> SPI transport shim
    output logic [7:0]  o_spi_tx_data,
    output logic        o_spi_tx_valid,
    input  logic        i_spi_tx_ready,

    // Transaction framing hint from shim (e.g., CS deassert)
    input  logic        i_spi_cs_end,

    // ASP ingress stream seam (host WRITE_DATA payload)
    output logic [7:0]  o_ing_tdata,
    output logic        o_ing_tvalid,
    input  logic        i_ing_tready,

    // ASP egress stream seam (host READ_DATA payload)
    input  logic [7:0]  i_egr_tdata,
    input  logic        i_egr_tvalid,
    output logic        o_egr_tready,

    // Status/flags from surrounding datapath
    input  logic [15:0] i_rx_len,
    input  logic        i_status_rx_overflow,
    input  logic        i_status_crc_err,
    input  logic        i_status_len_err,
    input  logic        i_status_resync_evt,

    // IRQ doorbell to host
    output logic        o_int_req
);

    localparam logic [7:0] ASP_CMD_WRITE_DATA  = 8'h80;
    localparam logic [7:0] ASP_CMD_READ_STATUS = 8'h01;
    localparam logic [7:0] ASP_CMD_READ_DATA   = 8'h02;

    typedef enum logic [2:0] {
        S_WAIT_CMD,
        S_WRITE_DATA,
        S_READ_STATUS_B0,
        S_READ_STATUS_B1,
        S_READ_STATUS_B2,
        S_READ_STATUS_B3,
        S_READ_DATA
    } state_t;

    state_t state_q, state_d;

    logic [7:0] cmd_q, cmd_d;
    logic [7:0] status_byte;

    // Status bit layout baseline
    // [0]=RX_READY, [1]=RX_OVERFLOW, [2]=CRC_ERR, [3]=LEN_ERR, [4]=TX_ACCEPT, [5]=RESYNC_EVT
    always_comb begin
        status_byte       = 8'h00;
        status_byte[0]    = (i_rx_len != 16'h0000);
        status_byte[1]    = i_status_rx_overflow;
        status_byte[2]    = i_status_crc_err;
        status_byte[3]    = i_status_len_err;
        status_byte[4]    = i_ing_tready;
        status_byte[5]    = i_status_resync_evt;
    end

    always_comb begin
        // defaults
        state_d       = state_q;
        cmd_d         = cmd_q;

        o_spi_rx_ready = 1'b0;
        o_spi_tx_data  = 8'h00;
        o_spi_tx_valid = 1'b0;

        o_ing_tdata  = i_spi_rx_data;
        o_ing_tvalid = 1'b0;

        o_egr_tready = 1'b0;

        // IRQ is level-style: high while readable bytes exist
        o_int_req = (i_rx_len != 16'h0000);

        case (state_q)
            S_WAIT_CMD: begin
                o_spi_rx_ready = 1'b1;
                if (i_spi_rx_valid) begin
                    cmd_d = i_spi_rx_data;
                    unique case (i_spi_rx_data)
                        ASP_CMD_WRITE_DATA:  state_d = S_WRITE_DATA;
                        ASP_CMD_READ_STATUS: state_d = S_READ_STATUS_B0;
                        ASP_CMD_READ_DATA:   state_d = S_READ_DATA;
                        default:             state_d = S_WAIT_CMD;
                    endcase
                end
            end

            S_WRITE_DATA: begin
                // pass-through write payload into ingress stream seam
                o_spi_rx_ready = i_ing_tready;
                o_ing_tvalid   = i_spi_rx_valid;

                // end transaction on CS boundary
                if (i_spi_cs_end)
                    state_d = S_WAIT_CMD;
            end

            S_READ_STATUS_B0: begin
                o_spi_tx_data  = P_ASP_VERSION;
                o_spi_tx_valid = 1'b1;
                if (i_spi_tx_ready)
                    state_d = S_READ_STATUS_B1;
            end

            S_READ_STATUS_B1: begin
                o_spi_tx_data  = status_byte;
                o_spi_tx_valid = 1'b1;
                if (i_spi_tx_ready)
                    state_d = S_READ_STATUS_B2;
            end

            S_READ_STATUS_B2: begin
                o_spi_tx_data  = i_rx_len[15:8];
                o_spi_tx_valid = 1'b1;
                if (i_spi_tx_ready)
                    state_d = S_READ_STATUS_B3;
            end

            S_READ_STATUS_B3: begin
                o_spi_tx_data  = i_rx_len[7:0];
                o_spi_tx_valid = 1'b1;
                if (i_spi_tx_ready)
                    state_d = S_WAIT_CMD;
            end

            S_READ_DATA: begin
                // stream egress bytes out over SPI
                o_spi_tx_data  = i_egr_tdata;
                o_spi_tx_valid = i_egr_tvalid;
                o_egr_tready   = i_spi_tx_ready;

                // end transaction on CS boundary
                if (i_spi_cs_end)
                    state_d = S_WAIT_CMD;
            end

            default: begin
                state_d = S_WAIT_CMD;
            end
        endcase
    end

    always_ff @(posedge i_clk or negedge i_rst_n) begin
        if (!i_rst_n) begin
            state_q <= S_WAIT_CMD;
            cmd_q   <= 8'h00;
        end else begin
            state_q <= state_d;
            cmd_q   <= cmd_d;

            // force resync at transaction boundary
            if (i_spi_cs_end) begin
                state_q <= S_WAIT_CMD;
                cmd_q   <= 8'h00;
            end
        end
    end

endmodule
