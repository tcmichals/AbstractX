// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`default_nettype wire

module asp_axis_fifo #(
    parameter int DEPTH_LOG2 = 8 // 256 bytes
)(
    input  wire        clk,
    input  wire        rst_n,
    
    // Ingress (Slave) Port
    input  wire [7:0]  s_axis_tdata,
    input  wire        s_axis_tvalid,
    input  wire        s_axis_tlast,
    output wire        s_axis_tready,
    
    // Egress (Master) Port
    output wire [7:0]  m_axis_tdata,
    output wire        m_axis_tvalid,
    output wire        m_axis_tlast,
    input  wire        m_axis_tready,
    
    // Status
    output wire [15:0] data_count
);

    localparam DEPTH = 1 << DEPTH_LOG2;
    
    // Memory arrays
    logic [7:0] mem_data [DEPTH-1:0];
    logic       mem_last [DEPTH-1:0];
    
    // Pointers
    logic [DEPTH_LOG2:0] wr_ptr;
    logic [DEPTH_LOG2:0] rd_ptr;
    
    logic full;
    logic empty;
    
    assign full  = (wr_ptr[DEPTH_LOG2] != rd_ptr[DEPTH_LOG2]) && 
                   (wr_ptr[DEPTH_LOG2-1:0] == rd_ptr[DEPTH_LOG2-1:0]);
    assign empty = (wr_ptr == rd_ptr);
    
    assign s_axis_tready = ~full;
    assign m_axis_tvalid = ~empty;
    
    logic [DEPTH_LOG2-1:0] wr_idx;
    logic [DEPTH_LOG2-1:0] rd_idx;
    
    assign wr_idx = wr_ptr[DEPTH_LOG2-1:0];
    assign rd_idx = rd_ptr[DEPTH_LOG2-1:0];
    
    // Read continuously asynchronous from array (since memory is small block RAM)
    assign m_axis_tdata = mem_data[rd_idx];
    assign m_axis_tlast = mem_last[rd_idx];
    
    logic [DEPTH_LOG2:0] diff_count;
    assign diff_count = wr_ptr - rd_ptr;
    assign data_count = 16'(diff_count);
    
    always_ff @(posedge clk) begin
        if (!rst_n) begin
            wr_ptr <= '0;
            rd_ptr <= '0;
        end else begin
            if (s_axis_tvalid && s_axis_tready) begin
                mem_data[wr_idx] <= s_axis_tdata;
                mem_last[wr_idx] <= s_axis_tlast;
                wr_ptr <= wr_ptr + 1'b1;
            end
            
            if (m_axis_tvalid && m_axis_tready) begin
                rd_ptr <= rd_ptr + 1'b1;
            end
        end
    end

endmodule
`default_nettype wire
