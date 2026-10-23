/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: step callbacks inside a bus transaction ([SPEC-HAL-12]).
 */

#ifndef ABSTRACTX_HAL_BUS_STEP_HPP
#define ABSTRACTX_HAL_BUS_STEP_HPP

#include <cstddef>
#include <cstdint>
#include <span>

#include "io_status.hpp"

namespace abstractx::hal {

// @impl [SPEC-HAL-12] docs/DESIGN_SPECIFICATION.md
enum class BusStep : uint8_t { Continue, Repeat, Finish, Fail };

// Runs at interrupt level when step `index` completed; `rx` = the bytes that step received.
// Bounded, never blocks, never resumes a coroutine.
using BusStepFn = BusStep (*)(void* ctx, size_t index, std::span<const uint8_t> rx) noexcept;

struct BusStepHook {
    BusStepFn fn{nullptr};
    void*     ctx{nullptr};
    uint16_t  max_repeats{0};   // Repeat beyond this ends the transaction with Timeout
    constexpr explicit operator bool() const noexcept { return fn != nullptr; }
};

// What the driver does after a step: run step `index`, or end with `status`.
struct BusStepNext {
    bool     done{false};
    size_t   index{0};
    IoStatus status{IoStatus::Ok};
};

// The driver-side decision logic, shared by every target so the outcomes match.
class BusStepSequencer {
public:
    void start(size_t steps) noexcept {
        steps_ = steps;
        index_ = 0;
        repeats_ = 0;
        done_ = 0;
    }

    size_t index() const noexcept { return index_; }
    size_t steps_done() const noexcept { return done_; }
    uint16_t repeats() const noexcept { return repeats_; }

    // Step index() finished and received `rx`; ask its hook (none = Continue) what comes next.
    BusStepNext step_done(const BusStepHook& hook, std::span<const uint8_t> rx) noexcept {
        const BusStep s = hook ? hook.fn(hook.ctx, index_, rx) : BusStep::Continue;
        switch (s) {
        case BusStep::Repeat:
            if (repeats_ >= hook.max_repeats) {
                return BusStepNext{true, index_, IoStatus::Timeout};
            }
            ++repeats_;
            return BusStepNext{false, index_, IoStatus::Ok};
        case BusStep::Finish:
            done_ = index_ + 1U;
            return BusStepNext{true, index_, IoStatus::Ok};
        case BusStep::Fail:
            return BusStepNext{true, index_, IoStatus::Aborted};
        case BusStep::Continue:
            break;
        }
        done_ = ++index_;
        repeats_ = 0;
        return BusStepNext{index_ >= steps_, index_, IoStatus::Ok};
    }

private:
    size_t   steps_{0};
    size_t   index_{0};
    size_t   done_{0};
    uint16_t repeats_{0};
};

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_BUS_STEP_HPP
