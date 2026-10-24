/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * e906_periph.cpp - Interrupt-driven UART3 (GPS) and TWI1 (compass) for the MicroPython
 * bindings, plus interrupt statistics.
 *
 * @impl [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
 * @impl [SPEC-E906-IRQ-02] targets/allwinner_e906/SPECIFICATION.md
 */

#include "abstractx/abstractx.hpp"
#include "abstractx/hal/platform.hpp"
#include "abstractx/hal/uart_rx.hpp"
#include "abstractx/interrupt_lock.hpp"
#include "hal/clic.hpp"
#include "hal/dma.hpp"
#include "hal/rpmsg.hpp"
#include "hal/timer.hpp"
#include "tlp_ring.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

extern "C" void abstractx_repl_vm_hook(void);

namespace {

template <uintptr_t Addr>
struct Reg {
    static volatile uint32_t& r() noexcept { return *reinterpret_cast<volatile uint32_t*>(Addr); }
    // A read for its side effect (clears an interrupt); (void)r() would not access it.
    static void touch() noexcept { const uint32_t v = r(); (void)v; }
};

// ---------------------------------------------------------------------------
// Common wait: run the VM hook (Ctrl-C, RPMsg), then sleep until any interrupt
// or `deadline` (mtime ticks). Never reads a peripheral register.
// ---------------------------------------------------------------------------
void wait_step(uint64_t deadline) noexcept {
    hal::Timer::arm_wake(deadline);
    abstractx_repl_vm_hook();
    abstractx::hal::platform_idle_wait();
    abstractx::status_service();  // reports keep coming while a script waits [SPEC-E906-REPL-18]
}

// ---------------------------------------------------------------------------
// UART3 (PI11 TX / PI12 RX), DesignWare 8250, GIC 37 -> CLIC line 90
// Circular 4x64 B DMA receive in .sram_c ([SPEC-E906-HAL-02]) with runtime ISR
// fallback switch (`abstractx.uart_dma`).
// ---------------------------------------------------------------------------
constexpr uintptr_t kUart3 = 0x02500C00U;
using U_RBR = Reg<kUart3 + 0x00>;  // also THR / DLL
using U_IER = Reg<kUart3 + 0x04>;  // also DLH
using U_IIR = Reg<kUart3 + 0x08>;  // read: IIR, write: FCR
using U_LCR = Reg<kUart3 + 0x0C>;
using U_MCR = Reg<kUart3 + 0x10>;
using U_LSR = Reg<kUart3 + 0x14>;
using U_MSR = Reg<kUart3 + 0x18>;
using U_USR = Reg<kUart3 + 0x7C>;
using U_RFL = Reg<kUart3 + 0x84>;  // RX FIFO level
using U_HALT = Reg<kUart3 + 0xA4>; // bit1 CHCFG_AT_BUSY, bit2 CHANGE_UPDATE
using U_DMA_REQ_EN = Reg<kUart3 + 0xB4>; // bit0 RX_REQ_EN, bit1 TX_REQ_EN, bit2 TIMEOUT_EN

constexpr uint32_t kIerRx  = 0x01;  // ERBFI: RX data / timeout
constexpr uint32_t kIerTx  = 0x02;  // ETBEI: THR empty
constexpr uint32_t kIerLsi = 0x04;  // ELSI: Receiver line status (overrun/framing/parity/break)
constexpr size_t kUartFifo = 128;   // UART1-7 (T527 manual 8.19.1); UART0 / S_UART are 64

constexpr uint32_t kDmaBufBytes  = 64;
constexpr uint32_t kDmaNumBufs   = 16;
constexpr uint32_t kDmaRingBytes = kDmaBufBytes * kDmaNumBufs;

// Lock-free SPSC byte ring whose control indices live in .bss (zeroed at boot) and whose
// backing storage lives in .sram_c (SRAM Space 1, NOLOAD) to preserve SRAM_FAST.
template <size_t Capacity>
struct SramByteRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
    uint8_t* buf{nullptr};
    std::atomic<uint32_t> head{0};
    std::atomic<uint32_t> tail{0};

    void clear() noexcept {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
    }

    bool push(uint8_t b) noexcept {
        const uint32_t t = tail.load(std::memory_order_relaxed);
        const uint32_t h = head.load(std::memory_order_acquire);
        if (t - h >= Capacity) return false;
        buf[t & (Capacity - 1)] = b;
        tail.store(t + 1, std::memory_order_release);
        return true;
    }

    bool pop(uint8_t& b) noexcept {
        const uint32_t h = head.load(std::memory_order_relaxed);
        const uint32_t t = tail.load(std::memory_order_acquire);
        if (h == t) return false;
        b = buf[h & (Capacity - 1)];
        head.store(h + 1, std::memory_order_release);
        return true;
    }

    bool empty() const noexcept {
        return head.load(std::memory_order_relaxed) == tail.load(std::memory_order_acquire);
    }
};

__attribute__((section(".sram_c"))) alignas(4) uint8_t s_uart_dma_rx_mem[kDmaRingBytes];
__attribute__((section(".sram_c"))) alignas(4) hal::DmaLli s_uart_rx_lli[kDmaNumBufs];
__attribute__((section(".sram_c"))) alignas(4) uint8_t s_uart_tx_dma_buf[256];
__attribute__((section(".sram_c"))) alignas(4) hal::DmaLli s_uart_tx_lli;
__attribute__((section(".sram_c"))) uint8_t s_uart_rx_mem[1024];
__attribute__((section(".sram_c"))) uint8_t s_uart_tx_mem[256];

// @impl [SPEC-E906-HAL-02] targets/allwinner_e906/SPECIFICATION.md
abstractx::hal::UartRxRingBase s_uart_rx_ring{std::span<uint8_t>(s_uart_dma_rx_mem, kDmaRingBytes), kDmaBufBytes};
SramByteRing<1024> s_uart_rx{s_uart_rx_mem};  // DMA/ISR -> VM
SramByteRing<256>  s_uart_tx{s_uart_tx_mem};  // VM -> ISR
volatile uint32_t s_uart_rx_dropped = 0;
bool s_uart_attached = false;
bool s_uart_dma_want = true;
bool s_uart_dma_active = false;
int  s_uart_rx_dma_chan = -1;
int  s_uart_tx_dma_chan = -1;
volatile bool s_uart_tx_dma_done = false;
uint32_t s_uart_dma_pkg0 = 0;
uint32_t s_uart_dma_wr = 0;

// Sync the circular 4x64 B DMA RX ring from DMAC_PKG_NUM_REG and DMAC_CUR_DEST_REG, then
// drain completed spans into the 1024 B VM ring when no coroutine is awaiting s_uart_rx_ring.
// @impl [SPEC-E906-HAL-02] targets/allwinner_e906/SPECIFICATION.md
void uart3_dma_sync(bool end_of_frame) noexcept {
    if (!s_uart_dma_active || s_uart_rx_dma_chan < 0) return;
    const uint8_t ch = static_cast<uint8_t>(s_uart_rx_dma_chan);
    const uint32_t base_pa = hal::DmaController::pa(s_uart_dma_rx_mem);
    uint32_t pkg0 = 0, pkg1 = 0, cur_dst = 0;
    do {
        pkg0 = hal::DmaController::pkg_num(ch);
        cur_dst = hal::DmaController::current_dst_pa(ch);
        pkg1 = hal::DmaController::pkg_num(ch);
    } while (pkg0 != pkg1);

    if (cur_dst < base_pa || cur_dst > base_pa + kDmaRingBytes) {
        return;
    }
    uint32_t pkg_bytes = (pkg0 - s_uart_dma_pkg0) * kDmaBufBytes;
    const uint32_t pos_in_ring = (cur_dst - base_pa) & (kDmaRingBytes - 1U);
    // On Allwinner T527 DMAC, DMA_PKG_NUM_REG (+0x30) increments when an LLI burst starts
    // (~145 us before DMA_CUR_DEST_REG (+0x14) advances to the next LLI). If pkg_bytes is one
    // LLI buffer ahead of pos_in_ring, that 64 B block is still in flight to SRAM.
    if (pkg_bytes >= kDmaBufBytes &&
        ((pkg_bytes - pos_in_ring) & (kDmaRingBytes - 1U)) == kDmaBufBytes) {
        pkg_bytes -= kDmaBufBytes;
    }
    const uint32_t ahead = (pos_in_ring - (pkg_bytes & (kDmaRingBytes - 1U))) & (kDmaRingBytes - 1U);
    // Only include partial-LLI `ahead` bytes when `end_of_frame` is true (RX timeout or idle UART FIFO):
    // during an active 64 B LLI burst, DMA_CUR_DEST_REG advances 4 bytes ahead of the MBUS write to SRAM.
    const uint32_t total_written = pkg_bytes + (end_of_frame && ahead < kDmaBufBytes ? ahead : 0U);
    const uint32_t new_bytes = static_cast<int32_t>(total_written - s_uart_dma_wr) > 0
                                   ? (total_written - s_uart_dma_wr)
                                   : 0U;
    if (new_bytes == 0U && !end_of_frame) {
        return;
    }
    s_uart_dma_wr = total_written;
    s_uart_rx_ring.produced_from_isr(new_bytes, end_of_frame);

    if (!s_uart_rx_ring.reader_waiting()) {
        uint8_t tmp[64];
        size_t n = 0;
        while ((n = s_uart_rx_ring.copy_out(tmp, sizeof(tmp))) > 0) {
            for (size_t i = 0; i < n; ++i) {
                if (!s_uart_rx.push(tmp[i])) {
                    s_uart_rx_dropped = s_uart_rx_dropped + 1;
                }
            }
        }
    }
}

void on_uart3_rx_dma_pkg(uint8_t, bool) noexcept {
    abstractx::InterruptGuard guard;
    uart3_dma_sync(false);
}

void on_uart3_tx_dma_done(uint8_t, bool) noexcept {
    s_uart_tx_dma_done = true;
}

bool uart3_start_rx_dma() noexcept {
    hal::DmaController::init();
    if (s_uart_rx_dma_chan < 0) {
        s_uart_rx_dma_chan = hal::DmaController::allocate_channel();
    }
    if (s_uart_rx_dma_chan < 0) {
        s_uart_dma_active = false;
        return false;
    }
    const uint8_t ch = static_cast<uint8_t>(s_uart_rx_dma_chan);
    hal::DmaController::stop_transfer(ch);
    s_uart_rx_ring.reset();
    for (uint32_t i = 0; i < kDmaNumBufs; ++i) {
        const uint32_t next_pa = hal::DmaController::pa(&s_uart_rx_lli[(i + 1U) & (kDmaNumBufs - 1U)]);
        s_uart_rx_lli[i] = hal::DmaController::make_periph2mem(
            kUart3 + 0x00U,
            s_uart_dma_rx_mem + i * kDmaBufBytes,
            kDmaBufBytes,
            hal::DRQ_UART3,
            hal::DMA_BURST_1,
            hal::DMA_WIDTH_8,
            next_pa
        );
    }
    U_IIR::r() = 0x8FU;         // FCR: FIFO on, reset both, DMA mode 1, RX trigger half full
    U_DMA_REQ_EN::r() = 0x07U;  // RX_REQ_EN | TX_REQ_EN | TIMEOUT_EN ([SPEC-E906-HAL-02])
    const bool ok = hal::DmaController::start_transfer(
        ch,
        &s_uart_rx_lli[0],
        hal::DmaController::CompletionCallback::create<on_uart3_rx_dma_pkg>(),
        hal::DMA_IRQ_PKG,
        true
    );
    s_uart_dma_pkg0 = hal::DmaController::pkg_num(ch);
    s_uart_dma_wr = 0;
    s_uart_dma_active = ok;
    return ok;
}

void uart3_stop_rx_dma() noexcept {
    if (s_uart_rx_dma_chan >= 0) {
        hal::DmaController::stop_transfer(static_cast<uint8_t>(s_uart_rx_dma_chan));
    }
    U_DMA_REQ_EN::r() = 0x00U;
    U_IIR::r() = 0x87U;  // FCR: FIFO on, DMA mode 0, RX trigger half full
    s_uart_dma_active = false;
}

void uart3_isr(void*) noexcept {
    for (int guard = 0; guard < 8; ++guard) {
        const uint32_t iir = U_IIR::r() & 0x0FU;
        if (iir == 0x01U) {
            return;  // nothing pending (0x1). Busy detect is 0x7, so not a bit-0 test.
        }
        switch (iir) {
            case 0x04:  // RX data available (64 B half-full trigger)
            case 0x0C: {  // RX character timeout (4 character times idle)
                if (s_uart_dma_active) {
                    // In DMA1 mode with UART_DMA_REQ_EN=0x07, the DMAC drains both trigger batches
                    // and RX-timeout tails ([SPEC-E906-HAL-02]). Never read U_RBR from the CPU while
                    // DMA is active. On 0x04 wait only for RFL < 64 (1 byte drained) so IIR clears;
                    // on 0x0C wait for RFL == 0 plus MBUS write flush before syncing the tail.
                    const uint32_t target_rfl = (iir == 0x0CU) ? 0U : 63U;
                    for (uint32_t spin = 0; spin < 1024U && (U_RFL::r() & 0x1FFU) > target_rfl; ++spin) {
                        for (uint32_t d = 0; d < 32U; ++d) {
                            asm volatile("nop");
                        }
                    }
                    if (iir == 0x0CU) {
                        for (uint32_t d = 0; d < 64U; ++d) {
                            asm volatile("nop");
                        }
                        uart3_dma_sync(true);
                    }
                    break;
                }
                // ISR mode: drain the RX FIFO via U_RFL + U_RBR ([SPEC-E906-REPL-16]).
                for (uint32_t n = U_RFL::r() & 0x1FFU; n; n = U_RFL::r() & 0x1FFU) {
                    for (; n; --n) {
                        if (!s_uart_rx.push(static_cast<uint8_t>(U_RBR::r() & 0xFFU))) {
                            s_uart_rx_dropped = s_uart_rx_dropped + 1;
                        }
                    }
                }
                break;
            }
            case 0x02: {  // THR empty: refill the FIFO from the ring
                uint8_t b = 0;
                size_t n = 0;
                while (n < kUartFifo && s_uart_tx.pop(b)) {
                    U_RBR::r() = b;
                    ++n;
                }
                if (s_uart_tx.empty()) {
                    U_IER::r() = U_IER::r() & ~kIerTx;
                }
                break;
            }
            case 0x06: {  // line status: overrun, parity, framing, break ([SPEC-E906-HAL-02])
                const uint32_t lsr = U_LSR::r();
                uint8_t errs = 0;
                if (lsr & (1U << 1)) errs |= abstractx::hal::UartLineError::Overrun;
                if (lsr & (1U << 2)) errs |= abstractx::hal::UartLineError::Parity;
                if (lsr & (1U << 3)) errs |= abstractx::hal::UartLineError::Framing;
                if (lsr & (1U << 4)) errs |= abstractx::hal::UartLineError::Break;
                if (errs != 0) {
                    s_uart_rx_ring.line_error_from_isr(errs);
                }
                break;
            }
            case 0x07: U_USR::touch(); break;  // busy detect (DW APB UART)
            default:   U_MSR::touch(); break;  // modem status
        }
    }
}

// ---------------------------------------------------------------------------
// TWI1 (PB4 SCL / PB5 SDA), Allwinner TWI, GIC 43 -> CLIC line 91.
// One INT_FLG interrupt per bus step; INT_FLG is cleared by writing 1.
// ---------------------------------------------------------------------------
constexpr uintptr_t kTwi1 = 0x02502400U;
using T_DATA = Reg<kTwi1 + 0x08>;
using T_CNTR = Reg<kTwi1 + 0x0C>;
using T_STAT = Reg<kTwi1 + 0x10>;
using T_CCR  = Reg<kTwi1 + 0x14>;
using T_SRST = Reg<kTwi1 + 0x18>;

constexpr uint32_t kIntEn = 1U << 7;
constexpr uint32_t kBusEn = 1U << 6;
constexpr uint32_t kMSta  = 1U << 5;
constexpr uint32_t kMStp  = 1U << 4;
constexpr uint32_t kIntFlg = 1U << 3;
constexpr uint32_t kAAck  = 1U << 2;

enum class TwiState : uint8_t { Idle, Busy, Done, Error };

// Static, not on the caller's stack: a Ctrl-C can unwind the caller mid-transfer.
// Data buffers live in .sram_c (SRAM Space 1) to save 512 B of SRAM_FAST (.bss).
__attribute__((section(".sram_c"))) uint8_t s_twi_wbuf[256];
__attribute__((section(".sram_c"))) uint8_t s_twi_rbuf[256];

struct TwiXfer {
    volatile TwiState state = TwiState::Idle;
    uint8_t addr = 0;
    int16_t reg = -1;
    bool reg_sent = false;
    bool read_phase = false;
    uint8_t* wbuf = s_twi_wbuf;
    uint8_t* rbuf = s_twi_rbuf;
    size_t wlen = 0, widx = 0, rlen = 0, ridx = 0;
    uint8_t last_stat = 0;
} s_twi;
bool s_twi_attached = false;
volatile uint32_t s_twi_timeouts = 0;

inline void twi_cmd(uint32_t bits) noexcept { T_CNTR::r() = kBusEn | kIntEn | kIntFlg | bits; }

inline void twi_finish(TwiState st) noexcept {
    T_CNTR::r() = kBusEn | kMStp | kIntFlg;  // STOP, no further interrupt
    s_twi.state = st;
}

void twi_after_tx() noexcept {
    if (s_twi.reg >= 0 && !s_twi.reg_sent) {
        s_twi.reg_sent = true;
        T_DATA::r() = static_cast<uint8_t>(s_twi.reg);
        twi_cmd(0);
    } else if (s_twi.widx < s_twi.wlen) {
        T_DATA::r() = s_twi.wbuf[s_twi.widx++];
        twi_cmd(0);
    } else if (s_twi.rlen > 0 && !s_twi.read_phase) {
        s_twi.read_phase = true;
        twi_cmd(kMSta);  // repeated START for the read
    } else {
        twi_finish(TwiState::Done);
    }
}

void twi1_isr(void*) noexcept {
    if (!(T_CNTR::r() & kIntFlg)) {
        return;  // another TWI on the shared line
    }
    const uint8_t stat = static_cast<uint8_t>(T_STAT::r() & 0xFFU);
    s_twi.last_stat = stat;
    if (s_twi.state != TwiState::Busy) {
        T_CNTR::r() = kBusEn | kIntFlg;  // stray flag: clear, interrupt off
        return;
    }
    switch (stat) {
        case 0x08:  // START sent
        case 0x10:  // repeated START sent
            T_DATA::r() = static_cast<uint8_t>((s_twi.addr << 1) | (s_twi.read_phase ? 1U : 0U));
            twi_cmd(0);
            break;
        case 0x18:  // SLA+W ACK
        case 0x28:  // data byte ACK
            twi_after_tx();
            break;
        case 0x40:  // SLA+R ACK
            twi_cmd(s_twi.rlen > 1 ? kAAck : 0);
            break;
        case 0x50:  // data received, ACK sent
            s_twi.rbuf[s_twi.ridx++] = static_cast<uint8_t>(T_DATA::r() & 0xFFU);
            twi_cmd((s_twi.rlen - s_twi.ridx) > 1 ? kAAck : 0);
            break;
        case 0x58:  // last byte received, NACK sent
            s_twi.rbuf[s_twi.ridx++] = static_cast<uint8_t>(T_DATA::r() & 0xFFU);
            twi_finish(TwiState::Done);
            break;
        default:    // NACK or bus error
            twi_finish(TwiState::Error);
            break;
    }
}

constexpr uint64_t kTwiTimeoutTicks = 20ULL * hal::Timer::TICKS_PER_MS;

} // namespace

extern "C" {

// ---------------------------------------------------------------------------
// UART3
// ---------------------------------------------------------------------------
bool e906_uart3_init(uint32_t baud) {
    constexpr uintptr_t kCcu = 0x02001000U;
    constexpr uintptr_t kPio = 0x02000000U;
    auto& ccu_uart_bgr = *reinterpret_cast<volatile uint32_t*>(kCcu + 0x090CU);
    auto& pi_cfg1 = *reinterpret_cast<volatile uint32_t*>(kPio + 0x0184U);
    auto& pi_pull0 = *reinterpret_cast<volatile uint32_t*>(kPio + 0x01A4U);

    if (baud == 0) baud = 9600;
    hal::clic::disable(hal::clic::line_for_gic(hal::clic::kGicUart3));
    ccu_uart_bgr = ccu_uart_bgr | (1U << 19) | (1U << 3);             // reset off, clock on
    pi_cfg1 = (pi_cfg1 & ~(0xFFU << 12)) | (0x22U << 12);             // PI11/PI12 = UART3
    pi_pull0 = (pi_pull0 & ~(0x03U << 24)) | (1U << 24);              // pull-up on RX

    U_IER::r() = 0;
    uart3_stop_rx_dma();
    U_IIR::r() = 0x87;  // FCR: FIFO on, reset both, RX trigger half full
    // The GPS may be streaming: an LCR/DLL write while USR.BUSY is ignored and raises
    // busy-detect. Use the T527 "configure at busy" sequence (manual 8.19, UART_HALT).
    const uint32_t divisor = (24000000U + 8U * baud) / (16U * baud);
    U_HALT::r() = 0x02;                 // CHCFG_AT_BUSY
    U_LCR::r() = 0x83;
    U_RBR::r() = divisor & 0xFFU;       // DLL
    U_IER::r() = (divisor >> 8) & 0xFFU; // DLH
    U_LCR::r() = 0x03;                  // 8N1
    U_HALT::r() = 0x06;                 // CHANGE_UPDATE; hardware clears it when applied
    hal::Timer::delay_us(50);           // one sleep, then carry on (no register spin)
    U_HALT::r() = 0x00;
    U_IIR::r() = 0x87;                  // flush any framing-error bytes received at the old baud
    U_USR::touch();                     // clear any busy-detect raised before this
    U_LSR::touch();                     // clear any sticky line status errors from baud switch
    U_MCR::r() = 0x03;

    s_uart_rx.clear();
    s_uart_tx.clear();

    if (s_uart_dma_want) {
        (void)uart3_start_rx_dma();
    } else {
        uart3_stop_rx_dma();
    }

    hal::clic::route_gic(hal::clic::kGicUart3, true);
    if (!s_uart_attached) {
        s_uart_attached = hal::irq::attach(hal::clic::line_for_gic(hal::clic::kGicUart3), uart3_isr, nullptr);
        // @impl [SPEC-E906-REPL-18] apps/e906_repl_app/SPECIFICATION.md
        hal::irq::describe(hal::clic::line_for_gic(hal::clic::kGicUart3), "uart3", &s_uart_rx_dropped);
        hal::irq::describe(hal::clic::kLineDma, "dma");
    } else {
        hal::clic::enable(hal::clic::line_for_gic(hal::clic::kGicUart3));
    }
    U_IER::r() = kIerRx | kIerLsi;
    return s_uart_attached;
}

// @impl [SPEC-E906-HAL-02] targets/allwinner_e906/SPECIFICATION.md
bool e906_uart3_set_dma(bool enable) {
    abstractx::InterruptGuard guard;
    s_uart_dma_want = enable;
    if (!s_uart_attached) {
        return s_uart_dma_want;
    }
    if (enable) {
        if (!s_uart_dma_active) {
            (void)uart3_start_rx_dma();
        }
    } else if (s_uart_dma_active) {
        uart3_dma_sync(true);
        uart3_stop_rx_dma();
    }
    return s_uart_dma_active;
}

bool e906_uart3_get_dma(void) {
    return s_uart_dma_active;
}

size_t e906_uart3_read(uint8_t* dst, size_t max_len) {
    if (s_uart_dma_active) {
        abstractx::InterruptGuard guard;
        const bool fifo_idle = (U_RFL::r() & 0x1FFU) == 0U;
        uart3_dma_sync(fifo_idle);
    }
    size_t n = 0;
    while (n < max_len && s_uart_rx.pop(dst[n])) {
        ++n;
    }
    return n;
}

size_t e906_uart3_write(const uint8_t* src, size_t len) {
    // @impl [SPEC-E906-HAL-01] [SPEC-E906-HAL-02] targets/allwinner_e906/SPECIFICATION.md
    if (len > kUartFifo && s_uart_dma_active) {
        if (s_uart_tx_dma_chan < 0) {
            s_uart_tx_dma_chan = hal::DmaController::allocate_channel();
        }
        if (s_uart_tx_dma_chan >= 0) {
            size_t sent = 0;
            while (sent < len) {
                const size_t chunk = (len - sent) > sizeof(s_uart_tx_dma_buf) ? sizeof(s_uart_tx_dma_buf) : (len - sent);
                for (size_t i = 0; i < chunk; ++i) {
                    s_uart_tx_dma_buf[i] = src[sent + i];
                }
                s_uart_tx_lli = hal::DmaController::make_mem2periph(
                    s_uart_tx_dma_buf, kUart3 + 0x00U, static_cast<uint32_t>(chunk), hal::DRQ_UART3
                );
                s_uart_tx_dma_done = false;
                hal::DmaController::start_transfer(
                    static_cast<uint8_t>(s_uart_tx_dma_chan),
                    &s_uart_tx_lli,
                    hal::DmaController::CompletionCallback::create<on_uart3_tx_dma_done>()
                );
                const uint64_t deadline = hal::Timer::get_ticks() + 50ULL * hal::Timer::TICKS_PER_MS;
                while (!s_uart_tx_dma_done && hal::Timer::get_ticks() < deadline) {
                    wait_step(deadline);
                }
                if (!s_uart_tx_dma_done) {
                    hal::DmaController::stop_transfer(static_cast<uint8_t>(s_uart_tx_dma_chan));
                    break;
                }
                sent += chunk;
            }
            return sent;
        }
    }
    for (size_t i = 0; i < len; ++i) {
        while (!s_uart_tx.push(src[i])) {
            {
                abstractx::InterruptGuard guard;
                U_IER::r() = U_IER::r() | kIerTx;
            }
            wait_step(hal::Timer::get_ticks() + 10ULL * hal::Timer::TICKS_PER_MS);
        }
    }
    abstractx::InterruptGuard guard;
    U_IER::r() = U_IER::r() | kIerTx;  // THR-empty interrupt drains the ring
    return len;
}

uint32_t e906_uart3_lsr(void) { return U_LSR::r() & 0xFFU; }
uint32_t e906_uart3_rx_dropped(void) { return s_uart_rx_dropped; }

// ---------------------------------------------------------------------------
// TWI1
// ---------------------------------------------------------------------------
bool e906_twi1_init(uint32_t freq) {
    constexpr uintptr_t kCcu = 0x02001000U;
    constexpr uintptr_t kPio = 0x02000000U;
    auto& ccu_twi_bgr = *reinterpret_cast<volatile uint32_t*>(kCcu + 0x091CU);
    auto& pb_cfg0 = *reinterpret_cast<volatile uint32_t*>(kPio + 0x0030U);

    hal::clic::disable(hal::clic::line_for_gic(hal::clic::kGicTwi1));
    ccu_twi_bgr = ccu_twi_bgr | (1U << 17) | (1U << 1);
    pb_cfg0 = (pb_cfg0 & ~(0xFFU << 16)) | (0x22U << 16);  // PB4/PB5 = TWI1
    T_SRST::r() = 1;  // soft reset (self-clearing)
    hal::Timer::delay_us(20);
    // 24 MHz: 400 kHz -> N=1, M=2 (0x12); 100 kHz -> N=2, M=5 (0x25)
    T_CCR::r() = freq >= 400000U ? 0x12U : 0x25U;
    T_CNTR::r() = kBusEn;
    s_twi.state = TwiState::Idle;

    hal::clic::route_gic(hal::clic::kGicTwi1, true);
    if (!s_twi_attached) {
        s_twi_attached = hal::irq::attach(hal::clic::line_for_gic(hal::clic::kGicTwi1), twi1_isr, nullptr);
        hal::irq::describe(hal::clic::line_for_gic(hal::clic::kGicTwi1), "twi1", &s_twi_timeouts);
    } else {
        hal::clic::enable(hal::clic::line_for_gic(hal::clic::kGicTwi1));
    }
    return s_twi_attached;
}

// One transaction: START, addr+W, [reg], [write bytes], [repeated START, addr+R, read bytes], STOP.
// reg < 0: no register byte. Returns bytes read (or written), -1 on NACK/bus error, -2 on timeout.
int e906_twi1_xfer(uint8_t addr, int reg, const uint8_t* wdata, size_t wlen, uint8_t* rdata, size_t rlen) {
    if (wlen > sizeof(s_twi_wbuf) || rlen > sizeof(s_twi_rbuf)) return -1;
    if (s_twi.state == TwiState::Busy) {
        twi_finish(TwiState::Error);  // a previous call was interrupted (Ctrl-C): abort it
    }
    if (T_CNTR::r() & kMStp) {
        // previous STOP still on the bus: one short sleep, then START (queued behind STOP)
        wait_step(hal::Timer::get_ticks() + 50ULL * hal::Timer::TICKS_PER_US);
    }
    s_twi.addr = addr;
    s_twi.reg = static_cast<int16_t>(reg);
    s_twi.reg_sent = false;
    s_twi.read_phase = (reg < 0 && wlen == 0 && rlen > 0);
    for (size_t i = 0; i < wlen; ++i) s_twi.wbuf[i] = wdata[i];
    s_twi.wlen = wlen;
    s_twi.widx = 0;
    s_twi.rlen = rlen;
    s_twi.ridx = 0;
    s_twi.state = TwiState::Busy;
    twi_cmd(kMSta);

    const uint64_t deadline = hal::Timer::get_ticks() + kTwiTimeoutTicks;
    while (s_twi.state == TwiState::Busy) {
        if (hal::Timer::get_ticks() >= deadline) {
            twi_finish(TwiState::Error);
            s_twi_timeouts = s_twi_timeouts + 1;
            return -2;
        }
        wait_step(deadline);
    }
    if (s_twi.state != TwiState::Done) return -1;
    for (size_t i = 0; i < s_twi.ridx && rdata; ++i) rdata[i] = s_twi.rbuf[i];
    return static_cast<int>(rlen > 0 ? s_twi.ridx : wlen);
}

uint32_t e906_twi1_timeouts(void) { return s_twi_timeouts; }

// ---------------------------------------------------------------------------
// Interrupt statistics ([SPEC-E906-REPL-16])
// ---------------------------------------------------------------------------
size_t e906_irq_stats(uint32_t* lines, uint32_t* counts, size_t max) {
    size_t n = 0;
    for (uint32_t line = 0; line < hal::clic::kNumLines && n < max; ++line) {
        const uint32_t c = hal::irq::count(line);
        if (c) {
            lines[n] = line;
            counts[n] = c;
            ++n;
        }
    }
    return n;
}

uint32_t e906_irq_spurious(void) { return hal::irq::spurious(); }
uint32_t e906_rpmsg_doorbells(void) { return hal::rpmsg_doorbell_irqs(); }

uint32_t e906_mtvec(void) {
    uint32_t v = 0;
#if defined(__riscv)
    asm volatile("csrr %0, mtvec" : "=r"(v));
#endif
    return v;
}

// Sleep `ms` with wfi; the timer interrupt ends it, the VM hook runs on every wake.
void e906_sleep_ms(uint32_t ms) {
    const uint64_t deadline = hal::Timer::get_ticks() + static_cast<uint64_t>(ms) * hal::Timer::TICKS_PER_MS;
    while (hal::Timer::get_ticks() < deadline) {
        wait_step(deadline);
    }
}

// ---------------------------------------------------------------------------
// DMA bring-up measurement ([SPEC-E906-HAL-01]): SRAM Space 0 -> Space 1 copy on an E906
// channel, completion on CLIC line 88, timed against a CPU copy.
// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
// ---------------------------------------------------------------------------
namespace {
uint8_t s_dma_src[1024];                                       // .bss, SRAM Space 0
__attribute__((section(".sram_c"))) uint8_t s_dma_dst[1024];   // SRAM Space 1
__attribute__((section(".sram_c"))) hal::DmaLli s_dma_lli;
volatile bool s_dma_done = false;
volatile bool s_dma_ok = false;
volatile uint32_t s_dma_end = 0;

uint32_t mcycle32() noexcept {
    uint32_t v = 0;
#if defined(__riscv)
    asm volatile("csrr %0, mcycle" : "=r"(v));
#endif
    return v;
}

void dma_bench_done(uint8_t, bool ok) noexcept {
    s_dma_end = mcycle32();
    s_dma_ok = ok;
    s_dma_done = true;
}
} // namespace

// Returns 1 when the DMA copy completed and the data matches, 0 on mismatch, -1 on timeout,
// -2 when no channel is free. Releases the allocated DMA channel before returning.
int e906_dma_bench(uint32_t n, uint32_t port, uint32_t* dma_cycles, uint32_t* cpu_cycles) {
    if (n == 0 || n > sizeof(s_dma_src)) return 0;
    hal::DmaController::init();
    const int ch = hal::DmaController::allocate_channel();
    if (ch < 0) return -2;

    for (uint32_t i = 0; i < n; ++i) {
        s_dma_src[i] = static_cast<uint8_t>(i * 7U + 3U);
        s_dma_dst[i] = 0;
    }
    const uint8_t mem = static_cast<uint8_t>(port & 0x3FU);
    s_dma_lli.cfg = hal::DmaController::build_cfg(mem, hal::DMA_BURST_8, hal::DMA_WIDTH_32, hal::DMA_MODE_LINEAR,
                                                  mem, hal::DMA_BURST_8, hal::DMA_WIDTH_32, hal::DMA_MODE_LINEAR);
    s_dma_lli.src = hal::DmaController::pa(s_dma_src);
    s_dma_lli.dst = hal::DmaController::pa(s_dma_dst);
    s_dma_lli.len = n;
    s_dma_lli.para = 0;
    s_dma_lli.p_lli_next = hal::LLI_LAST_ITEM;
    s_dma_done = false;
    s_dma_ok = false;
    const uint32_t start = mcycle32();
    hal::DmaController::start_transfer(static_cast<uint8_t>(ch), &s_dma_lli,
                                       hal::DmaController::CompletionCallback::create<dma_bench_done>());
    const uint64_t deadline = hal::Timer::get_ticks() + 20ULL * hal::Timer::TICKS_PER_MS;
    while (!s_dma_done && hal::Timer::get_ticks() < deadline) {
        wait_step(deadline);
    }
    if (!s_dma_done) {
        hal::DmaController::release_channel(static_cast<uint8_t>(ch));
        return -1;
    }
    hal::DmaController::release_channel(static_cast<uint8_t>(ch));
    *dma_cycles = s_dma_end - start;
    int ok = s_dma_ok ? 1 : 0;
    for (uint32_t i = 0; i < n && ok; ++i) {
        if (s_dma_dst[i] != s_dma_src[i]) ok = 0;
    }
    const uint32_t c0 = mcycle32();
    __builtin_memcpy(s_dma_dst, s_dma_src, n);
    *cpu_cycles = mcycle32() - c0;
    return ok;
}

} // extern "C"
