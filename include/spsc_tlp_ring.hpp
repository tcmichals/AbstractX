/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX C++20 Lock-Free Wait-Free Single-Producer Single-Consumer (SPSC) 64B TLP Ring
 */

#ifndef SPSC_TLP_RING_HPP
#define SPSC_TLP_RING_HPP

#include "asp_tlp64.hpp"
#include <atomic>
#include <cstddef>
#include <array>
#include <optional>

namespace abstractx {

// Zero-allocation, wait-free, lock-free SPSC ring buffer operating on 64-byte TLPs
template <size_t Capacity = 64>
class SpscTlpRing {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity MUST be a power of 2");

public:
    constexpr SpscTlpRing() noexcept : head_(0), tail_(0) {}

    // Push a 64-byte TLP into the ring (called strictly by Single Producer)
    bool push(const Tlp64& packet) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t current_head = head_.load(std::memory_order_acquire);

        if ((current_tail - current_head) >= Capacity) {
            return false; // Ring buffer full (drop or overflow handling)
        }

        buffer_[current_tail & (Capacity - 1)] = packet;
        tail_.store(current_tail + 1, std::memory_order_release);
        return true;
    }

    // Pop a 64-byte TLP from the ring (called strictly by Single Consumer)
    std::optional<Tlp64> pop() noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_acquire);

        if (current_head == current_tail) {
            return std::nullopt; // Ring buffer empty
        }

        Tlp64 packet = buffer_[current_head & (Capacity - 1)];
        head_.store(current_head + 1, std::memory_order_release);
        return packet;
    }

    constexpr size_t capacity() const noexcept { return Capacity; }

    size_t size() const noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        return (current_tail >= current_head) ? (current_tail - current_head) : 0;
    }

    bool empty() const noexcept { return size() == 0; }

private:
    alignas(64) std::array<Tlp64, Capacity> buffer_{};
    alignas(64) std::atomic<size_t> head_{0};
    alignas(64) std::atomic<size_t> tail_{0};
};

} // namespace abstractx

#endif // SPSC_TLP_RING_HPP
