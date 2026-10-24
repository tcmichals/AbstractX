/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX C++20 ISR-Safe Multi-Producer Single-Consumer (MPSC) TLP Ring Buffer
 * ------------------------------------------------------------------------------
 * Provides MpscRingBuffer<T, Capacity> and MpscTlpRing<Capacity> backed by
 * etl::queue_spsc_isr<T, Capacity, InterruptLock, etl::memory_model::MEMORY_MODEL_SMALL>.
 *
 * Multiple producers on the same core (ISRs, DMA completion callbacks, and cooperative
 * coroutines) are serialized via nesting-aware InterruptLock (csrrci/csrsi mstatus on
 * RISC-V, cpsid/cpsie on Cortex-M, recursive atomic spinlock on hosted OS), preserving
 * strict chronological packet ordering with a single shared static buffer pool.
 */

#ifndef MPSC_TLP_RING_HPP
#define MPSC_TLP_RING_HPP

#include "asp_tlp64.hpp"
#include "abstractx/interrupt_lock.hpp"

#include <cstddef>
#include <optional>
#include <etl/memory_model.h>
#include <etl/queue_spsc_isr.h>

namespace abstractx {

// Default TLP ring capacity: 8 for E906 (matching 7-TLP RPMsg batch payload), 64 for host/desktop
// @impl [SPEC-TLP-03] [SPEC-E906-MEM-01] [SPEC-E906-IPC-04] docs/DESIGN_SPECIFICATION.md#spec-tlp-03 targets/allwinner_e906/SPECIFICATION.md#spec-e906-mem-01
#ifndef ABSTRACTX_TLP_RING_CAPACITY
#if defined(TARGET_ALLWINNER_E906) || defined(CONFIG_RPROC_RPMSG)
#define ABSTRACTX_TLP_RING_CAPACITY 8
#else
#define ABSTRACTX_TLP_RING_CAPACITY 64
#endif
#endif

// @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
template <typename T, size_t Capacity = 64>
class MpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity MUST be a power of 2");

public:
    using value_type = T;
    using QueueType = etl::queue_spsc_isr<T,
                                          Capacity,
                                          InterruptLock,
                                          etl::memory_model::MEMORY_MODEL_SMALL>;

    constexpr MpscRingBuffer() noexcept = default;

    // Safe from any producer context (cooperative coroutine or ISR via nesting InterruptLock)
    bool push(const T& item) noexcept {
        return queue_.push(item);
    }

    // Fast path when caller is already inside a bare-metal ISR with interrupts masked
    bool push_from_isr(const T& item) noexcept {
#if ABSTRACTX_HOSTED
        return queue_.push(item);
#else
        return queue_.push_from_isr(item);
#endif
    }

    std::optional<T> pop() noexcept {
        T item{};
        if (!queue_.pop(item)) {
            return std::nullopt;
        }
        return item;
    }

    bool pop(T& item) noexcept {
        return queue_.pop(item);
    }

    bool pop_from_isr(T& item) noexcept {
#if ABSTRACTX_HOSTED
        return queue_.pop(item);
#else
        return queue_.pop_from_isr(item);
#endif
    }

    constexpr size_t capacity() const noexcept { return Capacity; }

    size_t size() const noexcept {
        return queue_.size();
    }

    bool empty() const noexcept {
#if ABSTRACTX_HOSTED
        return queue_.empty();
#else
        return queue_.empty_from_isr();
#endif
    }

    void clear() noexcept {
        InterruptGuard guard;
        queue_.clear();
    }

private:
    alignas(64) QueueType queue_{};
};

// @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
template <size_t Capacity = ABSTRACTX_TLP_RING_CAPACITY>
using MpscTlpRing = MpscRingBuffer<Tlp64, Capacity>;

} // namespace abstractx

#endif // MPSC_TLP_RING_HPP
