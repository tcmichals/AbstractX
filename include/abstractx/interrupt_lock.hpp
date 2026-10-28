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
 * On hosted builds it is a real recursive lock (worker threads stand in for ISRs).
 *
 * @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md#spec-arch-01
 */

#ifndef ABSTRACTX_INTERRUPT_LOCK_HPP
#define ABSTRACTX_INTERRUPT_LOCK_HPP

#include <cstdint>

// Hosted builds (Linux SITL, the Cubie A55 / Zynq A9 Linux targets): the
// "ISR" side of a domain queue is a worker thread, so masking interrupts would
// protect nothing. InterruptLock is a real recursive lock there.
#if defined(__linux__) || defined(__APPLE__) || defined(_WIN32)
#define ABSTRACTX_HOSTED 1
#else
#define ABSTRACTX_HOSTED 0
#endif

// Bare-metal targets whose cores share domain queues set ABSTRACTX_SMP_CORES (> 1) and provide
// abstractx::abstractx_core_id() ([SPEC-ARCH-01] cross-core lock). Single-core targets leave it unset.
#if !defined(ABSTRACTX_SMP_CORES)
#define ABSTRACTX_SMP_CORES 1
#endif
#define ABSTRACTX_SMP (!ABSTRACTX_HOSTED && ABSTRACTX_SMP_CORES > 1)

// The "_from_isr" shortcuts (no lock, interrupts already masked) are only safe when nothing else
// can run at the same time: one core, no threads. Hosted and SMP builds take the lock instead.
#define ABSTRACTX_LOCKED_ISR_PATHS (ABSTRACTX_HOSTED || ABSTRACTX_SMP)

#include <atomic>

namespace abstractx {

using interrupt_flags_t = uint32_t;

// Cross-core interrupt lock ([SPEC-ARCH-01]): mask this core's interrupts, then take one spinlock
// shared by all cores. Depth and saved interrupt state are per core: each is written only by its
// own core with that core's interrupts masked. Platform supplies core_id(), irq_save() and
// irq_restore(); the host test runs it with threads as the cores.
// @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md (cross-core lock)
template <typename Platform, uint32_t Cores>
class SmpInterruptLock {
    static_assert(Cores >= 1, "at least one core");

public:
    static void lock() noexcept {
        const interrupt_flags_t flags = Platform::irq_save();
        const uint32_t c = Platform::core_id();
        if (depth_[c] == 0U) {
            while (flag_.test_and_set(std::memory_order_acquire)) {
            }
            saved_[c] = flags;
        }
        ++depth_[c];
    }

    static void unlock() noexcept {
        const uint32_t c = Platform::core_id();
        if (depth_[c] == 0U) {
            return;
        }
        if (--depth_[c] == 0U) {
            const interrupt_flags_t flags = saved_[c];
            flag_.clear(std::memory_order_release);
            Platform::irq_restore(flags);
        }
    }

    static uint32_t depth(uint32_t core) noexcept { return depth_[core]; }

private:
    static inline std::atomic_flag flag_ = ATOMIC_FLAG_INIT;  // shared: every core; test_and_set acquire / clear release
    static inline uint32_t depth_[Cores]{};                   // per core, own core only, its IRQs masked
    static inline interrupt_flags_t saved_[Cores]{};          // per core, as depth_
};

#if ABSTRACTX_HOSTED

// No interrupts to mask on a hosted OS; kept so shared code compiles.
inline interrupt_flags_t disable_interrupts_save_flags() noexcept { return 0U; }
inline void restore_interrupts_flags(interrupt_flags_t flags) noexcept { (void)flags; }

// ETL `TAccess` policy for hosted builds: one process-wide recursive spinlock
// (atomics only, no <mutex>), so a queue shared by threads is consistent.
// @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md#spec-arch-01
class InterruptLock {
public:
    static void lock() noexcept {
        if (depth_++ == 0U) {
            while (flag_.test_and_set(std::memory_order_acquire)) {
            }
        }
    }
    static void unlock() noexcept {
        if (depth_ == 0U) {
            return;
        }
        if (--depth_ == 0U) {
            flag_.clear(std::memory_order_release);
        }
    }

private:
    static inline std::atomic_flag flag_ = ATOMIC_FLAG_INIT;
    static inline thread_local uint32_t depth_{0U};
};

#else // bare metal

// Disable the interrupts that can touch a domain queue and return the prior state.
inline interrupt_flags_t disable_interrupts_save_flags() noexcept {
#if defined(__riscv)
    interrupt_flags_t flags = 0U;
    __asm__ volatile("csrrc %0, mstatus, %1" : "=r"(flags) : "r"(8U) : "memory");
    return flags;
#elif defined(__arm__) && defined(__ARM_ARCH_PROFILE) && (__ARM_ARCH_PROFILE == 'M')
    interrupt_flags_t flags = 0U;
    __asm__ volatile("mrs %0, primask" : "=r"(flags) :: "memory");
    __asm__ volatile("cpsid i" ::: "memory");
    return flags;
#else
#error "InterruptLock: unsupported bare-metal architecture (add its interrupt mask here)"
#endif
}

// Restore the exact interrupt state captured by disable_interrupts_save_flags().
inline void restore_interrupts_flags(interrupt_flags_t flags) noexcept {
#if defined(__riscv)
    if ((flags & 0x8U) != 0U) {
        __asm__ volatile("csrsi mstatus, 8" ::: "memory");
    }
#elif defined(__arm__)
    if (flags == 0U) {
        __asm__ volatile("cpsie i" ::: "memory");
    }
#endif
}

#if ABSTRACTX_SMP
// Target-provided: index of the calling core, 0 .. ABSTRACTX_SMP_CORES - 1.
uint32_t abstractx_core_id() noexcept;

struct BareMetalSmpPlatform {
    static uint32_t core_id() noexcept { return abstractx_core_id(); }
    static interrupt_flags_t irq_save() noexcept { return disable_interrupts_save_flags(); }
    static void irq_restore(interrupt_flags_t f) noexcept { restore_interrupts_flags(f); }
};

// ETL `TAccess` policy on a multi-core target: excludes the other cores too ([SPEC-ARCH-01]).
using InterruptLock = SmpInterruptLock<BareMetalSmpPlatform, ABSTRACTX_SMP_CORES>;
#else
// ETL `TAccess` policy: nesting-aware interrupt mask with exact state restore.
// Single-core only: it does not exclude another core (ABSTRACTX_SMP_CORES > 1 selects the SMP lock).
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
#endif // ABSTRACTX_SMP

#endif // ABSTRACTX_HOSTED

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
