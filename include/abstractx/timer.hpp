/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX ETL-Backed Asynchronous Coroutine Timer Service
 * ---------------------------------------------------------
 * Zero-allocation, deterministic software timer wheel based on ETL callback_timer_atomic.
 * Integrates with AbstractX IsrDispatcher for non-blocking 'co_await sleep_ms(N)'.
 *
 * Cross-Platform: RP2040, ESP32-P4, XuanTie E907, STM32, Host Workstation.
 */

#ifndef ABSTRACTX_TIMER_HPP
#define ABSTRACTX_TIMER_HPP

#include <stdint.h>
#include <coroutine>
#include <atomic>
#include <utility>
#include "isr_dispatcher.hpp"
#include <etl/callback_timer_atomic.h>

namespace abstractx {

template <size_t MaxTimers = 32>
class GenericTimerService {
public:
    using TimerBackend = etl::callback_timer_atomic<MaxTimers, std::atomic<int32_t>>;

    static inline void init() {
        timer_backend_.enable(true);
    }

    // Called from hardware timer tick ISR (e.g. 1 kHz SysTick / MTIMER interrupt)
    static inline void tick(uint32_t count = 1) {
        timer_backend_.tick(count);
    }

    /* Asynchronous Coroutine Sleep Awaiter */
    struct AsyncSleepAwaiter {
        uint32_t period_ms;
        etl::timer::id::type timer_id{etl::timer::id::NO_TIMER};
        std::coroutine_handle<> handle{nullptr};
        int8_t allocated_slot{-1};

        explicit AsyncSleepAwaiter(uint32_t ms) : period_ms(ms) {}

        bool await_ready() const noexcept {
            return (period_ms == 0);
        }

        void await_suspend(std::coroutine_handle<> h) noexcept {
            handle = h;
            for (size_t i = 0; i < MaxTimers; ++i) {
                if (!slot_active_[i]) {
                    slot_active_[i] = true;
                    slot_handles_[i] = handle;
                    allocated_slot = static_cast<int8_t>(i);
                    auto cb = get_callback(i);
                    timer_id = timer_backend_.register_timer(cb, period_ms, etl::timer::mode::SINGLE_SHOT);
                    if (timer_id != etl::timer::id::NO_TIMER) {
                        timer_backend_.start(timer_id);
                    }
                    break;
                }
            }
        }

        void await_resume() noexcept {
            if (timer_id != etl::timer::id::NO_TIMER) {
                timer_backend_.unregister_timer(timer_id);
                timer_id = etl::timer::id::NO_TIMER;
            }
            if (allocated_slot >= 0 && allocated_slot < static_cast<int8_t>(MaxTimers)) {
                slot_active_[allocated_slot] = false;
                allocated_slot = -1;
            }
        }
    };

    static inline AsyncSleepAwaiter sleep_ms(uint32_t ms) {
        return AsyncSleepAwaiter(ms);
    }

private:
    static inline TimerBackend timer_backend_;
    static inline std::coroutine_handle<> slot_handles_[MaxTimers];
    static inline bool slot_active_[MaxTimers]{false};

    template <size_t Slot>
    static void timer_expired_slot() {
        std::coroutine_handle<> h = slot_handles_[Slot];
        slot_active_[Slot] = false;
        if (h) {
            IsrDispatcher::isr_post_resume(h);
        }
    }

    template <size_t... Is>
    static typename TimerBackend::callback_type get_callback_helper(size_t index, std::index_sequence<Is...>) {
        using CallbackFn = typename TimerBackend::callback_type;
        static const CallbackFn table[] = {
            CallbackFn::template create<timer_expired_slot<Is>>()...
        };
        return (index < sizeof...(Is)) ? table[index] : CallbackFn();
    }

    static typename TimerBackend::callback_type get_callback(size_t index) {
        return get_callback_helper(index, std::make_index_sequence<MaxTimers>{});
    }
};

using TimerService = GenericTimerService<32>;

inline TimerService::AsyncSleepAwaiter sleep_ms(uint32_t ms) {
    return TimerService::sleep_ms(ms);
}

} // namespace abstractx

#endif // ABSTRACTX_TIMER_HPP
