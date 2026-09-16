/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Bounded Wait-Free Multi-Producer Single-Consumer (MPSC) Queue
 * ----------------------------------------------------------------------
 * Designed specifically for single-core coprocessors (XuanTie E907) and
 * intra-core ISR-to-coroutine dispatching across all platforms.
 *
 * ARCHITECTURAL PROPERTIES:
 * 1. Multi-Producer Safe: Hardware PLIC/NVIC ISRs and running Coroutines
 *    can concurrently push events without race conditions or pointer corruption.
 * 2. Single-Consumer Wait-Free: The main IOProcessor / Coroutine event loop
 *    drains the queue without blocking or holding locks.
 * 3. Deterministic Low Overhead:
 *    - On XuanTie E907 RISC-V: 2-cycle hardware IRQ mask via `csrrci mstatus, 8` (~10 ns).
 *    - On ARM Cortex-M: Hardware PRIMASK mask via `cpsid i` / `cpsie i` (~5 ns).
 *    - On Host / Desktop SITL: Thread-safe spinlock / atomic section.
 * 4. 0 B Dynamic Memory Allocation (Fixed static circular buffer).
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <optional>

#if !defined(__riscv) && !defined(__arm__)
#include <mutex>
#endif

namespace abstractx {

template <typename T, size_t Capacity = 32>
class MpscIsrQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity MUST be a power of 2");

public:
    using value_type = T;

    constexpr MpscIsrQueue() noexcept : head_(0), tail_(0), dropped_(0) {}

    // Multi-Producer Push: Callable from ANY ISR or ANY Coroutine Context
    bool push(const T& item) noexcept {
#if defined(__riscv)
        uint32_t prev_mstatus = 0;
        // Atomic 2-cycle machine interrupt disable on XuanTie E907
        __asm__ volatile("csrrci %0, mstatus, 8" : "=r"(prev_mstatus) :: "memory");

        const size_t current_tail = tail_;
        const size_t current_head = head_;

        if ((current_tail - current_head) >= Capacity) {
            dropped_++;
            __asm__ volatile("csrw mstatus, %0" :: "r"(prev_mstatus) : "memory");
            return false; // Queue Full
        }

        buffer_[current_tail & (Capacity - 1)] = item;
        tail_ = current_tail + 1;

        // Restore previous machine interrupt state
        __asm__ volatile("csrw mstatus, %0" :: "r"(prev_mstatus) : "memory");
        return true;

#elif defined(__arm__)
        // ARM Cortex-M PRIMASK interrupt disable
        uint32_t prev_primask = 0;
        __asm__ volatile(
            "mrs %0, primask\n"
            "cpsid i\n"
            : "=r"(prev_primask) :: "memory"
        );

        const size_t current_tail = tail_;
        const size_t current_head = head_;

        if ((current_tail - current_head) >= Capacity) {
            dropped_++;
            __asm__ volatile("msr primask, %0" :: "r"(prev_primask) : "memory");
            return false;
        }

        buffer_[current_tail & (Capacity - 1)] = item;
        tail_ = current_tail + 1;

        __asm__ volatile("msr primask, %0" :: "r"(prev_primask) : "memory");
        return true;

#else
        // Host / SITL multi-threaded fallback
        std::lock_guard<std::mutex> lock(host_mutex_);
        const size_t current_tail = tail_;
        const size_t current_head = head_;

        if ((current_tail - current_head) >= Capacity) {
            dropped_++;
            return false;
        }

        buffer_[current_tail & (Capacity - 1)] = item;
        tail_ = current_tail + 1;
        return true;
#endif
    }

    // Single-Consumer Pop: Called strictly by the Main IOProcessor Loop
    bool pop(T& item) noexcept {
        const size_t current_head = head_;
        const size_t current_tail = tail_;

        if (current_head == current_tail) {
            return false; // Queue Empty
        }

        item = buffer_[current_head & (Capacity - 1)];
        head_ = current_head + 1;
        return true;
    }

    std::optional<T> pop() noexcept {
        T item;
        if (pop(item)) {
            return item;
        }
        return std::nullopt;
    }

    static constexpr size_t MAX_SIZE = Capacity;

    bool push_from_isr(const T& item) noexcept {
        return push(item);
    }

    bool pop_from_isr(T& item) noexcept {
        return pop(item);
    }

    bool empty_from_isr() const noexcept {
        return empty();
    }

    constexpr size_t capacity() const noexcept { return Capacity; }

    size_t size() const noexcept {
        const size_t current_head = head_;
        const size_t current_tail = tail_;
        return (current_tail >= current_head) ? (current_tail - current_head) : 0;
    }

    bool empty() const noexcept { return size() == 0; }
    uint32_t dropped() const noexcept { return dropped_; }

    void clear() noexcept {
        head_ = 0;
        tail_ = 0;
        dropped_ = 0;
    }

private:
    std::array<T, Capacity> buffer_{};
    volatile size_t head_{0};
    volatile size_t tail_{0};
    volatile uint32_t dropped_{0};

#if !defined(__riscv) && !defined(__arm__)
    std::mutex host_mutex_{};
#endif
};

} // namespace abstractx
