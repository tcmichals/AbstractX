// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later

`timescale 1ns/1ps

module test_asp_top_tb;

    logic clk;
    logic rst_n;

    // Physical SPI Pins
    logic spi_sclk;
    logic spi_cs_n;
    wire  spi_io0;
    wire  spi_io1;

    logic spi_io0_dir; // 1 = output, 0 = input
    logic spi_io1_dir;
    logic spi_io0_out;
    logic spi_io1_out;

    assign spi_io0 = spi_io0_dir ? spi_io0_out : 1'bZ;
    assign spi_io1 = spi_io1_dir ? spi_io1_out : 1'bZ;

    // External IMU Pins
    wire  imu_sclk;
    wire  imu_cs_n;
    wire  imu_mosi;
    logic imu_miso;
    logic imu_int_i;

    // Doorbell IRQ Output
    wire  o_int_req;

    // Instantiate DUT
    asp_top #(
        .DUAL_SPI_ENABLE(1'b1)
    ) u_dut (
        .clk       (clk),
        .rst_n     (rst_n),
        .spi_sclk  (spi_sclk),
        .spi_cs_n  (spi_cs_n),
        .spi_io0   (spi_io0),
        .spi_io1   (spi_io1),
        .imu_sclk  (imu_sclk),
        .imu_cs_n  (imu_cs_n),
        .imu_mosi  (imu_mosi),
        .imu_miso  (imu_miso),
        .imu_int_i (imu_int_i),
        .o_int_req (o_int_req)
    );

    // 100 MHz Clock Generation
    always #5 clk = ~clk;

    // Task to transmit Dual-SPI Command & Payload (2 bits per SCLK cycle)
    task send_dual_spi(input [7:0] cmd, input [511:0] tlp_data, input integer send_payload);
        integer i;
        begin
            spi_cs_n = 1'b0;
            spi_io0_dir = 1'b1;
            spi_io1_dir = 1'b0;
            #20;

            // Send Command Byte over IO0 (8 SCLK pulses)
            for (i = 7; i >= 0; i = i - 1) begin
                spi_io0_out = cmd[i];
                #10;
                spi_sclk = 1'b1;
                #10;
                spi_sclk = 1'b0;
            end

            // If sending payload (Command 0xA1), transmit 512 bits via Dual-SPI (256 SCLK pulses)
            if (send_payload) begin
                spi_io0_dir = 1'b1;
                spi_io1_dir = 1'b1;
                for (i = 255; i >= 0; i = i - 1) begin
                    spi_io0_out = tlp_data[i*2];
                    spi_io1_out = tlp_data[i*2+1];
                    #10;
                    spi_sclk = 1'b1;
                    #10;
                    spi_sclk = 1'b0;
                end
            end

            #20;
            spi_cs_n = 1'b1;
            spi_io0_dir = 1'b0;
            spi_io1_dir = 1'b0;
            #50;
        end
    endtask

    // Task to clock out a 64-byte TLP from FPGA over Dual-SPI (Command 0xA2)
    task read_dual_spi(input [7:0] cmd);
        integer i;
        begin
            spi_cs_n = 1'b0;
            spi_io0_dir = 1'b1;
            spi_io1_dir = 1'b0;
            #20;

            // Send Command Byte 0xA2 over IO0 (8 SCLK pulses)
            for (i = 7; i >= 0; i = i - 1) begin
                spi_io0_out = cmd[i];
                #10;
                spi_sclk = 1'b1;
                #10;
                spi_sclk = 1'b0;
            end

            // Switch host pins to input and clock 256 Dual-SPI pulses (64 bytes)
            spi_io0_dir = 1'b0;
            spi_io1_dir = 1'b0;
            for (i = 0; i < 256; i = i + 1) begin
                #10;
                spi_sclk = 1'b1;
                #10;
                spi_sclk = 1'b0;
            end

            #20;
            spi_cs_n = 1'b1;
            #50;
        end
    endtask

    initial begin
        $display("------------------------------------------------------------");
        $display("Starting AbstractX 64-Byte TLP Dual-SPI SystemVerilog Testbench");
        $display("------------------------------------------------------------");

        clk = 0;
        rst_n = 0;
        spi_sclk = 0;
        spi_cs_n = 1;
        spi_io0_dir = 0;
        spi_io1_dir = 0;
        spi_io0_out = 0;
        spi_io1_out = 0;
        imu_miso = 0;
        imu_int_i = 0;

        #100;
        rst_n = 1;
        #100;

        // 1. Send MemWr TLP: Write 0x00000001 to IMU_CTRL (0x40000100) to enable Auto-DMA
        $display("[TEST 1] Sending MemWr TLP over Dual-SPI (Enable IMU Auto-DMA)...");
        send_dual_spi(8'hA1, {
            8'h02, 8'h00, 8'h00, 8'h01, // DW0: MemWr, Tag=0, Ch=1 (Control)
            32'h40000100,               // DW1: Target Address (IMU_CTRL)
            16'd1, 16'd1,               // DW2: Length = 1 DW, Seq = 1
            64'd0,                      // DW3-4: Timestamp = 0
            32'h00000001,               // DW5: Payload (AUTO_DMA_EN = 1)
            288'd0,                     // DW6-14: Zero-padding
            32'hDEADBEEF                // DW15: CRC32
        }, 1);

        #200;

        // 2. Trigger IMU Hardware Interrupt (imu_int_i)
        $display("[TEST 2] Pulsing IMU Hardware Interrupt pin (imu_int_i)...");
        imu_int_i = 1;
        #40;
        imu_int_i = 0;

        #15000; // Allow FPGA SPI Master to execute read burst

        // 3. Verify Doorbell Interrupt Output (o_int_req)
        if (o_int_req === 1'b1) begin
            $display("[SUCCESS] Doorbell Interrupt (o_int_req) asserted HIGH on IMU Auto-DMA packet!");
        end else begin
            $display("[ERROR] Doorbell Interrupt (o_int_req) failed to assert!");
            $stop;
        end

        // 4. Host reads out the queued 64-byte IMU Auto-DMA TLP over Dual-SPI
        $display("[TEST 3] Reading out 64-Byte IMU Telemetry TLP over Dual-SPI (Command 0xA2)...");
        read_dual_spi(8'hA2);

        #200;

        // 5. Verify Doorbell Interrupt Output (o_int_req) deasserts LOW
        if (o_int_req === 1'b0) begin
            $display("[SUCCESS] Doorbell Interrupt (o_int_req) deasserted LOW after host read!");
        end else begin
            $display("[ERROR] Doorbell Interrupt (o_int_req) failed to deassert!");
            $stop;
        end

        $display("------------------------------------------------------------");
        $display("ALL SYSTEMVERILOG TESTBENCH VERIFICATION TESTS PASSED!");
        $display("------------------------------------------------------------");
        $finish;
    end

endmodule
