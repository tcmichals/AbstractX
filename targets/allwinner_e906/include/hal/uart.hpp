/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E906 UART (DesignWare 8250) driver header.
 */

#pragma once

#include "abstractx/hal/uart.hpp"
#include "abstractx/hal/uart_rx.hpp"
#include "hal/dma.hpp"
#include "hal/ports.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace abstractx::hal {

// Static storage for one port, in .sram_c (SRAM Space 1) to save SRAM_FAST.
struct UartBuffers {
    static constexpr uint32_t kDmaBufBytes = 64;
    static constexpr uint32_t kDmaNumBufs  = 16;
    static constexpr uint32_t kDmaRingBytes = kDmaBufBytes * kDmaNumBufs;
    static constexpr size_t   kRxBytes = 1024;     // polled-read buffer
    static constexpr size_t   kTxBytes = 256;      // TX ring
    static constexpr size_t   kTxDmaBytes = 256;   // TX DMA bounce buffer

    uint8_t*        dma_rx;   // kDmaRingBytes
    ::hal::DmaLli*  rx_lli;   // kDmaNumBufs
    uint8_t*        tx_dma;   // kTxDmaBytes
    ::hal::DmaLli*  tx_lli;   // 1
    uint8_t*        rx;       // kRxBytes
    uint8_t*        tx;       // kTxBytes
};

// Interrupt-driven UART, receive by circular DMA ([SPEC-E906-HAL-02]).
// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md
class E906Uart final : public IUart {
public:
    E906Uart(const ::hal::ports::UartPort& port, const UartBuffers& bufs) noexcept;

    void init(uint32_t baudrate) override;
    bool set_baud_rate(uint32_t baudrate) override;
    void write_byte(uint8_t ch) override;
    void puts(const char* str) override;
    size_t write(std::span<const uint8_t> buffer) override;
    bool read_byte(uint8_t& ch) override;
    size_t read(std::span<uint8_t> buffer) override;
    void flush() override;
    UartCapabilities capabilities() const noexcept override;
    UartRxRingBase* rx_ring() noexcept override { return &dma_ring_; }

    // Receive by DMA (default) or by interrupt (bring-up); returns whether DMA is active.
    bool set_dma(bool enable) noexcept;
    bool dma_active() const noexcept { return dma_active_; }
    bool attached() const noexcept { return attached_; }
    uint32_t line_status() const noexcept { return reg(kLsr) & 0xFFU; }
    uint32_t rx_dropped() const noexcept { return rx_dropped_; }

    void on_irq() noexcept;

protected:
    void start_hardware_transfer_from_isr(const UartTxRequest& req) noexcept override;

private:
    static constexpr uint32_t kRbr = 0x00, kIer = 0x04, kIir = 0x08, kLcr = 0x0C, kMcr = 0x10,
                              kLsr = 0x14, kMsr = 0x18, kUsr = 0x7C, kRfl = 0x84, kHalt = 0xA4,
                              kDmaReqEn = 0xB4;

    // Lock-free SPSC byte ring over external storage.
    struct ByteRing {
        uint8_t* buf;
        uint32_t cap;  // power of two
        std::atomic<uint32_t> head{0};
        std::atomic<uint32_t> tail{0};
        void clear() noexcept;
        bool push(uint8_t b) noexcept;
        bool pop(uint8_t& b) noexcept;
        bool empty() const noexcept;
    };

    volatile uint32_t& reg(uint32_t off) const noexcept {
        return *reinterpret_cast<volatile uint32_t*>(port_.base + off);
    }
    void touch(uint32_t off) const noexcept { const uint32_t v = reg(off); (void)v; }
    void program_baud(uint32_t baud) noexcept;
    bool start_rx_dma() noexcept;
    void stop_rx_dma() noexcept;
    void dma_sync(bool end_of_frame) noexcept;
    void on_rx_dma_pkg(uint8_t channel, bool ok) noexcept;
    void on_tx_dma_done(uint8_t channel, bool ok) noexcept;
    size_t write_dma(std::span<const uint8_t> data) noexcept;

    const ::hal::ports::UartPort& port_;
    const UartBuffers bufs_;
    UartRxRingBase dma_ring_;
    ByteRing rx_;
    ByteRing tx_;
    volatile uint32_t rx_dropped_{0};
    bool attached_{false};
    bool dma_want_{true};
    bool dma_active_{false};
    int rx_chan_{-1};
    int tx_chan_{-1};
    volatile bool tx_dma_done_{false};
    uint32_t dma_pkg0_{0};
    uint32_t dma_wr_{0};
};

E906Uart& get_e906_uart3() noexcept;

} // namespace abstractx::hal
