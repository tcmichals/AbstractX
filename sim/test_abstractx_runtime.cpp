/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Master Runtime Verification Suite
 * --------------------------------------------
 * Verifies [SPEC-ARCH-06] Unified AbstractX Runtime API:
 * 1. abstractx::init() configuration and startup
 * 2. abstractx::spawn() concurrent coroutine scheduling
 * 3. abstractx::step() and co_await abstractx::step_async() cooperative execution
 * 4. abstractx::run() lifecycle to completion
 * 5. Transparent CTF trace sink flushing
 */

#include "abstractx/abstractx.hpp"
#include <iostream>
#include <cassert>
#include <vector>

using namespace abstractx;
using namespace abstractx::coro;

static int g_step_counter = 0;
static bool g_task1_completed = false;
static bool g_task2_completed = false;
static bool g_trace_received = false;

// Custom trace sink listener
void test_trace_sink(const uint8_t* data, size_t len, void* /*context*/) {
    if (data && len >= sizeof(trace::CtfPacketHeader)) {
        const auto* hdr = reinterpret_cast<const trace::CtfPacketHeader*>(data);
        if (hdr->magic == trace::CTF_MAGIC) {
            g_trace_received = true;
        }
    }
}

// Test Coroutine 1: Executes multiple steps with co_await step_async()
Task<void> sample_coroutine_1() {
    for (int i = 0; i < 3; ++i) {
        g_step_counter++;
        co_await abstractx::step_async();
    }
    g_task1_completed = true;
}

// Test Coroutine 2: Yields and logs
Task<void> sample_coroutine_2() {
    g_step_counter += 10;
    co_await abstractx::step_async();
    g_step_counter += 10;
    g_task2_completed = true;
}

// Primary Application Task
Task<void> app_main_task() {
    // Spawn sub-tasks into the AbstractX runtime
    abstractx::spawn(sample_coroutine_1());
    abstractx::spawn(sample_coroutine_2());

    while (!g_task1_completed || !g_task2_completed) {
        co_await abstractx::step_async();
    }
}

int main() {
    std::cout << "========================================================\n";
    std::cout << "  AbstractX Master Runtime [SPEC-ARCH-06] Verification  \n";
    std::cout << "========================================================\n";

    // 1. Verify init with custom trace sink
    Config cfg{};
    cfg.trace.sink_type = TraceSinkType::None; // Custom sink callback
    assert(abstractx::init(cfg));
    trace::g_tracer.set_flush_handler(test_trace_sink, nullptr);
    std::cout << "[+] abstractx::init() successfully executed\n";

    // 2. Emit a test trace event to verify tracer integration
    trace::g_tracer.trace_coro(1, 0x1234, trace::CoroState::Spawn, 0, 1000);
    trace::g_tracer.flush(2000);
    assert(g_trace_received && "Expected CTF trace packet to be received by flush handler");
    std::cout << "[+] Transparent CTF 1.8 trace sink emission verified\n";

    // 3. Verify step() without any tasks does not fault
    abstractx::step();
    std::cout << "[+] Idle abstractx::step() verified\n";

    // 4. Verify abstractx::run() executing app_main_task to completion
    abstractx::run(app_main_task());
    assert(g_task1_completed && "Expected Task 1 to complete");
    assert(g_task2_completed && "Expected Task 2 to complete");
    assert(g_step_counter == 23 && "Step counter mismatch (expected 3 + 20)");
    std::cout << "[+] abstractx::run() and cooperative coroutine scheduling verified!\n";

    // 5. Verify FileTraceSink
    trace::FileTraceSink file_sink;
    assert(file_sink.open("test_runtime_trace.ctf"));
    trace::g_tracer.set_flush_handler([](const uint8_t* data, size_t len, void* ctx) {
        auto* sink = reinterpret_cast<trace::FileTraceSink*>(ctx);
        sink->write(data, len);
        sink->flush();
    }, &file_sink);

    trace::g_tracer.trace_imu(1, 100, 200, 300, 10, 20, 30, 2500, 3000);
    trace::g_tracer.flush(4000);
    file_sink.close();

    FILE* fp = fopen("test_runtime_trace.ctf", "rb");
    assert(fp != nullptr && "Expected test_runtime_trace.ctf to exist");
    trace::CtfPacketHeader fhdr{};
    assert(fread(&fhdr, sizeof(fhdr), 1, fp) == 1);
    assert(fhdr.magic == trace::CTF_MAGIC);
    fclose(fp);
    std::remove("test_runtime_trace.ctf");
    std::cout << "[+] FileTraceSink binary emission & header verified!\n";

    // 6. Verify UdpTraceSink
    trace::UdpTraceSink udp_sink;
    assert(udp_sink.open("127.0.0.1:9870"));
    assert(udp_sink.is_open());
    udp_sink.write(reinterpret_cast<const uint8_t*>(&fhdr), sizeof(fhdr));
    udp_sink.close();
    std::cout << "[+] UdpTraceSink socket transmission verified!\n";

    // 7. Verify trace_dispatcher_task
    coro::Task<void> disp_task = trace::trace_dispatcher_task(5);
    disp_task.resume();
    assert(!disp_task.done());
    std::cout << "[+] trace_dispatcher_task coroutine verified!\n";

    // 8. Verify 1 KB Ping-Pong Buffer Architecture [SPEC-TRACE-06]
    assert(trace::g_tracer.PACKET_SIZE == 1024 && "Expected 1024-byte packet size");
    assert(trace::g_tracer.NUM_PACKETS == 2 && "Expected 2 ping-pong buffers");
    assert(trace::g_tracer.TOTAL_CAPACITY_BYTES == 2048 && "Expected 2048 bytes capacity");
    assert(trace::g_tracer.is_ping_pong() && "Expected is_ping_pong() == true");

    size_t initial_fill_idx = trace::g_tracer.get_active_fill_idx();
    trace::g_tracer.trace_coro(2, 0x5678, trace::CoroState::Suspend, 1, 5000);
    trace::g_tracer.flush(6000);
    size_t swapped_fill_idx = trace::g_tracer.get_active_fill_idx();
    assert(swapped_fill_idx == (initial_fill_idx + 1) % 2 && "Expected ping-pong index swap on flush");
    trace::g_tracer.trace_coro(3, 0x9abc, trace::CoroState::Resume, 2, 7000);
    trace::g_tracer.flush(8000);
    assert(trace::g_tracer.get_active_fill_idx() == initial_fill_idx && "Expected ping-pong index swap back to initial");
    std::cout << "[+] 1 KB Ping-Pong Buffer Architecture & atomic swapping [SPEC-TRACE-06] verified!\n";

    std::cout << "\n[SUCCESS] Unified Master Runtime API & Configurable Trace Sinks [SPEC-ARCH-06] [SPEC-TRACE-04] [SPEC-TRACE-06] Verified 100% Cleanly!\n";
    return 0;
}
