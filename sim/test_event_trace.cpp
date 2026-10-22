/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: profiler samples [SPEC-TRACE-09] and timeline spans [SPEC-TRACE-10]
 * A fake cycle clock drives 1000 resumes; status_service() drains the span ring into stream-5
 * packets with one span per resume (id = frame, duration = wall cycles), idle spans, drop
 * counting when the ring is not drained, and stream-4 packets carrying PC samples with their
 * owner (frame / idle / none). Steady state: pool high water and dropped resumes do not grow.
 */
// @verifies [SPEC-TRACE-09] docs/DESIGN_SPECIFICATION.md
// @verifies [SPEC-TRACE-10] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "abstractx/abstractx.hpp"
#include "abstractx/trace/event_trace.hpp"

#include "abstractx/hal/platform.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace abstractx;
using namespace abstractx::coro;

namespace {

uint64_t g_clock = 0;
uint64_t fake_clock() noexcept { return g_clock; }

constexpr size_t H = 40;  // CtfPacketHeaderWire

struct Capture {
    uint32_t packets{0};
    uint32_t spans{0};
    uint32_t resume_spans{0};
    uint32_t idle_spans{0};
    uint64_t resume_cycles{0};
    uint32_t wrong_id{0};
    uint32_t last_dropped{0};
    uint32_t samples{0};
    uint8_t  last5[512]{};
    size_t   last5_len{0};
    uint8_t  last4[512]{};
    size_t   last4_len{0};
    uint32_t first_pc{0};
    uint32_t first_owner{0};
};
Capture g_cap;
uint32_t g_worker_frame = 0;

template <typename T>
T rd(const uint8_t* b, size_t off) {
    T v{};
    std::memcpy(&v, b + off, sizeof(T));
    return v;
}

void capture(const uint8_t* data, size_t len, void*) {
    if (len < H || len > 512) return;
    const uint8_t stream = data[4];
    if (stream == events::kTimelineStream) {
        assert(data[H] == events::kTimelineEvent && data[H + 1] == events::kVersion);
        const uint16_t n = rd<uint16_t>(data, H + 2);
        assert(len == H + sizeof(events::TimelineHeader) + n * sizeof(events::TimelineEvent));
        assert(n <= events::kMaxEventsPerPacket);
        ++g_cap.packets;
        g_cap.last_dropped = rd<uint32_t>(data, H + 16);
        for (uint16_t i = 0; i < n; ++i) {
            const size_t o = H + 24 + i * 16u;
            const uint8_t kind = data[o + 12];
            ++g_cap.spans;
            if (kind == events::kResume) {
                ++g_cap.resume_spans;
                g_cap.resume_cycles += rd<uint32_t>(data, o + 4);
                if (rd<uint32_t>(data, o + 8) != g_worker_frame) ++g_cap.wrong_id;
            } else if (kind == events::kIdle && rd<uint32_t>(data, o + 4) == 40u) {
                ++g_cap.idle_spans;  // the 40-cycle sleep of part 2
            }
        }
        std::memcpy(g_cap.last5, data, len);
        g_cap.last5_len = len;
    } else if (stream == events::kProfileStream) {
        assert(data[H] == events::kProfileEvent);
        const uint16_t n = rd<uint16_t>(data, H + 2);
        assert(len == H + sizeof(events::ProfileHeader) + n * sizeof(events::ProfileSample));
        if (g_cap.samples == 0 && n > 0) {
            g_cap.first_pc = rd<uint32_t>(data, H + 16);
            g_cap.first_owner = rd<uint32_t>(data, H + 20);
        }
        g_cap.samples += n;
        std::memcpy(g_cap.last4, data, len);
        g_cap.last4_len = len;
    }
}

// 100 cycles per resume; samples the PC once from inside the resume.
Task<void> worker(int rounds) {
    for (int i = 0; i < rounds; ++i) {
        g_clock += 100;
        if (i == 0) events::sample(0xABCD0000u);
        co_await step_async();
    }
}

void dump(const char* env, const uint8_t* b, size_t len) {
    if (const char* path = std::getenv(env)) {
        if (FILE* f = std::fopen(path, "wb")) {
            std::fwrite(b, 1, len, f);
            std::fclose(f);
        }
    }
}

events::ProfileSample g_samples[64];

// Let the flush wait pass (status_service() uses the platform clock, not the fake cycle clock).
void wait_ms(uint32_t ms) {
    auto& timer = hal::get_timer_driver();
    const uint64_t t0 = timer.get_time_us();
    while (timer.get_time_us() - t0 < ms * 1000ull) {
    }
}

} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 0;  // only the event streams in this test
    init(cfg);
    trace::g_tracer.set_flush_handler(capture, nullptr);
    stats::set_cycle_clock(fake_clock, 1'000'000);
    stats::reset();
    events::attach_profile(g_samples, 64, 1000);
    assert(events::g_events.timeline_on && "timeline on by default with the weak 128-span ring");

    // 1. One span per resume, drained as the loop runs (steady state, no drops).
    constexpr int kRounds = 1000;
    Task<void> t = worker(kRounds);
    g_worker_frame = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(t.handle().address()));
    spawn(std::move(t), "worker");
    const size_t hw0 = coro_pool_high_water();
    const uint32_t dropped0 = Dispatcher::dropped_resumes();
    while (Dispatcher::has_pending_resumes()) {
        Dispatcher::process();
        status_service();
    }
    wait_ms(60);  // past the 50 ms flush wait
    status_service();
    if (g_cap.resume_spans != kRounds + 1 || g_cap.last_dropped != 0) {
        std::fprintf(stderr, "spans %u resume %u dropped %u\n", g_cap.spans, g_cap.resume_spans, g_cap.last_dropped);
    }
    assert(g_cap.resume_spans == kRounds + 1);          // initial run at spawn + one per step_async
    assert(g_cap.resume_cycles == (kRounds + 1) * 100ull - 100);  // last resume only returns
    assert(g_cap.wrong_id == 0);                         // id = the resumed frame
    assert(g_cap.last_dropped == 0);
    assert(coro_pool_high_water() == hw0);
    assert(Dispatcher::dropped_resumes() == dropped0);
    dump("EVENT_TIMELINE_DUMP", g_cap.last5, g_cap.last5_len);

    // 2. Idle span.
    stats::idle_begin();
    g_clock += 40;
    stats::idle_end();
    status_service();                 // sees the span; the 50 ms wait starts now
    assert(g_cap.idle_spans == 0);
    wait_ms(60);
    status_service();
    assert(g_cap.idle_spans == 1);

    // 3. Not drained: a full ring drops and counts, the header reports it.
    const uint32_t before = g_cap.spans;
    for (int i = 0; i < 200; ++i) events::span(events::kIsr, 7, g_clock, 3);
    assert(events::g_events.spans.dropped.load() == 200 - 128);
    for (int i = 0; i < 10; ++i) status_service();
    assert(g_cap.spans - before == 4 * events::kMaxEventsPerPacket);  // full packets go at once
    wait_ms(60);
    status_service();                                                  // the rest after the wait
    assert(g_cap.spans - before == 128 && g_cap.last_dropped == 72);

    // 4. Profiler: owner = frame inside a resume (sampled in step 1), idle, none.
    stats::idle_begin();
    events::sample(0x1111u);
    stats::idle_end();
    events::sample(0x2222u);
    // The sample taken inside the first resume went out on its own after its 100 ms wait.
    assert(g_cap.samples == 1 && g_cap.first_pc == 0xABCD0000u && g_cap.first_owner == g_worker_frame);
    status_service();                 // the 100 ms wait for the new two starts now
    assert(g_cap.samples == 1);
    wait_ms(110);
    status_service();
    assert(g_cap.samples == 3);
    const uint8_t* p = g_cap.last4;
    assert(rd<uint32_t>(p, H + 4) == 1000u);                      // sample_hz
    assert(rd<uint16_t>(p, H + 2) == 2);
    assert(rd<uint32_t>(p, H + 16) == 0x1111u && rd<uint32_t>(p, H + 20) == events::kOwnerIdle);
    assert(rd<uint32_t>(p, H + 24) == 0x2222u && rd<uint32_t>(p, H + 28) == events::kOwnerNone);
    dump("EVENT_PROFILE_DUMP", g_cap.last4, g_cap.last4_len);

    // 5. A full packet goes out at once, without waiting.
    for (uint32_t i = 0; i < events::kMaxSamplesPerPacket; ++i) events::sample(0x3000u + i);
    status_service();
    assert(g_cap.samples == 3 + events::kMaxSamplesPerPacket);

    std::printf("[PASS] event trace: %u resume spans, idle span, %u drops counted, %u samples\n",
                g_cap.resume_spans, g_cap.last_dropped, g_cap.samples);
    return 0;
}
