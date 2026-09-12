/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Non-Blocking Trace Dispatcher Coroutine
 * -------------------------------------------------
 * Non-blocking coroutine that monitors CTF trace buffer watermarks and periodic flush timers.
 * Flushes binary CTF 1.8 packets cooperatively to the configured trace sink.
 */

#ifndef ABSTRACTX_TRACE_TRACE_DISPATCHER_HPP
#define ABSTRACTX_TRACE_TRACE_DISPATCHER_HPP

#include "abstractx/coro.hpp"
#include "abstractx/trace/tracer.hpp"
#include "abstractx/trace/sink.hpp"
#include "abstractx/hal/timer.hpp"
#include "spsc_tlp_ring.hpp"

namespace abstractx::trace {

/*
 * Trace Dispatcher Task:
 * Non-blocking coroutine that monitors CTF trace buffer watermarks and periodic flush timers (e.g. 10 ms).
 * Yields cooperatively to the coroutine dispatcher so real-time sensor loops are never blocked.
 */
inline coro::Task<void> trace_dispatcher_task(uint16_t flush_period_ms = 10) {
    auto& timer = hal::get_timer_driver();
    uint64_t flush_period_us = static_cast<uint64_t>(flush_period_ms) * 1000ULL;
    uint64_t next_flush_us = timer.get_time_us() + flush_period_us;

    while (true) {
        uint64_t now_us = timer.get_time_us();
        if (now_us >= next_flush_us) {
            g_tracer.flush(now_us);
            next_flush_us = now_us + flush_period_us;
        }
        co_await yield_to_dispatcher();
    }
}

/*
 * Linux Ingestion Coroutine:
 * Drains 64-byte CTF TLPs from the shared coprocessor SRAM ring (e.g. 0x40000000)
 * and streams them directly into the configured host sink (UDP or file).
 */
inline coro::Task<void> linux_trace_receiver_task(SpscTlpRing<64>& sram_ring, ITraceSink& sink) {
    Tlp64 tlp{};
    while (true) {
        bool drained = false;
        while (sram_ring.pop(tlp)) {
            drained = true;
            sink.write(reinterpret_cast<const uint8_t*>(&tlp.wire), sizeof(tlp.wire));
        }
        if (drained) {
            sink.flush();
        }
        co_await yield_to_dispatcher();
    }
}

} // namespace abstractx::trace

#endif // ABSTRACTX_TRACE_TRACE_DISPATCHER_HPP
