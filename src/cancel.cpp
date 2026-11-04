/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Cancelling a driver request when its awaiter is destroyed ([SPEC-ARCH-16] requirement 8).
 * Its own file: only programs with driver awaiters pull in the timer it waits with.
 */
#include "abstractx/hal/async_driver.hpp"
#include "abstractx/hal/platform.hpp"

namespace abstractx::hal {

// Weak reference: programs that link the core without a target (some sim tests) have no clock;
// the wait is then bounded by an iteration count instead.
ITimer& get_timer_driver() noexcept __attribute__((weak));

// How long abandon_request waits for a transfer the driver could not abort.
inline constexpr uint64_t kCancelWaitUs = 1'000'000;
inline constexpr uint64_t kCancelWaitSpins = 1'000'000'000;  // no clock linked

// @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (8: abort, else wait)
ABSTRACTX_COLD void abandon_request(ICancellable& driver, RequestStateWord& state) noexcept {
    {
        InterruptGuard guard;  // the same guard as complete(): no interleaving
        if (state.load(std::memory_order_acquire) != kReqPending) return;  // Done or never submitted
        state.store(kReqAbandoned, std::memory_order_release);
    }
    if (driver.cancel_request(&state)) return;
    const bool have_clock = &get_timer_driver != nullptr;
    const uint64_t t0 = have_clock ? get_timer_driver().get_time_us() : 0;
    uint64_t spins = 0;
    while (state.load(std::memory_order_acquire) != kReqDone) {
        const bool expired = have_clock ? (get_timer_driver().get_time_us() - t0 > kCancelWaitUs)
                                        : (++spins > kCancelWaitSpins);
        if (expired) {
            __builtin_trap();  // the driver never finished the transfer: a firmware fault, reported
        }
    }
}

} // namespace abstractx::hal
