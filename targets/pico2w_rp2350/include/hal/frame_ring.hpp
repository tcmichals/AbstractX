/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef TARGETS_PICO2W_HAL_FRAME_RING_HPP
#define TARGETS_PICO2W_HAL_FRAME_RING_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace abstractx::hal::pico {

// Core 1 -> Core 0 frame ring: one producer (Core 1 thread context), one consumer (Core 0).
// The producer fills a slot in place and publishes it; the consumer sends it and frees it.
// @impl [SPEC-PICO-CORE-01] targets/pico2w_rp2350/SPECIFICATION.md
template <size_t SlotBytes, size_t Slots>
class FrameRing {
    static_assert(Slots >= 2 && (Slots & (Slots - 1)) == 0, "Slots must be a power of two");
    static_assert(SlotBytes % 8 == 0, "SlotBytes must keep slots 8-byte aligned");

public:
    static constexpr size_t kSlotBytes = SlotBytes;
    static constexpr size_t kSlots = Slots;

    // Producer: the next free slot (the same one until commit), or empty if the ring is full.
    std::span<uint8_t> acquire() noexcept {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= Slots) {
            return {};
        }
        return {slots_[head & kMask].data, SlotBytes};
    }

    // Producer: publish the acquired slot with used bytes. used == 0 abandons it.
    bool commit(std::span<uint8_t> buffer, size_t used) noexcept {
        const uint32_t head = head_.load(std::memory_order_relaxed);
        Slot& s = slots_[head & kMask];
        if (buffer.data() != s.data || used == 0 || used > SlotBytes) {
            return false;
        }
        s.len = static_cast<uint16_t>(used);
        head_.store(head + 1U, std::memory_order_release);
        return true;
    }

    // Producer: copy one frame into a slot and publish it; counts a drop if full or too big.
    bool push(const uint8_t* data, size_t len) noexcept {
        if (data == nullptr || len == 0 || len > SlotBytes) {
            dropped_.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        std::span<uint8_t> slot = acquire();
        if (slot.empty()) {
            dropped_.fetch_add(1U, std::memory_order_relaxed);
            return false;
        }
        for (size_t i = 0; i < len; ++i) {
            slot[i] = data[i];
        }
        return commit(slot, len);
    }

    // Producer: count a frame the caller could not place (acquire returned empty).
    void note_drop() noexcept { dropped_.fetch_add(1U, std::memory_order_relaxed); }

    // Consumer: oldest published frame, or empty. Valid until release().
    std::span<const uint8_t> peek() const noexcept {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return {};
        }
        const Slot& s = slots_[tail & kMask];
        return {s.data, s.len};
    }

    // Consumer: free the frame returned by peek().
    void release() noexcept {
        const uint32_t tail = tail_.load(std::memory_order_relaxed);
        if (tail != head_.load(std::memory_order_acquire)) {
            tail_.store(tail + 1U, std::memory_order_release);
        }
    }

    size_t size() const noexcept {
        return head_.load(std::memory_order_acquire) - tail_.load(std::memory_order_acquire);
    }
    uint32_t dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }

private:
    static constexpr uint32_t kMask = static_cast<uint32_t>(Slots - 1U);

    struct Slot {
        alignas(8) uint8_t data[SlotBytes];
        uint16_t len;
    };

    // shared: head_ written by Core 1 only, tail_ by Core 0 only; release/acquire publish slots.
    alignas(32) std::atomic<uint32_t> head_{0};
    alignas(32) std::atomic<uint32_t> tail_{0};
    // shared: Core 1 increments, either core reads; relaxed (statistics only).
    std::atomic<uint32_t> dropped_{0};
    // shared: a slot belongs to Core 1 from acquire to commit, to Core 0 from peek to release.
    Slot slots_[Slots]{};
};

} // namespace abstractx::hal::pico

#endif // TARGETS_PICO2W_HAL_FRAME_RING_HPP
