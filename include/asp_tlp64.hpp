/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX C++20 64-Byte PCIe-like TLP Strongly Typed Interfaces
 */

#ifndef ASP_TLP64_HPP
#define ASP_TLP64_HPP

#include "asp_tlp64.h"
#include <cstdint>
#include <cstddef>
#include <span>
#include <array>

namespace abstractx {

// Strongly typed TLP Operation Codes matching asp_tlp64.h
enum class TlpType : uint8_t {
    MemRead    = ASP_TLP_TYPE_MEM_RD,     // Host -> FPGA Memory Read (0x01)
    MemWrite   = ASP_TLP_TYPE_MEM_WR,     // Host -> FPGA Memory Write (0x02)
    Completion = ASP_TLP_TYPE_CPL_D,      // FPGA -> Host Completion with Data (0x03)
    Status     = ASP_TLP_TYPE_CPL,        // FPGA -> Host Completion Status (0x04)
    DmaStream  = ASP_TLP_TYPE_DMA_STREAM, // FPGA -> Host Autonomous Telemetry Stream (0x10)
    DmaConfig  = ASP_TLP_TYPE_DMA_CFG     // Host -> FPGA DMA Configuration (0x11)
};

// Strongly typed Channel / AXID Routing Planes
enum class Channel : uint8_t {
    Control   = ASP_CHANNEL_CONTROL,     // Wishbone Gateway (0x01)
    Telemetry = ASP_CHANNEL_TELEMETRY,   // IMU Auto-DMA Stream (0x02)
    FlightLog = ASP_CHANNEL_FC_LOG,      // Flight Log Stream (0x03)
    Debug     = ASP_CHANNEL_DEBUG_TRACE, // Debug Trace (0x04)
    EscSerial = ASP_CHANNEL_ESC_SERIAL   // UART ESC Serial Tunnel (0x05)
};

constexpr bool operator==(TlpType t, uint8_t u) noexcept { return static_cast<uint8_t>(t) == u; }
constexpr bool operator==(uint8_t u, TlpType t) noexcept { return u == static_cast<uint8_t>(t); }
constexpr bool operator!=(TlpType t, uint8_t u) noexcept { return static_cast<uint8_t>(t) != u; }
constexpr bool operator!=(uint8_t u, TlpType t) noexcept { return u != static_cast<uint8_t>(t); }

constexpr bool operator==(Channel c, uint8_t u) noexcept { return static_cast<uint8_t>(c) == u; }
constexpr bool operator==(uint8_t u, Channel c) noexcept { return u == static_cast<uint8_t>(c); }
constexpr bool operator!=(Channel c, uint8_t u) noexcept { return static_cast<uint8_t>(c) != u; }
constexpr bool operator!=(uint8_t u, Channel c) noexcept { return u != static_cast<uint8_t>(c); }

// Wire format alias
using TlpWire64 = asp_tlp64_t;

// C++20 Wrapper class around C asp_tlp64_t structure
struct alignas(64) Tlp64 {
    asp_tlp64_t wire;

    constexpr Tlp64() noexcept : wire{} {}

    // Construct a Memory Read TLP
    static constexpr Tlp64 make_mem_read(uint32_t addr, uint8_t tag = 0u) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemRead);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(Channel::Control);
        packet.wire.target_address = addr;
        packet.wire.length_dw = 1;
        return packet;
    }

    // Construct a Memory Write TLP
    static constexpr Tlp64 make_mem_write(uint32_t addr, uint32_t value, uint8_t tag = 0u) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(Channel::Control);
        packet.wire.target_address = addr;
        packet.wire.length_dw = 1;
        packet.wire.payload[0] = static_cast<uint8_t>(value >> 24);
        packet.wire.payload[1] = static_cast<uint8_t>(value >> 16);
        packet.wire.payload[2] = static_cast<uint8_t>(value >> 8);
        packet.wire.payload[3] = static_cast<uint8_t>(value & 0xFF);
        return packet;
    }

    constexpr TlpType type() const noexcept {
        return static_cast<TlpType>(wire.type);
    }

    constexpr Channel channel() const noexcept {
        return static_cast<Channel>(wire.channel);
    }

    constexpr uint8_t tag() const noexcept {
        return wire.tag;
    }

    constexpr uint16_t length() const noexcept {
        return wire.length_dw;
    }

    constexpr uint32_t target_address() const noexcept {
        return wire.target_address;
    }

    constexpr uint64_t timestamp_ns() const noexcept {
        return wire.timestamp_ns;
    }

    constexpr std::span<const uint8_t> payload() const noexcept {
        return std::span<const uint8_t>{wire.payload, ASP_TLP64_PAYLOAD_SIZE};
    }
};

// Static alignment & size verification
static_assert(sizeof(asp_tlp64_t) == 64, "asp_tlp64_t MUST be exactly 64 bytes");
static_assert(sizeof(Tlp64) == 64, "Tlp64 wrapper MUST be exactly 64 bytes");

} // namespace abstractx

#endif // ASP_TLP64_HPP
