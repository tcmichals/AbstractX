/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * T-Head CLIC and S_INTC group router on the Allwinner T527 XuanTie E906.
 * @impl [SPEC-E906-IRQ-01] [SPEC-E906-IRQ-02] targets/allwinner_e906/SPECIFICATION.md
 */
#pragma once

#include <stdint.h>
#include "abstractx/interrupt_lock.hpp"

namespace hal::clic {

inline constexpr uintptr_t kBase      = 0xE0800000U;
inline constexpr uintptr_t kIntBase   = kBase + 0x1000U;   // CLIC_INT_REGn: IP, IE, ATTR, CTL bytes
inline constexpr uintptr_t kSIntcGint = 0x070210C0U;        // S_INTC GINTC_CONFIG_REG0
inline constexpr uint32_t  kNumLines  = 144;

// Lines measured on the board ([SPEC-E906-IRQ-01] table).
inline constexpr uint32_t kLineTimer  = 7;
inline constexpr uint32_t kLineMsgbox = 17;
inline constexpr uint32_t kLineDma    = 88;
inline constexpr uint32_t kLineGroup0 = 90;   // GIC 32..39; each next line covers 8 more

inline constexpr uint32_t kGicUart3 = 37;
inline constexpr uint32_t kGicTwi1  = 43;

constexpr uint32_t line_for_gic(uint32_t gic) noexcept { return kLineGroup0 + (gic - 32U) / 8U; }

#if defined(__riscv)
inline volatile uint8_t* ctl(uint32_t line) noexcept {
    return reinterpret_cast<volatile uint8_t*>(kIntBase + 4U * line);
}
#else
// Host builds (sim tests compile the RPMsg driver): a RAM stand-in, never MMIO.
inline volatile uint8_t* ctl(uint32_t line) noexcept {
    static uint8_t regs[kNumLines * 4]{};
    return &regs[4U * line];
}
#endif

inline void enable(uint32_t line) noexcept  { ctl(line)[1] = 1; }
inline void disable(uint32_t line) noexcept { ctl(line)[1] = 0; }
inline bool enabled(uint32_t line) noexcept { return ctl(line)[1] != 0; }
inline bool pending(uint32_t line) noexcept { return (ctl(line)[0] & 1U) != 0; }

// Route GIC source `gic` (32..159) through S_INTC to its CLIC group line ([SPEC-E906-IRQ-02]).
inline void route_gic(uint32_t gic, bool on) noexcept {
#if !defined(__riscv)
    (void)gic; (void)on;
    return;
#else
    const uint32_t bit = gic - 32U;
    auto* reg = reinterpret_cast<volatile uint32_t*>(kSIntcGint + 4U * (bit / 32U));
    abstractx::InterruptGuard guard;
    if (on) {
        *reg = *reg | (1U << (bit % 32U));
    } else {
        *reg = *reg & ~(1U << (bit % 32U));
    }
#endif
}

} // namespace hal::clic

extern "C" volatile uint32_t g_e906_diag[16];

namespace hal::irq {

using Handler = void (*)(void* ctx);

// Attach a handler to a CLIC line and enable the line. Several handlers may share a line.
bool attach(uint32_t line, Handler fn, void* ctx) noexcept;
uint32_t count(uint32_t line) noexcept;
uint32_t spurious() noexcept;
// Runtime status ([SPEC-E906-IRQ-05]): a short name for a line and an error counter its
// driver owns (drops, timeouts); cycles spent in the line's handlers.
void describe(uint32_t line, const char* name, const volatile uint32_t* errors = nullptr) noexcept;
uint32_t cycles(uint32_t line) noexcept;
// @impl [SPEC-E906-IRQ-06] targets/allwinner_e906/SPECIFICATION.md
// mepc of the code the current interrupt stopped (valid inside a handler; interrupts do not nest).
uint32_t trap_pc() noexcept;

} // namespace hal::irq
