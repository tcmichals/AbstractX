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
#include <cstring>
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
    EscSerial = ASP_CHANNEL_ESC_SERIAL,  // UART ESC Serial Tunnel (0x05)
    SpiBridge = ASP_CHANNEL_SPI_BRIDGE,  // SPI Packet Transfer (0x06)
    I2cBridge = ASP_CHANNEL_I2C_BRIDGE,  // I2C Packet Transfer (0x07)
    GpioBridge = ASP_CHANNEL_GPIO_BRIDGE // GPIO / PIO Pin Control & Event Stream (0x08)
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

// @impl [SPEC-TLP-01] docs/DESIGN_SPECIFICATION.md#spec-tlp-01
// @status Complete
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

    // Construct a Completion with Data TLP
    static constexpr Tlp64 make_completion_data(uint8_t tag, uint32_t value, Channel ch = Channel::Telemetry) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::Completion);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.length_dw = 1;
        packet.wire.payload[0] = static_cast<uint8_t>(value >> 24);
        packet.wire.payload[1] = static_cast<uint8_t>(value >> 16);
        packet.wire.payload[2] = static_cast<uint8_t>(value >> 8);
        packet.wire.payload[3] = static_cast<uint8_t>(value & 0xFF);
        return packet;
    }

    static constexpr Tlp64 make_cpl_d(uint8_t tag, uint32_t value, Channel ch = Channel::Telemetry) noexcept {
        return make_completion_data(tag, value, ch);
    }

    // Construct an SPI Transfer TLP Request
    static Tlp64 make_spi_transfer(uint8_t bus_id, uint8_t cs_pin,
                                   std::span<const uint8_t> tx_data,
                                   uint8_t rx_len,
                                   uint8_t flags = ASP_SPI_FLAG_AUTO_CS,
                                   uint8_t tag = 0,
                                   Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = (rx_len > 0 && tx_data.empty()) ? static_cast<uint8_t>(TlpType::MemRead)
                                                           : static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.flags = flags;
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_SPI_BASE | ASP_REG_XFER;
        
        size_t copy_len = (tx_data.size() > 36) ? 36 : tx_data.size();
        packet.wire.length_dw = static_cast<uint16_t>(1 + (copy_len + 3) / 4);

        auto* req = reinterpret_cast<asp_tlp_spi_req_header_t*>(packet.wire.payload);
        req->bus_id = bus_id;
        req->cs_pin = cs_pin;
        req->tx_len = static_cast<uint8_t>(copy_len);
        req->rx_len = rx_len;

        if (copy_len > 0) {
            std::memcpy(packet.wire.payload + sizeof(asp_tlp_spi_req_header_t), tx_data.data(), copy_len);
        }
        return packet;
    }

    // Construct an SPI Completion TLP
    static Tlp64 make_spi_cpl(uint8_t tag, uint8_t status,
                              std::span<const uint8_t> rx_data,
                              uint16_t duration_us = 0,
                              uint64_t timestamp_ns = 0,
                              Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::Completion);
        packet.wire.flags = (status == 0) ? 0 : 1;
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_SPI_BASE | ASP_REG_XFER;
        packet.wire.timestamp_ns = timestamp_ns;

        size_t copy_len = (rx_data.size() > 36) ? 36 : rx_data.size();
        packet.wire.length_dw = static_cast<uint16_t>(1 + (copy_len + 3) / 4);

        auto* cpl = reinterpret_cast<asp_tlp_spi_cpl_header_t*>(packet.wire.payload);
        cpl->status = status;
        cpl->transferred_len = static_cast<uint8_t>(copy_len);
        cpl->bus_duration_us = duration_us;

        if (copy_len > 0) {
            std::memcpy(packet.wire.payload + sizeof(asp_tlp_spi_cpl_header_t), rx_data.data(), copy_len);
        }
        return packet;
    }

    // Construct an I2C Read Register TLP (Combined Write-then-Read / Repeated-Start)
    static Tlp64 make_i2c_read_reg(uint8_t bus_id, uint8_t slave_addr, uint8_t reg_offset,
                                   uint8_t rx_len, uint8_t tag = 0,
                                   Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemRead);
        packet.wire.flags = ASP_I2C_FLAG_USE_REG | ASP_I2C_FLAG_REPEATED_START;
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_I2C_BASE | ASP_REG_XFER;
        packet.wire.length_dw = static_cast<uint16_t>(2 + (rx_len + 3) / 4);

        auto* req = reinterpret_cast<asp_tlp_i2c_req_header_t*>(packet.wire.payload);
        req->bus_id = bus_id;
        req->slave_addr = slave_addr;
        req->reg_offset = reg_offset;
        req->sub_flags = ASP_I2C_FLAG_USE_REG | ASP_I2C_FLAG_REPEATED_START;
        req->tx_len = 0;
        req->rx_len = rx_len;
        return packet;
    }

    // Construct an I2C Write Register TLP
    static Tlp64 make_i2c_write_reg(uint8_t bus_id, uint8_t slave_addr, uint8_t reg_offset,
                                    std::span<const uint8_t> tx_data, uint8_t tag = 0,
                                    Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.flags = ASP_I2C_FLAG_USE_REG;
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_I2C_BASE | ASP_REG_XFER;

        size_t copy_len = (tx_data.size() > 32) ? 32 : tx_data.size();
        packet.wire.length_dw = static_cast<uint16_t>(2 + (copy_len + 3) / 4);

        auto* req = reinterpret_cast<asp_tlp_i2c_req_header_t*>(packet.wire.payload);
        req->bus_id = bus_id;
        req->slave_addr = slave_addr;
        req->reg_offset = reg_offset;
        req->sub_flags = ASP_I2C_FLAG_USE_REG;
        req->tx_len = static_cast<uint8_t>(copy_len);
        req->rx_len = 0;

        if (copy_len > 0) {
            std::memcpy(packet.wire.payload + sizeof(asp_tlp_i2c_req_header_t), tx_data.data(), copy_len);
        }
        return packet;
    }

    // Construct an I2C Completion TLP
    static Tlp64 make_i2c_cpl(uint8_t tag, uint8_t status,
                              std::span<const uint8_t> rx_data,
                              uint16_t duration_us = 0,
                              uint64_t timestamp_ns = 0,
                              Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::Completion);
        packet.wire.flags = (status == 0) ? 0 : 1;
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_I2C_BASE | ASP_REG_XFER;
        packet.wire.timestamp_ns = timestamp_ns;

        size_t copy_len = (rx_data.size() > 36) ? 36 : rx_data.size();
        packet.wire.length_dw = static_cast<uint16_t>(1 + (copy_len + 3) / 4);

        auto* cpl = reinterpret_cast<asp_tlp_i2c_cpl_header_t*>(packet.wire.payload);
        cpl->status = status;
        cpl->transferred_len = static_cast<uint8_t>(copy_len);
        cpl->bus_duration_us = duration_us;

        if (copy_len > 0) {
            std::memcpy(packet.wire.payload + sizeof(asp_tlp_i2c_cpl_header_t), rx_data.data(), copy_len);
        }
        return packet;
    }

    // Construct a GPIO Configuration TLP (Input/Output, Pull)
    static Tlp64 make_gpio_config(uint8_t pin, uint8_t mode, uint8_t pull = 0, uint8_t tag = 0,
                                  Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_GPIO_BASE | ASP_GPIO_REG_CONFIG;
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(asp_tlp_gpio_req_header_t) + 3) / 4);

        auto* req = reinterpret_cast<asp_tlp_gpio_req_header_t*>(packet.wire.payload);
        req->cmd = ASP_GPIO_CMD_CONFIG;
        req->pin = pin;
        req->mode = mode;
        req->pull_edge = pull;
        return packet;
    }

    // Construct an Atomic GPIO SET TLP (Drive High)
    static Tlp64 make_gpio_set(uint8_t pin, uint32_t mask = 0, uint8_t tag = 0,
                               Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_GPIO_BASE | ASP_GPIO_REG_SET;
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(asp_tlp_gpio_req_header_t) + 3) / 4);

        auto* req = reinterpret_cast<asp_tlp_gpio_req_header_t*>(packet.wire.payload);
        req->cmd = ASP_GPIO_CMD_SET;
        req->pin = pin;
        req->mask = mask ? mask : (1U << (pin % 32));
        req->value = 1;
        return packet;
    }

    // Construct an Atomic GPIO CLEAR TLP (Drive Low)
    static Tlp64 make_gpio_clr(uint8_t pin, uint32_t mask = 0, uint8_t tag = 0,
                               Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_GPIO_BASE | ASP_GPIO_REG_CLR;
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(asp_tlp_gpio_req_header_t) + 3) / 4);

        auto* req = reinterpret_cast<asp_tlp_gpio_req_header_t*>(packet.wire.payload);
        req->cmd = ASP_GPIO_CMD_CLR;
        req->pin = pin;
        req->mask = mask ? mask : (1U << (pin % 32));
        req->value = 0;
        return packet;
    }

    // Construct an Atomic GPIO XOR / Toggle TLP
    static Tlp64 make_gpio_xor(uint8_t pin, uint32_t mask = 0, uint8_t tag = 0,
                               Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_GPIO_BASE | ASP_GPIO_REG_XOR;
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(asp_tlp_gpio_req_header_t) + 3) / 4);

        auto* req = reinterpret_cast<asp_tlp_gpio_req_header_t*>(packet.wire.payload);
        req->cmd = ASP_GPIO_CMD_XOR;
        req->pin = pin;
        req->mask = mask ? mask : (1U << (pin % 32));
        return packet;
    }

    // Construct a GPIO READ TLP
    static Tlp64 make_gpio_read(uint8_t pin, uint8_t tag = 0,
                                Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemRead);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_GPIO_BASE | ASP_GPIO_REG_READ;
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(asp_tlp_gpio_req_header_t) + 3) / 4);

        auto* req = reinterpret_cast<asp_tlp_gpio_req_header_t*>(packet.wire.payload);
        req->cmd = ASP_GPIO_CMD_READ;
        req->pin = pin;
        return packet;
    }

    // Construct a GPIO Attach Edge ISR TLP (Pos / Neg / Both)
    static Tlp64 make_gpio_irq_attach(uint8_t pin, uint8_t edge, uint8_t tag = 0,
                                      Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_GPIO_BASE | ASP_GPIO_REG_IRQ_CFG;
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(asp_tlp_gpio_req_header_t) + 3) / 4);

        auto* req = reinterpret_cast<asp_tlp_gpio_req_header_t*>(packet.wire.payload);
        req->cmd = ASP_GPIO_CMD_IRQ_ATTACH;
        req->pin = pin;
        req->pull_edge = edge;
        return packet;
    }

    // Construct a GPIO Completion TLP
    static Tlp64 make_gpio_cpl(uint8_t tag, uint8_t status, uint8_t pin, uint8_t level,
                               uint8_t edge = 0, uint32_t mask_state = 0,
                               uint64_t timestamp_ns = 0,
                               Channel ch = Channel::Control) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::Completion);
        packet.wire.flags = (status == 0) ? 0 : 1;
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_GPIO_BASE | ASP_GPIO_REG_READ;
        packet.wire.timestamp_ns = timestamp_ns;
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(asp_tlp_gpio_cpl_header_t) + 3) / 4);

        auto* cpl = reinterpret_cast<asp_tlp_gpio_cpl_header_t*>(packet.wire.payload);
        cpl->status = status;
        cpl->pin = pin;
        cpl->level = level;
        cpl->edge_detected = edge;
        cpl->mask_state = mask_state;
        cpl->timestamp_ns = timestamp_ns;
        return packet;
    }

    // Construct an Asynchronous GPIO Edge Ingress Event TLP (DMA_Stream)
    static Tlp64 make_gpio_event(uint8_t pin, uint8_t level, uint8_t edge,
                                 uint64_t timestamp_ns, uint8_t tag = 0,
                                 Channel ch = Channel::GpioBridge) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.target_address = ASP_ADDR_GPIO_BASE | ASP_GPIO_REG_IRQ_STATUS;
        packet.wire.timestamp_ns = timestamp_ns;
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(asp_tlp_gpio_cpl_header_t) + 3) / 4);

        auto* cpl = reinterpret_cast<asp_tlp_gpio_cpl_header_t*>(packet.wire.payload);
        cpl->status = 0;
        cpl->pin = pin;
        cpl->level = level;
        cpl->edge_detected = edge;
        cpl->timestamp_ns = timestamp_ns;
        return packet;
    }

    // Construct a Hardware Sensor Fusion Setup TLP (Configure trigger, edge, bus, burst len)
    static Tlp64 make_hw_fusion_setup(const asp_tlp_auto_dma_cfg_t& cfg, uint8_t tag = 0) noexcept {
        Tlp64 packet{};
        packet.wire.type = ASP_TLP_TYPE_DMA_CFG;
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(Channel::Control);
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(asp_tlp_auto_dma_cfg_t) + 3) / 4);
        auto* p = reinterpret_cast<asp_tlp_auto_dma_cfg_t*>(packet.wire.payload);
        *p = cfg;
        p->cmd = ASP_DMA_CMD_SETUP;
        return packet;
    }

    // Construct a Hardware Sensor Fusion Mode Control TLP (Enable / Disable Auto Mode)
    static Tlp64 make_hw_fusion_control(uint8_t channel_id, bool enable, uint8_t tag = 0) noexcept {
        Tlp64 packet{};
        packet.wire.type = ASP_TLP_TYPE_DMA_CFG;
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(Channel::Control);
        packet.wire.length_dw = 1;
        auto* p = reinterpret_cast<asp_tlp_auto_dma_cfg_t*>(packet.wire.payload);
        p->cmd = enable ? ASP_DMA_CMD_ENABLE : ASP_DMA_CMD_DISABLE;
        p->channel_id = channel_id;
        return packet;
    }

    // Construct a Hardware Sensor Fusion Completion TLP
    static Tlp64 make_hw_fusion_cpl(uint8_t tag, uint8_t status, uint8_t cmd, uint8_t channel_id) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::Completion);
        packet.wire.flags = (status == ASP_STATUS_OK) ? 0 : 1;
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(Channel::Control);
        packet.wire.length_dw = 1;
        packet.wire.payload[0] = status;
        packet.wire.payload[1] = cmd;
        packet.wire.payload[2] = channel_id;
        return packet;
    }

    // Construct a generic Stream TX packet
    static Tlp64 make_stream_tx(Channel ch, uint16_t seq, std::span<const uint8_t> data, uint8_t tag = 0) noexcept {
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(TlpType::MemWrite);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.sequence = seq;
        size_t copy_len = (data.size() > ASP_TLP64_PAYLOAD_SIZE) ? ASP_TLP64_PAYLOAD_SIZE : data.size();
        packet.wire.length_dw = static_cast<uint16_t>((copy_len + 3) / 4);
        if (copy_len > 0) {
            std::memcpy(packet.wire.payload, data.data(), copy_len);
        }
        return packet;
    }

    constexpr bool is_error() const noexcept {
        return (wire.flags != 0);
    }

    constexpr size_t length_bytes() const noexcept {
        size_t bytes = static_cast<size_t>(wire.length_dw) * 4;
        return (bytes > ASP_TLP64_PAYLOAD_SIZE) ? ASP_TLP64_PAYLOAD_SIZE : bytes;
    }

    // Encapsulate an arbitrary CTF binary event payload into a 64-byte TLP
    template <typename TCtfPayload>
    static Tlp64 make_ctf(Channel ch, uint8_t tag, const TCtfPayload& ctf_event, uint64_t timestamp_ns = 0, TlpType type = TlpType::DmaStream) noexcept {
        static_assert(sizeof(TCtfPayload) <= ASP_TLP64_PAYLOAD_SIZE, "CTF payload size exceeds 40-byte TLP limit");
        Tlp64 packet{};
        packet.wire.type = static_cast<uint8_t>(type);
        packet.wire.tag = tag;
        packet.wire.channel = static_cast<uint8_t>(ch);
        packet.wire.length_dw = static_cast<uint16_t>((sizeof(TCtfPayload) + 3) / 4);
        packet.wire.timestamp_ns = timestamp_ns;
        std::memcpy(packet.wire.payload, &ctf_event, sizeof(TCtfPayload));
        return packet;
    }

    // Access payload as strongly typed CTF structure
    template <typename TCtfPayload>
    const TCtfPayload* as_ctf() const noexcept {
        static_assert(sizeof(TCtfPayload) <= ASP_TLP64_PAYLOAD_SIZE, "Requested CTF payload type exceeds 40 bytes");
        return reinterpret_cast<const TCtfPayload*>(wire.payload);
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
