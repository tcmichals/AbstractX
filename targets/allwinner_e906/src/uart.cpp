/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E906 UART (DesignWare 8250) driver: one driver, one object per port.
 */

#include "hal/uart.hpp"
#include "hal/ccu.hpp"
#include "hal/clic.hpp"
#include "hal/driver_wait.hpp"
#include "hal/timer.hpp"
#include "abstractx/interrupt_lock.hpp"

#include <initializer_list>

// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-HAL-02] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-IRQ-02] targets/allwinner_e906/SPECIFICATION.md

namespace abstractx::hal {

namespace {

constexpr uint32_t kIerRx  = 0x01;  // ERBFI: RX data / timeout
constexpr uint32_t kIerTx  = 0x02;  // ETBEI: THR empty
constexpr uint32_t kIerLsi = 0x04;  // ELSI: line status

__attribute__((section(".sram_c"))) alignas(4) uint8_t s_u3_dma_rx[UartBuffers::kDmaRingBytes];
__attribute__((section(".sram_c"))) alignas(4) ::hal::DmaLli s_u3_rx_lli[UartBuffers::kDmaNumBufs];
__attribute__((section(".sram_c"))) alignas(4) uint8_t s_u3_tx_dma[UartBuffers::kTxDmaBytes];
__attribute__((section(".sram_c"))) alignas(4) ::hal::DmaLli s_u3_tx_lli;
__attribute__((section(".sram_c"))) uint8_t s_u3_rx[UartBuffers::kRxBytes];
__attribute__((section(".sram_c"))) uint8_t s_u3_tx[UartBuffers::kTxBytes];

E906Uart g_uart3{::hal::ports::kUart3,
                 UartBuffers{s_u3_dma_rx, s_u3_rx_lli, s_u3_tx_dma, &s_u3_tx_lli, s_u3_rx, s_u3_tx}};

} // namespace

E906Uart& get_e906_uart3() noexcept { return g_uart3; }

// ---------------------------------------------------------------------------
// Byte ring (SPSC): control indices in .bss, storage in .sram_c.
// ---------------------------------------------------------------------------
void E906Uart::ByteRing::clear() noexcept {
    head.store(0, std::memory_order_relaxed);
    tail.store(0, std::memory_order_relaxed);
}

bool E906Uart::ByteRing::push(uint8_t b) noexcept {
    const uint32_t t = tail.load(std::memory_order_relaxed);
    const uint32_t h = head.load(std::memory_order_acquire);
    if (t - h >= cap) return false;
    buf[t & (cap - 1U)] = b;
    tail.store(t + 1, std::memory_order_release);
    return true;
}

bool E906Uart::ByteRing::pop(uint8_t& b) noexcept {
    const uint32_t h = head.load(std::memory_order_relaxed);
    const uint32_t t = tail.load(std::memory_order_acquire);
    if (h == t) return false;
    b = buf[h & (cap - 1U)];
    head.store(h + 1, std::memory_order_release);
    return true;
}

bool E906Uart::ByteRing::empty() const noexcept {
    return head.load(std::memory_order_relaxed) == tail.load(std::memory_order_acquire);
}

E906Uart::E906Uart(const ::hal::ports::UartPort& port, const UartBuffers& bufs) noexcept
    : port_(port),
      bufs_(bufs),
      dma_ring_(std::span<uint8_t>(bufs.dma_rx, UartBuffers::kDmaRingBytes), UartBuffers::kDmaBufBytes),
      rx_{bufs.rx, static_cast<uint32_t>(UartBuffers::kRxBytes)},
      tx_{bufs.tx, static_cast<uint32_t>(UartBuffers::kTxBytes)} {}

// ---------------------------------------------------------------------------
// Receive DMA ([SPEC-E906-HAL-02])
// ---------------------------------------------------------------------------

// Sync the circular DMA ring from DMAC_PKG_NUM_REG and DMAC_CUR_DEST_REG, then move completed
// bytes into the polled-read buffer when no coroutine is awaiting the ring.
void E906Uart::dma_sync(bool end_of_frame) noexcept {
    if (!dma_active_ || rx_chan_ < 0) return;
    constexpr uint32_t kBuf = UartBuffers::kDmaBufBytes;
    constexpr uint32_t kRing = UartBuffers::kDmaRingBytes;
    const uint8_t ch = static_cast<uint8_t>(rx_chan_);
    const uint32_t base_pa = ::hal::DmaController::pa(bufs_.dma_rx);
    uint32_t pkg0 = 0, pkg1 = 0, cur_dst = 0;
    do {
        pkg0 = ::hal::DmaController::pkg_num(ch);
        cur_dst = ::hal::DmaController::current_dst_pa(ch);
        pkg1 = ::hal::DmaController::pkg_num(ch);
    } while (pkg0 != pkg1);

    if (cur_dst < base_pa || cur_dst > base_pa + kRing) {
        return;
    }
    uint32_t pkg_bytes = (pkg0 - dma_pkg0_) * kBuf;
    const uint32_t pos_in_ring = (cur_dst - base_pa) & (kRing - 1U);
    // DMA_PKG_NUM_REG increments when an LLI burst starts (~145 us before DMA_CUR_DEST_REG
    // advances): if pkg_bytes is one buffer ahead of pos_in_ring, that block is still in flight.
    if (pkg_bytes >= kBuf && ((pkg_bytes - pos_in_ring) & (kRing - 1U)) == kBuf) {
        pkg_bytes -= kBuf;
    }
    const uint32_t ahead = (pos_in_ring - (pkg_bytes & (kRing - 1U))) & (kRing - 1U);
    // Partial-buffer bytes only at end of frame: mid-burst, CUR_DEST runs ahead of the SRAM write.
    const uint32_t total_written = pkg_bytes + (end_of_frame && ahead < kBuf ? ahead : 0U);
    const uint32_t new_bytes = static_cast<int32_t>(total_written - dma_wr_) > 0 ? (total_written - dma_wr_) : 0U;
    if (new_bytes == 0U && !end_of_frame) {
        return;
    }
    dma_wr_ = total_written;
    dma_ring_.produced_from_isr(new_bytes, end_of_frame);

    if (!dma_ring_.reader_waiting()) {
        uint8_t tmp[64];
        size_t n = 0;
        while ((n = dma_ring_.copy_out(tmp, sizeof(tmp))) > 0) {
            for (size_t i = 0; i < n; ++i) {
                if (!rx_.push(tmp[i])) {
                    rx_dropped_ = rx_dropped_ + 1;
                }
            }
        }
    }
}

void E906Uart::on_rx_dma_pkg(uint8_t, bool) noexcept {
    abstractx::InterruptGuard guard;
    dma_sync(false);
}

void E906Uart::on_tx_dma_done(uint8_t, bool) noexcept { tx_dma_done_ = true; }

bool E906Uart::start_rx_dma() noexcept {
    constexpr uint32_t kBuf = UartBuffers::kDmaBufBytes;
    constexpr uint32_t kN = UartBuffers::kDmaNumBufs;
    ::hal::DmaController::init();
    if (rx_chan_ < 0) {
        rx_chan_ = ::hal::DmaController::allocate_channel();
    }
    if (rx_chan_ < 0) {
        dma_active_ = false;
        return false;
    }
    const uint8_t ch = static_cast<uint8_t>(rx_chan_);
    ::hal::DmaController::stop_transfer(ch);
    dma_ring_.reset();
    for (uint32_t i = 0; i < kN; ++i) {
        const uint32_t next_pa = ::hal::DmaController::pa(&bufs_.rx_lli[(i + 1U) & (kN - 1U)]);
        bufs_.rx_lli[i] = ::hal::DmaController::make_periph2mem(
            port_.base + kRbr, bufs_.dma_rx + i * kBuf, kBuf, port_.drq,
            ::hal::DMA_BURST_1, ::hal::DMA_WIDTH_8, next_pa);
    }
    reg(kIir) = 0x8FU;       // FCR: FIFO on, reset both, DMA mode 1, RX trigger half full
    reg(kDmaReqEn) = 0x07U;  // RX_REQ_EN | TX_REQ_EN | TIMEOUT_EN
    const bool ok = ::hal::DmaController::start_transfer(
        ch, &bufs_.rx_lli[0],
        ::hal::DmaController::CompletionCallback::create<E906Uart, &E906Uart::on_rx_dma_pkg>(*this),
        ::hal::DMA_IRQ_PKG, true);
    dma_pkg0_ = ::hal::DmaController::pkg_num(ch);
    dma_wr_ = 0;
    dma_active_ = ok;
    return ok;
}

void E906Uart::stop_rx_dma() noexcept {
    if (rx_chan_ >= 0) {
        ::hal::DmaController::stop_transfer(static_cast<uint8_t>(rx_chan_));
    }
    reg(kDmaReqEn) = 0x00U;
    reg(kIir) = 0x87U;  // FCR: FIFO on, DMA mode 0, RX trigger half full
    dma_active_ = false;
}

bool E906Uart::set_dma(bool enable) noexcept {
    abstractx::InterruptGuard guard;
    dma_want_ = enable;
    if (!attached_) {
        return dma_want_;
    }
    if (enable) {
        if (!dma_active_) {
            (void)start_rx_dma();
        }
    } else if (dma_active_) {
        dma_sync(true);
        stop_rx_dma();
    }
    return dma_active_;
}

// ---------------------------------------------------------------------------
// Interrupt (shared CLIC line: returns when IIR reports nothing pending)
// ---------------------------------------------------------------------------
void E906Uart::on_irq() noexcept {
    for (int guard = 0; guard < 8; ++guard) {
        const uint32_t iir = reg(kIir) & 0x0FU;
        if (iir == 0x01U) {
            return;  // nothing pending (0x1); busy detect is 0x7, so not a bit-0 test
        }
        switch (iir) {
            case 0x04:    // RX data available (half-full trigger)
            case 0x0C: {  // RX character timeout (4 character times idle)
                if (dma_active_) {
                    // DMA mode 1 with DMA_REQ_EN = 0x07: the DMAC drains trigger batches and timeout
                    // tails; the CPU never reads RBR. On 0x04 wait for one byte drained so IIR
                    // clears; on 0x0C wait for RFL == 0 plus the MBUS write before the tail sync.
                    const uint32_t target_rfl = (iir == 0x0CU) ? 0U : 63U;
                    for (uint32_t spin = 0; spin < 1024U && (reg(kRfl) & 0x1FFU) > target_rfl; ++spin) {
                        for (uint32_t d = 0; d < 32U; ++d) {
                            asm volatile("nop");
                        }
                    }
                    if (iir == 0x0CU) {
                        for (uint32_t d = 0; d < 64U; ++d) {
                            asm volatile("nop");
                        }
                        dma_sync(true);
                    }
                    break;
                }
                // Interrupt receive: drain the FIFO via RFL + RBR.
                for (uint32_t n = reg(kRfl) & 0x1FFU; n; n = reg(kRfl) & 0x1FFU) {
                    for (; n; --n) {
                        if (!rx_.push(static_cast<uint8_t>(reg(kRbr) & 0xFFU))) {
                            rx_dropped_ = rx_dropped_ + 1;
                        }
                    }
                }
                break;
            }
            case 0x02: {  // THR empty: refill the FIFO from the TX ring
                uint8_t b = 0;
                size_t n = 0;
                while (n < port_.fifo_bytes && tx_.pop(b)) {
                    reg(kRbr) = b;
                    ++n;
                }
                if (tx_.empty()) {
                    reg(kIer) = reg(kIer) & ~kIerTx;
                }
                break;
            }
            case 0x06: {  // line status: overrun, parity, framing, break
                const uint32_t lsr = reg(kLsr);
                uint8_t errs = 0;
                if (lsr & (1U << 1)) errs |= UartLineError::Overrun;
                if (lsr & (1U << 2)) errs |= UartLineError::Parity;
                if (lsr & (1U << 3)) errs |= UartLineError::Framing;
                if (lsr & (1U << 4)) errs |= UartLineError::Break;
                if (errs != 0) {
                    dma_ring_.line_error_from_isr(errs);
                }
                break;
            }
            case 0x07: touch(kUsr); break;  // busy detect (DW APB UART)
            default:   touch(kMsr); break;  // modem status
        }
    }
}

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

// The T527 "configure at busy" sequence (manual 8.19, UART_HALT): an LCR/DLL write while
// USR.BUSY is ignored and raises busy-detect, and a GPS may be streaming.
void E906Uart::program_baud(uint32_t baud) noexcept {
    const uint32_t divisor = (24000000U + 8U * baud) / (16U * baud);
    reg(kHalt) = 0x02;                   // CHCFG_AT_BUSY
    reg(kLcr) = 0x83;
    reg(kRbr) = divisor & 0xFFU;         // DLL
    reg(kIer) = (divisor >> 8) & 0xFFU;  // DLH
    reg(kLcr) = 0x03;                    // 8N1
    reg(kHalt) = 0x06;                   // CHANGE_UPDATE; hardware clears it when applied
    ::hal::Timer::delay_us(50);          // one sleep, then carry on (no register spin)
    reg(kHalt) = 0x00;
}

void E906Uart::init(uint32_t baud) {
    if (baud == 0) baud = 9600;
    ::hal::Ccu::enable_module(port_.clock);
    for (const auto* m : {&port_.pins, &port_.pull}) {
        if (m->cfg != 0) {
            auto& r = *reinterpret_cast<volatile uint32_t*>(m->cfg);
            r = (r & ~m->mask) | m->func;
        }
    }

    reg(kIer) = 0;  // the CLIC line is shared: leave it enabled, silence this port
    stop_rx_dma();
    reg(kIir) = 0x87;  // FCR: FIFO on, reset both, RX trigger half full
    program_baud(baud);
    reg(kIir) = 0x87;  // flush bytes received at the old baud
    touch(kUsr);       // clear any busy-detect raised before this
    touch(kLsr);       // clear sticky line errors from the baud switch
    reg(kMcr) = 0x03;

    rx_.clear();
    tx_.clear();

    if (dma_want_) {
        (void)start_rx_dma();
    } else {
        stop_rx_dma();
    }

    const uint32_t line = ::hal::clic::line_for_gic(port_.gic);
    ::hal::clic::route_gic(port_.gic, true);
    if (!attached_) {
        attached_ = ::hal::irq::attach(line,
            +[](void* ctx) noexcept { static_cast<E906Uart*>(ctx)->on_irq(); }, this);
        ::hal::irq::describe(line, port_.name, &rx_dropped_);
        ::hal::irq::describe(::hal::clic::kLineDma, "dma");
    } else {
        ::hal::clic::enable(line);
    }
    reg(kIer) = kIerRx | kIerLsi;
}

bool E906Uart::set_baud_rate(uint32_t baud) {
    if (baud == 0) return false;
    abstractx::InterruptGuard guard;
    const uint32_t ier = reg(kIer);
    reg(kIer) = 0;
    program_baud(baud);
    reg(kIer) = ier;
    return true;
}

UartCapabilities E906Uart::capabilities() const noexcept {
    UartCapabilities caps{};
    caps.fifo_bytes = port_.fifo_bytes;
    caps.rx_dma = true;
    caps.tx_dma = true;
    return caps;
}

// ---------------------------------------------------------------------------
// Receive (polled)
// ---------------------------------------------------------------------------
size_t E906Uart::read(std::span<uint8_t> buffer) {
    if (dma_active_) {
        abstractx::InterruptGuard guard;
        const bool fifo_idle = (reg(kRfl) & 0x1FFU) == 0U;
        dma_sync(fifo_idle);
    }
    size_t n = 0;
    while (n < buffer.size() && rx_.pop(buffer[n])) {
        ++n;
    }
    return n;
}

bool E906Uart::read_byte(uint8_t& ch) { return read(std::span<uint8_t>(&ch, 1)) == 1; }

// ---------------------------------------------------------------------------
// Transmit
// ---------------------------------------------------------------------------

// Larger than the FIFO: DMA in bounce-buffer chunks, 50 ms deadline each ([SPEC-E906-HAL-01]).
size_t E906Uart::write_dma(std::span<const uint8_t> data) noexcept {
    if (tx_chan_ < 0) {
        tx_chan_ = ::hal::DmaController::allocate_channel();
    }
    if (tx_chan_ < 0) return 0;
    size_t sent = 0;
    while (sent < data.size()) {
        const size_t left = data.size() - sent;
        const size_t chunk = left > UartBuffers::kTxDmaBytes ? UartBuffers::kTxDmaBytes : left;
        for (size_t i = 0; i < chunk; ++i) {
            bufs_.tx_dma[i] = data[sent + i];
        }
        *bufs_.tx_lli = ::hal::DmaController::make_mem2periph(
            bufs_.tx_dma, port_.base + kRbr, static_cast<uint32_t>(chunk), port_.drq);
        tx_dma_done_ = false;
        ::hal::DmaController::start_transfer(
            static_cast<uint8_t>(tx_chan_), bufs_.tx_lli,
            ::hal::DmaController::CompletionCallback::create<E906Uart, &E906Uart::on_tx_dma_done>(*this));
        const uint64_t deadline = ::hal::Timer::get_ticks() + 50ULL * ::hal::Timer::TICKS_PER_MS;
        while (!tx_dma_done_ && ::hal::Timer::get_ticks() < deadline) {
            ::hal::driver_wait(deadline);
        }
        if (!tx_dma_done_) {
            ::hal::DmaController::stop_transfer(static_cast<uint8_t>(tx_chan_));
            break;
        }
        sent += chunk;
    }
    return sent;
}

size_t E906Uart::write(std::span<const uint8_t> data) {
    if (data.size() > port_.fifo_bytes && dma_active_) {
        const size_t sent = write_dma(data);
        if (sent > 0 || tx_chan_ >= 0) return sent;
    }
    for (const uint8_t b : data) {
        while (!tx_.push(b)) {
            {
                abstractx::InterruptGuard guard;
                reg(kIer) = reg(kIer) | kIerTx;
            }
            ::hal::driver_wait(::hal::Timer::get_ticks() + 10ULL * ::hal::Timer::TICKS_PER_MS);
        }
    }
    abstractx::InterruptGuard guard;
    reg(kIer) = reg(kIer) | kIerTx;  // THR-empty interrupt drains the ring
    return data.size();
}

void E906Uart::write_byte(uint8_t ch) { (void)write(std::span<const uint8_t>(&ch, 1)); }

void E906Uart::puts(const char* str) {
    if (!str) return;
    for (; *str; ++str) {
        if (*str == '\n') write_byte('\r');
        write_byte(static_cast<uint8_t>(*str));
    }
}

void E906Uart::flush() {
    const uint64_t deadline = ::hal::Timer::get_ticks() + 50ULL * ::hal::Timer::TICKS_PER_MS;
    while (!tx_.empty() && ::hal::Timer::get_ticks() < deadline) {
        ::hal::driver_wait(deadline);
    }
}

// Async write: queue what fits in the TX ring, never wait ([SPEC-E906-HAL-04] rule 1).
void E906Uart::start_hardware_transfer_from_isr(const UartTxRequest& req) noexcept {
    UartResult res{};
    if (req.break_us != 0) {
        res.status = UartStatus::NotSupported;
    } else {
        size_t n = 0;
        while (n < req.data.size() && tx_.push(req.data[n])) {
            ++n;
        }
        reg(kIer) = reg(kIer) | kIerTx;
        res.bytes_transferred = n;
        res.status = n == req.data.size() ? UartStatus::Ok : UartStatus::QueueFull;
    }
    res.timestamp_us = ::hal::Timer::get_time_us();
    push_completion_from_isr(req, res);
    set_hardware_idle_from_isr();
}

} // namespace abstractx::hal
