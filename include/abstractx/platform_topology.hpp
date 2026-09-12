/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Platform Topology & Silicon Interconnect Descriptors
 * ---------------------------------------------------------------
 * Standardized compile-time and runtime descriptor tables defining:
 * 1. Silicon Architecture (Linux SITL, Linux+E907, Linux+E907+FPGA, RP2350, ESP32-P4)
 * 2. Processing Cores & Roles (Host ARM64, Coprocessor E907, Coroutine Runner, FPGA)
 * 3. Transport Interconnects (Shared SRAM + MsgBox, SIO FIFO, PCIe, UDP)
 * 4. Synthesized Hardware Accelerators (Auto-DMA, DShot, NeoPixel)
 */

#pragma once

#include <cstdint>
#include <string_view>
#include <etl/array.h>

namespace abstractx::topology {

// 1. Top-Level Platform Silicon Architecture
enum class PlatformArch : uint8_t {
    Linux_Standard_SITL    = 0x01, // Pure host Linux with POSIX I/O workers
    Linux_Host_E907        = 0x02, // Linux host + XuanTie E907 coprocessor (Shared SRAM)
    Linux_Host_E907_FPGA   = 0x03, // Linux host + XuanTie E907 + FPGA fabric
    Linux_Host_FPGA_Direct = 0x04, // Linux host + Direct PCIe/SPI FPGA switch fabric
    RP2350_DualCore_Pico2W = 0x05, // Raspberry Pi Pico 2 W (RP2350 Dual-Core + CYW43)
    ESP32P4_FreeRTOS       = 0x06, // Espressif ESP32-P4 (Dual RISC-V 400MHz + FreeRTOS)
    Zynq_PS_PL             = 0x07  // Xilinx Zynq-7020 PS (ARM) + PL (FPGA)
};

// 2. Processing Unit / Core Role
enum class CoreRole : uint8_t {
    Host_Linux_SMP         = 0x01, // High-level Linux flight supervisor / network
    Coprocessor_IO_Worker  = 0x02, // XuanTie E907 or RP2350 Core 0 dedicated to I/O & DMA
    Coroutine_Main_Runner  = 0x03, // Core executing the cooperative C++20 coroutine loop
    Hardware_FPGA_Engine   = 0x04, // Synthesized FPGA hardware state machine
    RTOS_Background_Task   = 0x05  // FreeRTOS task (Wi-Fi, Bluetooth, TCP/IP stack)
};

// 3. Transport Interconnect Type
enum class InterconnectType : uint8_t {
    None                   = 0x00,
    Shared_SRAM_MsgBox     = 0x01, // Allwinner Shared SRAM A3/C + sun6i-msgbox
    RP2350_SIO_HardwareFifo= 0x02, // RP2350 Core 0 <-> Core 1 SIO FIFO
    PCIe_Gen2_TLP          = 0x03, // PCIe Switch Fabric (64-byte TLP)
    HighSpeed_SPI_DMA      = 0x04, // SPI DMA bus with IRQ handshake
    Loopback_POSIX_IPC     = 0x05, // Linux eventfd + shm / domain socket
    UDP_Network_Stream     = 0x06  // UDP Port 9870 wireless/Ethernet stream
};

// 4. Synthesized or On-Chip Hardware Accelerators Mask
enum HardwareAccelMask : uint32_t {
    ACCEL_NONE             = 0,
    ACCEL_IMU_AUTO_DMA     = (1 << 0), // FPGA IMU Auto-DMA IP core
    ACCEL_DSHOT_4CH        = (1 << 1), // FPGA 4-Channel DShot300/600 IP core
    ACCEL_NEOPIXEL         = (1 << 2), // FPGA NeoPixel status LED core
    ACCEL_SUN6I_MSGBOX     = (1 << 3), // Allwinner hardware mailbox doorbells
    ACCEL_CYW43_WIFI_PIO   = (1 << 4), // RP2350 PIO CYW43 Wi-Fi offloader
    ACCEL_FREERTOS_HOOKS   = (1 << 5)  // FreeRTOS runtime stats instrumentation
};

// 5. Individual Core Descriptor
struct CoreDescriptor {
    uint8_t     core_id{0};
    CoreRole    role{CoreRole::Coroutine_Main_Runner};
    const char* name{"MainCore"};
    uint32_t    nominal_clock_mhz{0};
};

// 6. Complete Master Platform Topology Table
struct PlatformTopologyTable {
    PlatformArch       arch{PlatformArch::Linux_Standard_SITL};
    const char*        platform_name{"AbstractX Generic"};
    const char*        board_model{"Unknown Board"};
    InterconnectType   primary_transport{InterconnectType::UDP_Network_Stream};
    uint32_t           accel_mask{ACCEL_NONE};
    
    // Core allocation (max 4 cores in static embedded table)
    uint8_t            core_count{1};
    etl::array<CoreDescriptor, 4> cores{};

    // Inter-domain memory configuration
    uintptr_t          shared_sram_base{0};
    uint32_t           shared_sram_size{0};
    uint16_t           spsc_ring_capacity{64};
};

// String helper
constexpr std::string_view to_string(PlatformArch arch) noexcept {
    switch (arch) {
        case PlatformArch::Linux_Standard_SITL:    return "Linux_Standard_SITL";
        case PlatformArch::Linux_Host_E907:        return "Linux_Host_E907";
        case PlatformArch::Linux_Host_E907_FPGA:   return "Linux_Host_E907_FPGA";
        case PlatformArch::Linux_Host_FPGA_Direct: return "Linux_Host_FPGA_Direct";
        case PlatformArch::RP2350_DualCore_Pico2W: return "RP2350_DualCore_Pico2W";
        case PlatformArch::ESP32P4_FreeRTOS:       return "ESP32P4_FreeRTOS";
        case PlatformArch::Zynq_PS_PL:             return "Zynq_PS_PL";
        default:                                   return "Unknown";
    }
}

} // namespace abstractx::topology
