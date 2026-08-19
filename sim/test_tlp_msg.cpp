/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test and Verification for Multi-Target TLP Transport Abstractions
 */

#include "asp_tlp_msg.hpp"
#include <iostream>
#include <cassert>

using namespace abstractx;

int main() {
    std::cout << "=================================================================\n";
    std::cout << " AbstractX Multi-Target TLP Transport Verification (FPGA vs CPU)\n";
    std::cout << "=================================================================\n";

    // 1. Verify FPGA Wire Container (64 Bytes)
    std::cout << "[+] FPGA Wire Container (Tlp64 / 512-bit vector): " << sizeof(Tlp64) << " bytes\n";
    assert(sizeof(Tlp64) == 64);

    // 2. Verify Common Header (20 Bytes)
    std::cout << "[+] Universal TLP Header: " << sizeof(TlpHeader) << " bytes\n";
    assert(sizeof(TlpHeader) == 20);

    // 3. Verify Processor-Optimized Short TLP (24 Bytes)
    std::cout << "[+] Processor Short TLP (32-bit Reg R/W): " << sizeof(TlpShort) << " bytes\n";
    assert(sizeof(TlpShort) == 24);

    // 4. Verify Variable Payload Containers
    TlpVar<14> imu_msg{}; // Exact 14B IMU Burst (Accel[6] + Gyro[6] + Temp[2])
    std::cout << "[+] RP2350 / ESP32-P4 IMU Message Container: " << sizeof(imu_msg) << " bytes\n";
    assert(sizeof(imu_msg) == 34); // 20B Header + 14B Payload

    // 5. Verify Zero-Copy Pointer Descriptor for Linux SMP
    std::cout << "[+] Linux SMP Zero-Copy Descriptor: " << sizeof(TlpDescriptor) << " bytes\n";

    std::cout << "\n[SUCCESS] Multi-Target TLP Transport Abstractions Verified!\n";
    return 0;
}
