/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Live profiler samples ([SPEC-TRACE-09], CTF stream 4) and timeline spans ([SPEC-TRACE-10],
 * CTF stream 5). The platform provides the ring storage; interrupt handlers and the cooperative
 * loop append, status_service() drains into CTF packets.
 */
#pragma once

#include "abstractx/interrupt_lock.hpp"
#include "abstractx/trace/runtime_stats.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace abstractx::events {

inline constexpr uint8_t kProfileStream  = 4;
inline constexpr uint8_t kProfileEvent   = 0x30;
inline constexpr uint8_t kTimelineStream = 5;
inline constexpr uint8_t kTimelineEvent  = 0x31;
inline constexpr uint8_t kVersion        = 1;

inline constexpr uint32_t kOwnerNone = 0;  // loop code outside any resume
inline constexpr uint32_t kOwnerIdle = 1;

enum Kind : uint8_t { kResume = 1, kIsr = 2, kIdle = 3 };

// @impl [SPEC-TRACE-09] [SPEC-TRACE-10] docs/DESIGN_SPECIFICATION.md
#pragma pack(push, 1)
struct ProfileHeader {
    uint8_t  event_id{kProfileEvent};
    uint8_t  version{kVersion};
    uint16_t n{0};
    uint32_t sample_hz{0};
    uint32_t dropped{0};
    uint32_t reserved{0};
};
struct ProfileSample {
    uint32_t pc;
    uint32_t owner;
};
struct TimelineHeader {
    uint8_t  event_id{kTimelineEvent};
    uint8_t  version{kVersion};
    uint16_t n{0};
    uint32_t cycle_hz{0};
    uint64_t cycles_now{0};
    uint32_t dropped{0};
    uint32_t reserved{0};
};
struct TimelineEvent {
    uint32_t start;
    uint32_t duration;
    uint32_t id;
    uint8_t  kind;
    uint8_t  pad[3];
};
#pragma pack(pop)

static_assert(sizeof(ProfileHeader) == 16 && sizeof(ProfileSample) == 8, "wire layout [SPEC-TRACE-09]");
static_assert(sizeof(TimelineHeader) == 24 && sizeof(TimelineEvent) == 16, "wire layout [SPEC-TRACE-10]");

inline constexpr size_t kPacketPayload = 496 - 40;  // RPMsg buffer minus CtfPacketHeaderWire
inline constexpr size_t kMaxSamplesPerPacket = (kPacketPayload - sizeof(ProfileHeader)) / sizeof(ProfileSample);
inline constexpr size_t kMaxEventsPerPacket  = (kPacketPayload - sizeof(TimelineHeader)) / sizeof(TimelineEvent);
static_assert(kMaxSamplesPerPacket == 55 && kMaxEventsPerPacket == 27, "[SPEC-TRACE-09] [SPEC-TRACE-10]");

inline constexpr uint64_t kProfileFlushUs  = 100'000;
inline constexpr uint64_t kTimelineFlushUs = 50'000;

// Single-index ring: head/tail run freely, capacity is a power of two.
template <typename T>
struct Ring {
    T*                    items{nullptr};
    uint32_t              mask{0};
    std::atomic<uint32_t> head{0};   // producers (under InterruptGuard for spans)
    std::atomic<uint32_t> tail{0};   // consumer: status_service()
    std::atomic<uint32_t> dropped{0};
    uint64_t              first_wait_us{0};  // when the oldest waiting item was first seen

    void attach(T* storage, uint32_t capacity) noexcept {
        items = (capacity && (capacity & (capacity - 1)) == 0) ? storage : nullptr;
        mask = items ? capacity - 1 : 0;
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        dropped.store(0, std::memory_order_relaxed);
        first_wait_us = 0;
    }
    uint32_t size() const noexcept {
        return head.load(std::memory_order_acquire) - tail.load(std::memory_order_relaxed);
    }
    bool push(const T& v) noexcept {
        if (!items) return false;
        const uint32_t h = head.load(std::memory_order_relaxed);
        if (h - tail.load(std::memory_order_acquire) > mask) {
            dropped.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        items[h & mask] = v;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
};

struct State {
    Ring<ProfileSample> samples{};
    Ring<TimelineEvent> spans{};
    uint32_t            sample_hz{0};
    bool                timeline_on{false};
};

inline State g_events{};

// Interrupt context, one producer (the platform's sample timer).
inline void sample(uint32_t pc) noexcept {
    const auto& st = stats::g_state;
    const uint32_t owner = st.idle ? kOwnerIdle
                         : st.owner ? static_cast<uint32_t>(st.owner->frame) : kOwnerNone;
    (void)g_events.samples.push(ProfileSample{pc, owner});
}

// Any context: loop (resume / idle spans) and interrupt handlers share the ring.
inline void span(Kind kind, uint32_t id, uint64_t start_cycles, uint64_t cycles) noexcept {
    if (!g_events.timeline_on) return;
    const TimelineEvent e{static_cast<uint32_t>(start_cycles),
                          cycles > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(cycles),
                          id, kind, {0, 0, 0}};
    InterruptGuard guard;
    (void)g_events.spans.push(e);
}

inline void span_hook(uint8_t kind, uint32_t id, uint64_t start, uint64_t cycles) noexcept {
    span(static_cast<Kind>(kind), id, start, cycles);
}

inline void attach_profile(ProfileSample* storage, uint32_t capacity, uint32_t hz) noexcept {
    g_events.samples.attach(storage, capacity);
    g_events.sample_hz = g_events.samples.items ? hz : 0;
}

inline void attach_timeline(TimelineEvent* storage, uint32_t capacity, bool on) noexcept {
    g_events.spans.attach(storage, capacity);
    g_events.timeline_on = on && g_events.spans.items;
    stats::g_state.span_hook = g_events.timeline_on ? span_hook : nullptr;
}

} // namespace abstractx::events
