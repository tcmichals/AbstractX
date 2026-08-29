/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Interrupt Lock Policy
 * ------------------------------
 * ETL-compatible access policy (`lock()` / `unlock()`) used as the `TAccess`
 * template argument of `etl::queue_spsc_isr`. It implements the interrupt
 * save/restore contract required by the AbstractX domain rules: the previous
 * interrupt state is saved on the outermost lock and restored exactly on the
 * matching unlock, so nesting never re-enables interrupts early.
 */

#ifndef ABSTRACTX_INTERRUPT_LOCK_HPP
#define ABSTRACTX_INTERRUPT_LOCK_HPP

#include <cstdint>

namespace abstractx {

using interrupt_flags_t = uint32_t;

// Disable the interrupts that can touch a domain queue and return the prior state.
inline interrupt_flags_t disable_interrupts_save_flags() noexcept {
#if defined(__riscv)
    interrupt_flags_t flags = 0U;
    __asm__ volatile("csrrc %0, mstatus, %1" : "=r"(flags) : "r"(8U) : "memory");
    return flags;
#elif defined(__arm__) || defined(__aarch64__)
    interrupt_flags_t flags = 0U;
    __asm__ volatile("mrs %0, primask" : "=r"(flags) :: "memory");
    __asm__ volatile("cpsid i" ::: "memory");
    return flags;
#else
    return 0U;
#endif
}

// Restore the exact interrupt state captured by disable_interrupts_save_flags().
inline void restore_interrupts_flags(interrupt_flags_t flags) noexcept {
#if defined(__riscv)
    if ((flags & 0x8U) != 0U) {
        __asm__ volatile("csrsi mstatus, 8" ::: "memory");
    }
#elif defined(__arm__) || defined(__aarch64__)
    if (flags == 0U) {
        __asm__ volatile("cpsie i" ::: "memory");
    }
#else
    (void)flags;
#endif
}

// ETL `TAccess` policy: nesting-aware interrupt mask with exact state restore.
class InterruptLock {
public:
    static void lock() noexcept {
        const interrupt_flags_t flags = disable_interrupts_save_flags();
        if (depth_ == 0U) {
            saved_flags_ = flags;
        }
        ++depth_;
    }

    static void unlock() noexcept {
        if (depth_ == 0U) {
            return;
        }
        --depth_;
        if (depth_ == 0U) {
            restore_interrupts_flags(saved_flags_);
        }
    }

private:
    // Only mutated with interrupts masked, so plain storage is sufficient.
    static inline uint32_t depth_{0U};
    static inline interrupt_flags_t saved_flags_{0U};
};

// RAII form for critical sections outside of the queue policies.
class InterruptGuard {
public:
    InterruptGuard() noexcept { InterruptLock::lock(); }
    ~InterruptGuard() noexcept { InterruptLock::unlock(); }

    InterruptGuard(const InterruptGuard&) = delete;
    InterruptGuard& operator=(const InterruptGuard&) = delete;
};

} // namespace abstractx

#endif // ABSTRACTX_INTERRUPT_LOCK_HPP
