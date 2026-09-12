/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Master Runtime Interface & Autonomous Domain Orchestrator
 * --------------------------------------------------------------------
 * The Single Authoritative Public API for all AbstractX applications.
 *
 * Encapsulates:
 * 1. Unified Configuration & Startup (init)
 * 2. Autonomous Component Placement (Core 0 vs Core 1, E907 vs Linux, SITL)
 * 3. Transparent CTF 1.8 Tracing & Stream Sinks (UDP :9870, File, Shared SRAM)
 * 4. Cooperative Coroutine Scheduling (spawn, step, step_async, run)
 *
 * Guarantee: Zero application #ifdef directives. 100% portable C++20.
 */

#ifndef ABSTRACTX_ABSTRACTX_HPP
#define ABSTRACTX_ABSTRACTX_HPP

#include "abstractx/coro.hpp"
#include "abstractx/hal/platform.hpp"
#include "abstractx/trace/tracer.hpp"
#include "abstractx/trace/sink.hpp"
#include "abstractx/trace/trace_dispatcher.hpp"
#include "abstractx/hal/io_processor.hpp"
#include "abstractx/platform_topology.hpp"

namespace abstractx {

// Trace Sink Topology
enum class TraceSinkType : uint8_t {
    None           = 0,
    Udp            = 1, // Live UDP streaming (e.g. :9870 to Visualizer Studio)
    File           = 2, // Binary CTF file logging (e.g. "trace.ctf" for Babeltrace)
    SharedSramRing = 3  // E907/RP2350 shared memory to Linux remoteproc/msgbox
};

// Export BufferProfile from trace namespace
using trace::BufferProfile;

// Startup Tracing Configuration
struct TraceConfig {
    TraceSinkType sink_type{TraceSinkType::None};
    const char*   sink_target{nullptr}; // Destination IP ("127.0.0.1") or filepath ("trace.ctf")
    BufferProfile buffer_profile{BufferProfile::PingPong_1K_x2};
    uint16_t      port{9870};
    uint32_t      flush_watermark_bytes{1024};
    uint32_t      flush_interval_ms{10};
};

// Complete Master AbstractX Configuration
struct Config {
    TraceConfig                   trace{};
    hal::IoProcessorSetup         io_setup{};
    topology::PlatformTopologyTable topology{};
    bool                          enable_auto_placement{true};
};

/*
 * 1. Initialize AbstractX Master Runtime:
 *    Detects target silicon topology, configures HAL drivers, establishes SPSC rings,
 *    and launches hardware coprocessors / background workers automatically.
 */
bool init(const Config& config = {}) noexcept;

/*
 * 2. Spawn Application Coroutine:
 *    Registers a user coroutine into the cooperative runtime dispatcher.
 */
void spawn(coro::Task<void> task) noexcept;

// Cooperative Step Awaiter that re-enqueues the coroutine handle into the Dispatcher
struct StepAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept;
    void await_resume() noexcept {}
};

/*
 * 3. Cooperative Step in Synchronous Loops:
 *    Pumps the Dispatcher, drains ready coroutines, and services AbstractX I/O.
 */
void step() noexcept;

/*
 * 4. Cooperative Step in Coroutine Loops:
 *    Usage: co_await abstractx::step_async();
 *    Cooperatively yields execution to let other coroutines and I/O run,
 *    automatically re-scheduling itself on the next Dispatcher cycle.
 */
StepAwaiter step_async() noexcept;


/*
 * 5. Master Application Runner:
 *    Takes the user's primary coroutine, attaches internal AbstractX service tasks,
 *    and drives the system until completion.
 */
void run(coro::Task<void> main_app_task);

/*
 * 6. Internal AbstractX Service Coroutine:
 *    Automatically spawned by init()/run(). Pumps io_processor, flushes CTF traces,
 *    and routes inter-domain TLP rings cooperatively.
 */
coro::Task<void> service_task() noexcept;

} // namespace abstractx

#endif // ABSTRACTX_ABSTRACTX_HPP
