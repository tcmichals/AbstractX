/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX C++20 Lock-Free Wait-Free Single-Producer Single-Consumer (SPSC) Ring Buffers
 * --------------------------------------------------------------------------------------
 * Provides:
 * 1. SpscRingBuffer<T, Capacity>: Zero-allocation, lock-free, wait-free ring buffer
 *    with cache-line alignment (alignas(64)) to prevent false sharing.
 * 2. SpscTlpRing<Capacity>: Tlp64-specialized alias.
 * 3. SpscChannelArray<T, NumChannels, Capacity>: Dedicated per-device lock-free ring array
 *    enabling 100% mutex-free multi-threaded I/O dispatches to a single main consumer loop.
 */

#ifndef SPSC_TLP_RING_HPP
#define SPSC_TLP_RING_HPP

#include "asp_tlp64.hpp"
#include <atomic>
#include <cstddef>
#include <array>
#include <optional>

namespace abstractx {

// Zero-allocation, wait-free, lock-free SPSC ring buffer for arbitrary payload T
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

private:
    alignas(64) std::array<T, Capacity> buffer_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

// Backward-compatible alias for 64-byte TLP packets
template <size_t Capacity = 64>
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

} // namespace abstractx

#endif // SPSC_TLP_RING_HPP
