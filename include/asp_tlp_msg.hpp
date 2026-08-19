/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Multi-Target TLP Transport Abstractions:
 * - FPGA Hardware Target: Fixed 64-byte wire container (Tlp64 / 512-bit vector)
 * - Processor Target (RP2350, ESP32-P4, STM32, Linux): Compact / Variable / Zero-Copy Descriptors
 */

#ifndef ASP_TLP_MSG_HPP
#define ASP_TLP_MSG_HPP

#include "asp_tlp64.hpp"
#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include <string_view>

namespace abstractx {

// Common 20-Byte TLP Header shared identically across FPGA & Processor targets
struct __attribute__((packed)) TlpHeader {
    uint8_t  type;            // TLP operation type (MemRd, MemWr, CplD, DMA_Stream)
    uint8_t  flags;           // Flags (ACK, Error, Stream ID)
    uint8_t  tag;             // Split-transaction correlation ID
    uint8_t  channel;         // Routing plane (Control, Telemetry, Log, ESC)
    uint32_t target_address;  // 32-bit Wishbone / Peripheral Base Address
    uint16_t length_dw;       // Payload length in 32-bit DWORDs (or bytes)
    uint16_t sequence;        // Sequence counter
    uint64_t timestamp_ns;    // 64-bit hardware or timer nanosecond timestamp

    constexpr TlpType get_type() const noexcept {
        return static_cast<TlpType>(type);
    }

    constexpr Channel get_channel() const noexcept {
        return static_cast<Channel>(channel);
    }
};

static_assert(sizeof(TlpHeader) == 20, "TlpHeader MUST be exactly 20 bytes");

// -----------------------------------------------------------------------------
// 1. Processor-Optimized Short TLP for 32-Bit Register R/W (24 Bytes Total)
// Ideal for RP2350 Core 0 <-> Core 1 SIO or ESP32-P4 Hardware Mailboxes
// -----------------------------------------------------------------------------
struct __attribute__((packed)) TlpShort {
    TlpHeader header;
    uint32_t  data; // 4-byte payload for single 32-bit register value

    static constexpr TlpShort make_read(uint32_t addr, uint8_t tag) noexcept {
        TlpShort msg{};
        msg.header.type = static_cast<uint8_t>(TlpType::MemRead);
        msg.header.tag = tag;
        msg.header.channel = static_cast<uint8_t>(Channel::Control);
        msg.header.target_address = addr;
        msg.header.length_dw = 1;
        msg.data = 0;
        return msg;
    }

    static constexpr TlpShort make_write(uint32_t addr, uint32_t value, uint8_t tag) noexcept {
        TlpShort msg{};
        msg.header.type = static_cast<uint8_t>(TlpType::MemWrite);
        msg.header.tag = tag;
        msg.header.channel = static_cast<uint8_t>(Channel::Control);
        msg.header.target_address = addr;
        msg.header.length_dw = 1;
        msg.data = value;
        return msg;
    }
};

static_assert(sizeof(TlpShort) == 24, "TlpShort MUST be exactly 24 bytes");

// -----------------------------------------------------------------------------
// 2. Processor-Optimized Variable-Payload TLP Container (Templated Capacity)
// Ideal for embedded SRAM conservation on Pico 2W / ESP32-P4
// -----------------------------------------------------------------------------
template <size_t MaxPayload = 16>
struct __attribute__((packed)) TlpVar {
    TlpHeader header;
    uint8_t   payload[MaxPayload];

    constexpr size_t total_size() const noexcept {
        return sizeof(TlpHeader) + (header.length_dw * 4);
    }

    constexpr std::span<const uint8_t> get_payload() const noexcept {
        size_t len = header.length_dw * 4;
        if (len > MaxPayload) len = MaxPayload;
        return std::span<const uint8_t>{payload, len};
    }
};

// -----------------------------------------------------------------------------
// 3. Zero-Copy Pointer Descriptor for Linux SMP & Large PSRAM Streams (ESP32-P4)
// Passes memory buffers without copying data bytes through the ring
// -----------------------------------------------------------------------------
struct TlpDescriptor {
    TlpHeader      header;
    const uint8_t* payload_ptr{nullptr};
    size_t         payload_len{0};
    void*          user_context{nullptr}; // e.g. custom buffer deallocator / pool handle

    constexpr std::span<const uint8_t> payload() const noexcept {
        if (!payload_ptr || payload_len == 0) return {};
        return std::span<const uint8_t>{payload_ptr, payload_len};
    }
};

} // namespace abstractx

#endif // ASP_TLP_MSG_HPP
