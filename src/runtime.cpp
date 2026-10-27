/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Master Runtime Implementation
 * ---------------------------------------
 * Authoritative single runtime orchestrator for AbstractX applications.
 */

#include "abstractx/abstractx.hpp"
#include "abstractx/trace/sink.hpp"
#include "abstractx/trace/trace_dispatcher.hpp"
#include "abstractx/trace/runtime_stats.hpp"
#include "abstractx/trace/event_trace.hpp"
#include <etl/vector.h>
#include <cstdio>

namespace abstractx {

namespace {

Config g_config{};
bool g_initialized{false};

// Fixed pool of active spawned application coroutines (0 B dynamic heap)
constexpr size_t MAX_ACTIVE_TASKS = 16;
etl::vector<coro::Task<void>, MAX_ACTIVE_TASKS> g_tasks;

// Active trace sinks (0 B dynamic heap)
trace::FileTraceSink g_file_sink;
trace::UdpTraceSink  g_udp_sink;
trace::NullTraceSink g_null_sink;
trace::ITraceSink*   g_active_sink{&g_null_sink};

void trace_flush_callback(const uint8_t* data, size_t len, void* /*context*/) {
    if (g_active_sink && data && len > 0) {
        g_active_sink->write(data, len);
        g_active_sink->flush();
    }
    if (data && len > 0) {
        hal::platform_send_telemetry(data, len);
    }
}

uint64_t g_status_due_us{0};

// @impl [SPEC-TRACE-08] docs/DESIGN_SPECIFICATION.md
void copy_name(char* dst, size_t cap, const char* src) noexcept {
    for (size_t i = 0; src && i + 1 < cap && src[i]; ++i) dst[i] = src[i];
}

// @impl [SPEC-TRACE-08] docs/DESIGN_SPECIFICATION.md
void emit_status(uint64_t now_us) noexcept {
    stats::account();
    stats::PlatformStats plat{};
    hal::platform_runtime_stats(plat);
    const auto& st = stats::g_state;

    stats::RuntimeStatsHeader hdr{};
    for (const auto& t : st.tasks) {
        if (t.frame != 0 || t.resumes != 0) ++hdr.n_tasks;
    }
    hdr.n_drivers = static_cast<uint8_t>(plat.n_drivers < stats::kMaxDrivers ? plat.n_drivers : stats::kMaxDrivers);
    hdr.timestamp_us = now_us;
    hdr.cycle_hz = st.hz;
    hdr.cycles = static_cast<uint32_t>(st.mark);
    hdr.idle_cycles = static_cast<uint32_t>(st.idle_cycles);
    hdr.steps = st.steps;
    hdr.pool_used = static_cast<uint32_t>(coro::coro_pool_used());
    hdr.pool_high_water = static_cast<uint32_t>(coro::coro_pool_high_water());
    hdr.pool_capacity = static_cast<uint32_t>(coro::coro_pool_capacity());
    hdr.dropped_resumes = Dispatcher::dropped_resumes();
    hdr.queue_depth = static_cast<uint16_t>(Dispatcher::queue_depth());
    hdr.queue_capacity = static_cast<uint16_t>(Dispatcher::QUEUE_CAPACITY);
    hdr.trace_discarded = static_cast<uint32_t>(trace::g_tracer.get_discarded_count());

    const size_t bytes = sizeof(hdr) + hdr.n_tasks * sizeof(stats::TaskStatsRecord) +
                         hdr.n_drivers * sizeof(stats::DriverStatsRecord);
    (void)trace::g_tracer.emit_stream_packet(stats::kStreamId, bytes, [&](uint8_t* dst) {
        __builtin_memcpy(dst, &hdr, sizeof(hdr));
        dst += sizeof(hdr);
        for (const auto& t : st.tasks) {
            if (t.frame == 0 && t.resumes == 0) continue;
            stats::TaskStatsRecord r{};
            copy_name(r.name, sizeof(r.name), t.name);
            r.frame_addr = static_cast<uint32_t>(t.frame);
            r.resumes = t.resumes;
            r.run_cycles = static_cast<uint32_t>(t.run_cycles);
            r.max_cycles = t.max_cycles > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(t.max_cycles);
            __builtin_memcpy(dst, &r, sizeof(r));
            dst += sizeof(r);
        }
        for (size_t i = 0; i < hdr.n_drivers; ++i) {
            const auto& d = plat.drivers[i];
            stats::DriverStatsRecord r{};
            copy_name(r.name, sizeof(r.name), d.name);
            r.id = d.id;
            r.events = d.events;
            r.cycles = d.cycles;
            r.errors = d.errors;
            __builtin_memcpy(dst, &r, sizeof(r));
            dst += sizeof(r);
        }
    }, now_us);
}

// Profiler samples ([SPEC-TRACE-09]) and timeline spans ([SPEC-TRACE-10]) into CTF packets:
// when a packet is full, or `wait_us` after the first waiting item. A packet the transport
// cannot take stays in the ring for the next call.
// @impl [SPEC-TRACE-09] [SPEC-TRACE-10] docs/DESIGN_SPECIFICATION.md
template <typename T, typename Header>
void drain_once(events::Ring<T>& ring, uint8_t stream, size_t max_n, uint64_t wait_us,
                Header hdr, uint64_t now_us) noexcept {
    uint32_t n = ring.size();
    if (n == 0) {
        ring.first_wait_us = 0;
        return;
    }
    if (!hal::platform_trace_listening()) {  // nobody reads: empty the ring, no drops counted
        ring.tail.store(ring.tail.load(std::memory_order_relaxed) + n, std::memory_order_release);
        ring.first_wait_us = 0;
        return;
    }
    if (ring.first_wait_us == 0) ring.first_wait_us = now_us;
    if (n < max_n && now_us - ring.first_wait_us < wait_us) return;
    if (n > max_n) n = static_cast<uint32_t>(max_n);
    hdr.n = static_cast<uint16_t>(n);
    hdr.dropped = ring.dropped.load(std::memory_order_relaxed);
    const uint32_t t = ring.tail.load(std::memory_order_relaxed);
    const bool sent = trace::g_tracer.emit_stream_packet(stream, sizeof(hdr) + n * sizeof(T), [&](uint8_t* dst) {
        __builtin_memcpy(dst, &hdr, sizeof(hdr));
        dst += sizeof(hdr);
        for (uint32_t i = 0; i < n; ++i) {
            __builtin_memcpy(dst + i * sizeof(T), &ring.items[(t + i) & ring.mask], sizeof(T));
        }
    }, now_us);
    if (sent) {
        ring.tail.store(t + n, std::memory_order_release);
        ring.first_wait_us = ring.size() ? now_us : 0;
    }
}

// Up to 4 packets per call so a busy target keeps up ([SPEC-TRACE-10]).
template <typename T, typename Header>
void drain_ring(events::Ring<T>& ring, uint8_t stream, size_t max_n, uint64_t wait_us,
                Header hdr, uint64_t now_us) noexcept {
    for (int i = 0; i < 4; ++i) {
        const uint32_t before = ring.tail.load(std::memory_order_relaxed);
        drain_once(ring, stream, max_n, wait_us, hdr, now_us);
        if (ring.tail.load(std::memory_order_relaxed) == before || ring.size() < max_n) break;
    }
}

// @impl [SPEC-TRACE-09] [SPEC-TRACE-10] docs/DESIGN_SPECIFICATION.md
void events_service(uint64_t now_us) noexcept {
    auto& ev = events::g_events;
    if (ev.samples.items) {
        events::ProfileHeader h{};
        h.sample_hz = ev.sample_hz;
        drain_ring(ev.samples, events::kProfileStream, events::kMaxSamplesPerPacket, events::kProfileFlushUs, h, now_us);
    }
    if (ev.timeline_on) {
        events::TimelineHeader h{};
        h.cycle_hz = stats::g_state.hz;
        h.cycles_now = stats::now_cycles();
        drain_ring(ev.spans, events::kTimelineStream, events::kMaxEventsPerPacket, events::kTimelineFlushUs, h, now_us);
    }
}

} // anonymous namespace

// Weak defaults ([SPEC-TRACE-10]): a small timeline ring, no profiler sampler.
// @impl [SPEC-TRACE-09] [SPEC-TRACE-10] docs/DESIGN_SPECIFICATION.md
__attribute__((weak)) void hal::platform_event_buffers(hal::EventBuffers& out) noexcept {
    static events::TimelineEvent spans[128];
    out = hal::EventBuffers{nullptr, 0, spans, 128};
}
__attribute__((weak)) void hal::platform_profile_start(uint32_t /*hz*/) noexcept {}
// @impl [SPEC-E906-IPC-05] targets/allwinner_e906/SPECIFICATION.md
__attribute__((weak)) bool hal::platform_trace_listening() noexcept { return true; }
// @impl [SPEC-HAL-10] docs/DESIGN_SPECIFICATION.md
__attribute__((weak)) hal::IUart* hal::get_uart_port(uint8_t index) noexcept {
    return index == 0 ? &hal::get_uart_driver() : nullptr;
}
__attribute__((weak)) hal::II2c* hal::get_i2c_port(uint8_t index) noexcept {
    return index == 0 ? &hal::get_i2c_driver() : nullptr;
}

// Weak default: a platform without interrupt counters reports no drivers.
__attribute__((weak)) void hal::platform_runtime_stats(stats::PlatformStats& out) noexcept {
    out.n_drivers = 0;
}

// @impl [SPEC-TRACE-08] docs/DESIGN_SPECIFICATION.md
void status_service() noexcept {
    if (!g_initialized) {
        return;
    }
    const uint64_t now_us = hal::get_timer_driver().get_time_us();
    events_service(now_us);  // [SPEC-TRACE-09] [SPEC-TRACE-10]
    const uint32_t period_ms = g_config.trace.status_period_ms;
    if (period_ms == 0 || now_us < g_status_due_us) {
        return;
    }
    const uint64_t period_us = static_cast<uint64_t>(period_ms) * 1000u;
    // Next deadline on the period grid; after a long stall, skip the missed ones.
    g_status_due_us = (g_status_due_us != 0 && now_us - g_status_due_us < period_us)
                          ? g_status_due_us + period_us : now_us + period_us;
    if (hal::platform_trace_listening()) {  // nobody reads: skip, do not count a discard
        emit_status(now_us);
    }
}

uint64_t status_deadline_us() noexcept {
    return g_config.trace.status_period_ms ? g_status_due_us : UINT64_MAX;
}

// @impl [SPEC-ARCH-06] [SPEC-TRACE-04] docs/DESIGN_SPECIFICATION.md#spec-arch-06
// @status Complete
bool init(const Config& config) noexcept {
    g_config = config;

    // 1. Initialize underlying platform clocks, stdio, and hardware peripherals
    hal::platform_init();

    // 2. Configure transparent CTF 1.8 trace sink
    if (g_config.trace.sink_type == TraceSinkType::File) {
        const char* path = g_config.trace.sink_target ? g_config.trace.sink_target : "trace.ctf";
        g_file_sink.open(path);
        g_active_sink = &g_file_sink;
    } else if (g_config.trace.sink_type == TraceSinkType::Udp) {
        const char* target = g_config.trace.sink_target ? g_config.trace.sink_target : "127.0.0.1:9870";
        g_udp_sink.open(target);
        g_active_sink = &g_udp_sink;
    } else {
        g_active_sink = &g_null_sink;
    }
    // Zero-copy transport if the platform has one ([SPEC-TRACE-07]), otherwise
    // the static pool feeding the configured host sink.
    if (trace::ITraceTransport* transport = hal::platform_trace_transport()) {
        trace::g_tracer.set_transport(transport);
    } else {
        trace::g_tracer.set_flush_handler(trace_flush_callback, nullptr);
    }

    // 2b. Profiler and timeline rings from the platform ([SPEC-TRACE-09], [SPEC-TRACE-10])
    {
        hal::EventBuffers buf{};
        hal::platform_event_buffers(buf);
        events::attach_timeline(buf.spans, buf.n_spans, g_config.trace.timeline);
        events::attach_profile(buf.samples, buf.n_samples, g_config.trace.profile_hz);
        hal::platform_profile_start(events::g_events.sample_hz);
    }

    // 3. Initialize and arm target ioProcessor
    auto& io_proc = hal::get_target_io_processor();
    if (g_config.io_setup.channels.data() != nullptr) {
        io_proc.init(g_config.io_setup);
    } else {
        io_proc.start();
    }

    g_initialized = true;
    return true;
}

// @impl [SPEC-TRACE-08] docs/DESIGN_SPECIFICATION.md
bool spawn(coro::Task<void> task, const char* name) noexcept {
    if (!task.done() && name) {
        stats::name_task(task.handle().address(), name);
    }
    return spawn(std::move(task));
}

// @impl [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-06
// @status Complete
bool spawn(coro::Task<void> task) noexcept {
    if (task.done()) {
        return false;
    }

    if (g_tasks.full()) {
        // Drain any completed tasks to make room
        for (auto it = g_tasks.begin(); it != g_tasks.end();) {
            if (it->done()) {
                it = g_tasks.erase(it);
            } else {
                ++it;
            }
        }
    }

    if (g_tasks.full()) {
        return false;
    }
    g_tasks.push_back(std::move(task));
    // Kick off the coroutine into the cooperative scheduler
    {
        stats::ResumeScope charge(g_tasks.back().handle().address());  // [SPEC-TRACE-08]
        g_tasks.back().resume();
    }
    return true;
}

// @impl [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-06
// @status Complete
void step() noexcept {
    // 1. Drain and resume all ready coroutines in the Dispatcher
    Dispatcher::process();

    // 2. Step the target I/O processor reactor
    hal::get_target_io_processor().step(0);

    // 3. Service background network stack
    hal::platform_poll_network();

    // 4. Flush trace buffer if required
    trace::g_tracer.flush(hal::get_timer_driver().get_time_us());

    // 4b. Runtime status report when due ([SPEC-TRACE-08])
    stats::count_step();
    status_service();

    // 5. Clean up finished tasks from the active pool
    for (auto it = g_tasks.begin(); it != g_tasks.end();) {
        if (it->done()) {
            it = g_tasks.erase(it);
        } else {
            ++it;
        }
    }
}

void StepAwaiter::await_suspend(std::coroutine_handle<> h) noexcept {
    Dispatcher::push(h);
}

// @impl [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-06
// @status Complete
StepAwaiter step_async() noexcept {
    return {};
}

// @impl [SPEC-ARCH-06] [SPEC-TRACE-04] docs/DESIGN_SPECIFICATION.md#spec-arch-06
// @status Complete
coro::Task<void> service_task() noexcept {
    auto& io_proc = hal::get_target_io_processor();
    auto& timer   = hal::get_timer_driver();

    while (true) {
        io_proc.step(0);
        trace::g_tracer.flush(timer.get_time_us());
        co_await step_async();
    }
}


// @impl [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-06
// @status Complete
void run(coro::Task<void> main_app_task) {
    if (!g_initialized) {
        init();
    }

    main_app_task.resume();

    while (!main_app_task.done()) {
        step();
        // Sleep only when nothing is runnable; otherwise ready coroutines would
        // wait for an unrelated interrupt. platform_idle_wait() must re-check
        // with interrupts masked to close the check-then-sleep race.
        if (!Dispatcher::has_pending_resumes()) {
            hal::platform_idle_wait();
        }
    }

    // Final cleanup and flush
    step();
    if (g_active_sink) {
        g_active_sink->close();
        g_active_sink = &g_null_sink;
    }
}

} // namespace abstractx
