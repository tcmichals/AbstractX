/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Thread-Safe, Lock-Free ISR-to-Thread Coroutine Resume Dispatcher
 * -------------------------------------------------------------------------
 * Provides a wait-free SPSC (Single Producer Single Consumer) queue for safely
 * posting coroutine resumptions from hardware Interrupt Service Routines (ISRs)
 * to be executed on the main CPU thread stack.
 *
 * Cross-Platform: RP2040 (ARM Cortex-M0+), ESP32-P4 (RISC-V), XuanTie E907, STM32.
 */

#ifndef ABSTRACTX_ISR_DISPATCHER_HPP
#define ABSTRACTX_ISR_DISPATCHER_HPP

#include <coroutine>
#include <cstdint>
#include <atomic>

namespace abstractx {

class IsrDispatcher {
public:
    static constexpr size_t QUEUE_CAPACITY = 32; // Must be power of 2
    static constexpr size_t QUEUE_MASK = QUEUE_CAPACITY - 1;

    // Called strictly from ISR / Interrupt context (Top-Half Producer)
    static inline bool isr_post_resume(std::coroutine_handle<> handle) noexcept {
        if (!handle || handle.address() == nullptr) {
            return false;
        }

        uint32_t head = head_.load(std::memory_order_relaxed);
        uint32_t tail = tail_.load(std::memory_order_acquire);

        if ((head - tail) >= QUEUE_CAPACITY) {
            return false; // Queue full - dropped to protect memory bounds
        }

        buffer_[head & QUEUE_MASK] = handle;
        head_.store(head + 1, std::memory_order_release);

#if defined(__riscv)
        __asm__ volatile("fence rw, rw" ::: "memory");
        __asm__ volatile("csrs mip, %0" :: "r"(1 << 3));
#elif defined(__arm__) || defined(__aarch64__)
        __asm__ volatile("dmb ish" ::: "memory");
#endif
        return true;
    }

    // Alias for clean API
    static inline bool post(std::coroutine_handle<> handle) noexcept {
        return isr_post_resume(handle);
    }

    // Called strictly from Main Thread Event Loop (Bottom-Half Consumer)
    static inline void process_ready_coroutines() noexcept {
        uint32_t tail = tail_.load(std::memory_order_relaxed);
        uint32_t head = head_.load(std::memory_order_acquire);

        while (tail != head) {
            std::coroutine_handle<> handle = buffer_[tail & QUEUE_MASK];
            tail++;
            tail_.store(tail, std::memory_order_release);

            // Safe resume check: guarantees handle is non-null and not finished
            if (handle && handle.address() != nullptr && !handle.done()) {
                handle.resume();
            }

            head = head_.load(std::memory_order_acquire);
        }
    }

    // Alias for clean API
    static inline void process() noexcept {
        process_ready_coroutines();
    }

    static inline bool has_pending_resumes() noexcept {
        return head_.load(std::memory_order_relaxed) != tail_.load(std::memory_order_relaxed);
    }

    static inline void reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    static inline std::coroutine_handle<> buffer_[QUEUE_CAPACITY];
    static inline std::atomic<uint32_t> head_{0};
    static inline std::atomic<uint32_t> tail_{0};
};

} // namespace abstractx

#endif // ABSTRACTX_ISR_DISPATCHER_HPP
