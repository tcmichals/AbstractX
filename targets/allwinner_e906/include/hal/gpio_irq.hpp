/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * GPIO edge interrupts on the Allwinner T527 main PIO, delivered to the E906.
 * @impl [SPEC-E906-IRQ-07] targets/allwinner_e906/SPECIFICATION.md
 */
#pragma once

#include <stdint.h>

namespace hal::gpio_irq {

// Pin number = bank * 32 + pin, as Linux numbers gpiochip1 (PI8 = 8 * 32 + 8 = 264).
constexpr uint32_t pin(char bank, uint32_t n) noexcept {
    return static_cast<uint32_t>(bank - 'A') * 32U + n;
}
inline constexpr uint32_t kImuDrdy = pin('I', 8);   // header pin 29, ICM-42688-P INT1

// Called in interrupt context with `mcycle` (low 32 bits) taken at handler entry.
// It must not resume a coroutine ([SPEC-E906-IRQ-01]).
using Callback = void (*)(void* ctx, uint32_t gpio, uint32_t edge_cycles);

// Route the bank's interrupt to the E906 and arm one pin. Only banks whose CLIC line was
// measured are accepted (bank I today). false: unknown bank, or no free slot.
bool arm(uint32_t gpio, bool rising, Callback cb, void* ctx) noexcept;

// Disable the pin's edge interrupt and clear its status; the bank stays routed.
void disarm(uint32_t gpio) noexcept;

// Edges seen on an armed pin since boot (0 for a pin never armed).
uint32_t edges(uint32_t gpio) noexcept;

} // namespace hal::gpio_irq
