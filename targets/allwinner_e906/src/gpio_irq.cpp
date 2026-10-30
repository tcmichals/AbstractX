/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GPIO edge interrupts on the Allwinner T527 main PIO, delivered to the E906.
 * @impl [SPEC-E906-IRQ-07] targets/allwinner_e906/SPECIFICATION.md
 */
#include "hal/gpio_irq.hpp"

#include <stddef.h>
#include "abstractx/interrupt_lock.hpp"
#include "hal/clic.hpp"
#include "memory_map.h"

namespace hal::gpio_irq {
namespace {

// @impl [SPEC-E906-IRQ-07] targets/allwinner_e906/SPECIFICATION.md
constexpr uintptr_t kEintBase = PIO_BASE + 0x200U;   // + 0x20 * bank: CFG0..3, CTL, STA, DEB
constexpr uint32_t  kFuncEint = 0xEU;

struct Bank {
    char     name;
    uint32_t gic;
};
// Banks whose interrupt was measured on the E906 (IRQ table in the target spec).
constexpr Bank kBanks[] = {{'I', 115}};

constexpr size_t kMaxPins = 2;
struct Slot {
    uint32_t gpio;
    Callback cb;
    void*    ctx;
    volatile uint32_t edges;   // shared: line-100 handler increments, main reads; single hart, 32-bit store
};
Slot s_pins[kMaxPins];          // shared: arm() fills under InterruptGuard, handler reads
size_t s_num_pins = 0;          // shared: arm() under InterruptGuard, handler reads
bool s_line_attached[::hal::clic::kNumLines];

inline volatile uint32_t& reg(uintptr_t addr) noexcept {
    return *reinterpret_cast<volatile uint32_t*>(addr);
}
inline uintptr_t cfg_reg(uint32_t gpio) noexcept {   // Pn_CFGx: 8 pins per register
    return PIO_BASE + (gpio / 32U) * 0x30U + ((gpio % 32U) / 8U) * 4U;
}
inline uintptr_t eint(uint32_t gpio, uint32_t off) noexcept {
    return kEintBase + (gpio / 32U) * 0x20U + off;
}
inline uint32_t field_shift(uint32_t gpio) noexcept { return ((gpio % 32U) % 8U) * 4U; }

const Bank* find_bank(uint32_t gpio) noexcept {
    const char name = static_cast<char>('A' + gpio / 32U);
    for (const Bank& b : kBanks) {
        if (b.name == name) return &b;
    }
    return nullptr;
}

inline uint32_t mcycle_lo() noexcept {
    uint32_t v = 0;
#if defined(__riscv)
    asm volatile("csrr %0, mcycle" : "=r"(v));
#endif
    return v;
}

// Shared CLIC line (eight GIC sources): only pins whose status bit is set are ours.
void on_line(void*) noexcept {
    const uint32_t t = mcycle_lo();
    for (size_t i = 0; i < s_num_pins; ++i) {
        Slot& s = s_pins[i];
        const uint32_t bit = 1U << (s.gpio % 32U);
        volatile uint32_t& sta = reg(eint(s.gpio, 0x14U));
        if ((sta & bit) == 0) continue;
        sta = bit;                      // W1C before the callback starts the read
        s.edges = s.edges + 1U;
        if (s.cb) s.cb(s.ctx, s.gpio, t);
    }
}

} // namespace

bool arm(uint32_t gpio, bool rising, Callback cb, void* ctx) noexcept {
    const Bank* bank = find_bank(gpio);
    if (!bank) return false;
    const uint32_t bit = 1U << (gpio % 32U);
    const uint32_t sh = field_shift(gpio);
    const uint32_t line = ::hal::clic::line_for_gic(bank->gic);
    {
        // Pn_CFG1 also holds other drivers' pins (PI11/PI12 = UART3): change only this field.
        abstractx::InterruptGuard guard;
        Slot* slot = nullptr;
        for (size_t i = 0; i < s_num_pins; ++i) {
            if (s_pins[i].gpio == gpio) slot = &s_pins[i];
        }
        if (!slot) {
            if (s_num_pins >= kMaxPins) return false;
            slot = &s_pins[s_num_pins];
            slot->gpio = gpio;
            slot->edges = 0;
            ++s_num_pins;
        }
        slot->cb = cb;
        slot->ctx = ctx;
        reg(eint(gpio, 0x10U)) = reg(eint(gpio, 0x10U)) & ~bit;
        reg(cfg_reg(gpio)) = (reg(cfg_reg(gpio)) & ~(0xFU << sh)) | (kFuncEint << sh);
        const uintptr_t ecfg = eint(gpio, ((gpio % 32U) / 8U) * 4U);
        reg(ecfg) = (reg(ecfg) & ~(0xFU << sh)) | ((rising ? 0U : 1U) << sh);
        reg(eint(gpio, 0x14U)) = bit;   // drop an edge latched before arming
        reg(eint(gpio, 0x10U)) = reg(eint(gpio, 0x10U)) | bit;
    }
    if (!s_line_attached[line]) {
        if (!::hal::irq::attach(line, on_line, nullptr)) return false;
        ::hal::irq::describe(line, "gpio");
        s_line_attached[line] = true;
    }
    ::hal::clic::route_gic(bank->gic, true);
    return true;
}

void disarm(uint32_t gpio) noexcept {
    if (!find_bank(gpio)) return;
    const uint32_t bit = 1U << (gpio % 32U);
    abstractx::InterruptGuard guard;
    reg(eint(gpio, 0x10U)) = reg(eint(gpio, 0x10U)) & ~bit;
    reg(eint(gpio, 0x14U)) = bit;
}

uint32_t edges(uint32_t gpio) noexcept {
    for (size_t i = 0; i < s_num_pins; ++i) {
        if (s_pins[i].gpio == gpio) return s_pins[i].edges;
    }
    return 0;
}

} // namespace hal::gpio_irq
