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

} // anonymous namespace

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
    g_tasks.back().resume();
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
