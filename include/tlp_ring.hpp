/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX C++20 Ring Buffers: SPSC (1:1 Dedicated), Channel Multiplexer & MPSC (Shared Sinks)
 * ---------------------------------------------------------------------------------------------
 * Standardized under [SPEC-TLP-03] into three explicit, non-misleading concurrency topologies:
 * 
 * 1. Point-to-Point (1 Producer : 1 Consumer) -> DedicatedSpscChannel<Cap> / SpscRingBuffer<T, Cap>:
 *    - Strictly for isolated 1:1 hardware DMA channels (e.g. PIO DMA -> Core 1, UartRxRingBase).
 *    - WARNING: NEVER use for shared telemetry sinks or multi-task queues. Concurrent push()
 *      causes race conditions on head_ and tail_ resulting in torn writes and queue corruption.
 * 
 * 2. Channel Multiplexer (N Private Channels -> 1 Consumer) -> TlpChannelMultiplexer<N, Cap> / SpscChannelArray<T, N, Cap>:
 *    - Each distinct producer (Core 0 I/O, PIO DMA, I2C coroutines) owns a private SPSC channel.
 *    - Single consumer drains channels round-robin or via priority with ZERO atomic write contention.
 * 
 * 3. Shared Event Queue (N Producers -> 1 Consumer) -> MpscEventQueue<Cap> / MpscRingBuffer<T, Cap>:
 *    - Shared event sinks where multiple threads, coroutines, or ISRs push into a single queue.
 *    - Serialized safely via nesting-aware InterruptLock (bare-metal), hardware spinlocks (RP2350),
 *      or atomic ticket reservation.
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
        return current_tail - current_head;
    }

    bool empty() const noexcept { return size() == 0; }
    bool full() const noexcept { return size() >= Capacity; }

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

// Explicit alias for dedicated point-to-point 1:1 SPSC channels ([SPEC-TLP-03])
// WARNING: Strictly 1 Producer : 1 Consumer. Never use for shared sinks.
// @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
template <size_t Capacity = ABSTRACTX_TLP_RING_CAPACITY>
using DedicatedSpscChannel = SpscTlpRing<Capacity>;

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

// First-class abstraction for multiplexing N private dedicated SPSC channels to 1 consumer ([SPEC-TLP-03])
// Eliminates multi-producer lock contention by giving each producer (e.g. Core 0, PIO DMA, I2C) a private channel.
// @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
template <size_t NumChannels = 4, size_t Capacity = 16>
using TlpChannelMultiplexer = SpscChannelArray<Tlp64, NumChannels, Capacity>;

// ---------------------------------------------------------------------------
// ISR-Safe Multi-Producer Single-Consumer (MPSC) Ring Buffer
// ---------------------------------------------------------------------------

// Explicit alias exposing ETL's interrupt-locked queue under true MPSC semantics ([SPEC-TLP-03]).
// Note: ETL names this template `queue_spsc_isr`, but when instantiated with `InterruptLock`,
// all non-ISR push() operations acquire InterruptLock, serializing concurrent producers.
// On bare-metal, InterruptLock disables interrupts, preventing ISR preemption during push.
// Hence, it functions as a true Multi-Producer Single-Consumer (MPSC) queue.
// The ETL memory model sets the index width: SMALL holds at most 255 entries (a 512-byte UART
// ring failed to compile on the Pico 2 W), so pick the smallest model that fits Capacity.
template <size_t Capacity>
inline constexpr size_t kEtlMemoryModel =
    Capacity <= 255U   ? etl::memory_model::MEMORY_MODEL_SMALL :
    Capacity <= 65535U ? etl::memory_model::MEMORY_MODEL_MEDIUM :
                         etl::memory_model::MEMORY_MODEL_LARGE;

template <typename T, size_t Capacity = 64>
using EtlMpscQueue = etl::queue_spsc_isr<T,
                                         Capacity,
                                         InterruptLock,
                                         kEtlMemoryModel<Capacity>>;

// @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
template <typename T, size_t Capacity = 64>
class MpscRingBuffer {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity MUST be a power of 2");

public:
    using value_type = T;
    using QueueType = EtlMpscQueue<T, Capacity>;

    constexpr MpscRingBuffer() noexcept = default;

    // Safe from any producer context (cooperative coroutine or ISR via nesting InterruptLock)
    bool push(const T& item) noexcept {
        return queue_.push(item);
    }

    // Fast path when caller is already inside a bare-metal ISR with interrupts masked
    bool push_from_isr(const T& item) noexcept {
#if ABSTRACTX_LOCKED_ISR_PATHS  // hosted threads or SMP cores: take the lock ([SPEC-ARCH-01])
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
#if ABSTRACTX_LOCKED_ISR_PATHS  // hosted threads or SMP cores: take the lock ([SPEC-ARCH-01])
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
#if ABSTRACTX_LOCKED_ISR_PATHS  // hosted threads or SMP cores: take the lock ([SPEC-ARCH-01])
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

// Explicit alias for multi-producer shared event queues ([SPEC-TLP-03])
// Synchronized safely across coroutines and ISRs via nesting-aware InterruptLock.
// @impl [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md#spec-tlp-03
template <size_t Capacity = ABSTRACTX_TLP_RING_CAPACITY>
using MpscEventQueue = MpscTlpRing<Capacity>;

} // namespace abstractx

#endif // TLP_RING_HPP
