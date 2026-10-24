/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX C++20 Ring Buffers: SPSC (Inter-Core / AMP) & MPSC (ISR-Safe / Intra-Core)
 * -------------------------------------------------------------------------------------
 * Provides:
 * 1. SpscRingBuffer<T, Capacity>: Zero-allocation, lock-free, wait-free ring buffer
 *    with cache-line alignment (alignas(64)) to prevent false sharing across cores.
 * 2. SpscTlpRing<Capacity>: Tlp64-specialized alias for single-producer inter-core channels.
 * 3. SpscChannelArray<T, NumChannels, Capacity>: Dedicated per-device lock-free ring array
 *    enabling mutex-free multi-threaded I/O dispatches to a single main consumer loop.
 * 4. MpscRingBuffer<T, Capacity>: ISR-safe multi-producer single-consumer ring buffer backed by
 *    etl::queue_spsc_isr<T, Capacity, InterruptLock, etl::memory_model::MEMORY_MODEL_SMALL>,
 *    serializing concurrent ISR and coroutine producers via nesting-aware InterruptLock.
 * 5. MpscTlpRing<Capacity>: Tlp64-specialized alias for intra-core multi-producer telemetry.
 */

#ifndef TLP_RING_HPP
#define TLP_RING_HPP

#include "asp_tlp64.hpp"
#include "abstractx/interrupt_lock.hpp"

#include <array>
#include <atomic>
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

// ---------------------------------------------------------------------------
// Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer
// ---------------------------------------------------------------------------

// @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
// @status Complete
template <typename T, size_t Capacity = 64>
class SpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity MUST be a power of 2");

public:
    using value_type = T;
    constexpr SpscRingBuffer() noexcept : head_(0), tail_(0) {}

    // Called strictly by the dedicated Single Producer thread / ISR
    bool push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_acquire);

        if ((current_tail - current_head) >= Capacity) {
            return false; // Ring buffer full
        }

        buffer_[current_tail & (Capacity - 1)] = item;
        // Release barrier guarantees data write finishes before tail increments
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Called strictly by the Single Consumer (Main Flight Loop) thread
    std::optional<T> pop() noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return std::nullopt; // Ring buffer empty
        }

        T item = buffer_[current_head & (Capacity - 1)];
        // Release barrier notifies producer that slot is now free
        head_.store(current_head + 1, std::memory_order_release);
        return item;
    }

    // Direct reference pop for zero-copy optimization
    bool pop(T& item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return false;
        }

        item = buffer_[current_head & (Capacity - 1)];
        head_.store(current_head + 1, std::memory_order_release);
        return true;
    }

    constexpr size_t capacity() const noexcept { return Capacity; }

    size_t size() const noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        return (current_tail >= current_head) ? (current_tail - current_head) : 0;
    }

    bool empty() const noexcept { return size() == 0; }

    void clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

private:
    alignas(64) std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

// SPSC TLP ring default capacity alias
template <size_t Capacity = ABSTRACTX_TLP_RING_CAPACITY>
using SpscTlpRing = SpscRingBuffer<Tlp64, Capacity>;

// Dedicated Per-Device Lock-Free Ring Array
template <typename T, size_t NumChannels = 4, size_t Capacity = 16>
class SpscChannelArray {
public:
    // Producer pushes to its dedicated channel index [0..NumChannels-1]
    bool push(size_t channel_idx, const T& item) noexcept {
        if (channel_idx >= NumChannels) return false;
        return channels_[channel_idx].push(item);
    }

    // Consumer polls specific channel
    bool pop(size_t channel_idx, T& item) noexcept {
        if (channel_idx >= NumChannels) return false;
        return channels_[channel_idx].pop(item);
    }

    // Round-robin drain across all channels (single consumer)
    template <typename Handler>
    size_t drain_all(Handler&& handler) {
        size_t processed = 0;
        T item;
        for (size_t ch = 0; ch < NumChannels; ++ch) {
            while (channels_[ch].pop(item)) {
                handler(ch, item);
                processed++;
            }
        }
        return processed;
    }

    constexpr size_t num_channels() const noexcept { return NumChannels; }

private:
    std::array<SpscRingBuffer<T, Capacity>, NumChannels> channels_{};
};

// ---------------------------------------------------------------------------
// ISR-Safe Multi-Producer Single-Consumer (MPSC) Ring Buffer
// ---------------------------------------------------------------------------

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

// MPSC TLP ring alias
// @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
template <size_t Capacity = ABSTRACTX_TLP_RING_CAPACITY>
using MpscTlpRing = MpscRingBuffer<Tlp64, Capacity>;

} // namespace abstractx

#endif // TLP_RING_HPP
