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
#include "abstractx/interrupt_lock.hpp"
#include "hal/clic.hpp"
#include "hal/rpmsg.hpp"
#include "hal/timer.hpp"
#include "spsc_tlp_ring.hpp"

#include <cstddef>
#include <cstdint>

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
using U_HALT = Reg<kUart3 + 0xA4>;  // bit1 CHCFG_AT_BUSY, bit2 CHANGE_UPDATE

constexpr uint32_t kIerRx = 0x01;   // ERBFI: RX data / timeout
constexpr uint32_t kIerTx = 0x02;   // ETBEI: THR empty
constexpr size_t kUartFifo = 64;

abstractx::SpscRingBuffer<uint8_t, 1024> s_uart_rx;  // ISR -> VM
abstractx::SpscRingBuffer<uint8_t, 256> s_uart_tx;   // VM -> ISR
volatile uint32_t s_uart_rx_dropped = 0;
bool s_uart_attached = false;

void uart3_isr(void*) noexcept {
    for (int guard = 0; guard < 8; ++guard) {
        const uint32_t iir = U_IIR::r() & 0x0FU;
        if (iir == 0x01U) {
            return;  // nothing pending (0x1). Busy detect is 0x7, so not a bit-0 test.
        }
        switch (iir) {
            case 0x04:  // RX data available
            case 0x0C:  // RX timeout
                while (U_LSR::r() & 0x01U) {
                    if (!s_uart_rx.push(static_cast<uint8_t>(U_RBR::r() & 0xFFU))) {
                        s_uart_rx_dropped = s_uart_rx_dropped + 1;
                    }
                }
                break;
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
            case 0x06: U_LSR::touch(); break;  // line status
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
struct TwiXfer {
    volatile TwiState state = TwiState::Idle;
    uint8_t addr = 0;
    int16_t reg = -1;
    bool reg_sent = false;
    bool read_phase = false;
    uint8_t wbuf[256]{};
    uint8_t rbuf[256]{};
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
    U_USR::touch();                     // clear any busy-detect raised before this
    U_MCR::r() = 0x03;

    uint8_t b = 0;
    while (s_uart_rx.pop(b)) {}
    while (s_uart_tx.pop(b)) {}

    hal::clic::route_gic(hal::clic::kGicUart3, true);
    if (!s_uart_attached) {
        s_uart_attached = hal::irq::attach(hal::clic::line_for_gic(hal::clic::kGicUart3), uart3_isr, nullptr);
        // @impl [SPEC-E906-REPL-18] apps/e906_repl_app/SPECIFICATION.md
        hal::irq::describe(hal::clic::line_for_gic(hal::clic::kGicUart3), "uart3", &s_uart_rx_dropped);
    } else {
        hal::clic::enable(hal::clic::line_for_gic(hal::clic::kGicUart3));
    }
    U_IER::r() = kIerRx;
    return s_uart_attached;
}

size_t e906_uart3_read(uint8_t* dst, size_t max_len) {
    size_t n = 0;
    while (n < max_len && s_uart_rx.pop(dst[n])) {
        ++n;
    }
    return n;
}

size_t e906_uart3_write(const uint8_t* src, size_t len) {
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
    if (wlen > sizeof(s_twi.wbuf) || rlen > sizeof(s_twi.rbuf)) return -1;
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

} // extern "C"
