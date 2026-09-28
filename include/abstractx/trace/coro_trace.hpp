/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Coroutine Source Instrumentation Macros
 * -------------------------------------------------
 * Zero-cost compile-time tagging for C++20 coroutines.
 * Embeds human-readable coroutine names and co_await suspension tokens
 * directly into the CTF TLP telemetry stream so AbstractX Studio's
 * Coroutine Inspector can display them live.
 *
 * Usage in coroutine bodies:
 *
 *   ABSTRACTX_CORO_SPAWN(task_id, "imu_pipeline");
 *   ...
 *   ABSTRACTX_CORO_SUSPEND(task_id, "imu_pipeline", "spi_ring.pop()");
 *   co_await g_sensor_ring.pop();
 *   ABSTRACTX_CORO_RESUME(task_id, "imu_pipeline", "spi_ring.pop()");
 *   ...
 *   ABSTRACTX_CORO_DONE(task_id, "imu_pipeline");
 *
 * All macros are no-ops unless ABSTRACTX_CORO_TRACE_ENABLE is defined
 * and a g_tracer instance of CtfTraceEngine is accessible in scope.
 *
 * Budget threshold watchdog:
 *   ABSTRACTX_CORO_BUDGET_US(task_id, "imu_pipeline", "spi_ring.pop()", 150, elapsed_us)
 *   Emits a watchdog-exceeded TLP if elapsed_us > budget_us.
 *
 * Design invariants:
 *   - All string literals are compile-time constants (zero heap, zero copy).
 *   - No dynamic memory allocation.
 *   - No blocking I/O.
 *   - Freestanding C++20 compatible.
 */

#ifndef ABSTRACTX_TRACE_CORO_TRACE_HPP
#define ABSTRACTX_TRACE_CORO_TRACE_HPP

#include "tracer.hpp"     // CoroState, CtfTraceEngine, make_coro_tlp
#include "sink.hpp"       // g_telemetry_ring (or provide your own ring reference)
#include <cstdint>

namespace abstractx::trace {

// ============================================================================
// Coroutine Inspector TLP Packet Types
// ============================================================================

// Extended reason codes for the Coroutine Inspector watchdog
enum class CoroReason : uint8_t {
    Normal         = 0,  // Normal lifecycle event
    BudgetExceeded = 1,  // Task exceeded its per-awaiter deadline threshold
    Deadlock       = 2,  // Task has been suspended beyond hard stall limit (>1s)
    PoolExhausted  = 3,  // Static coroutine frame pool approaching capacity
};

// @impl [SPEC-STUDIO-02] tools/visualizer/abstractx_studio.py
// Per-coroutine inspector entry for the Coroutine Inspector tab.
// Written into the TLP telemetry stream via trace_coro_named().
struct CoroInspectorEntry {
    uint8_t  task_id;          // Slot 1..N (0 = invalid)
    char     name[12];         // Null-terminated coroutine name
    char     awaiter_token[16];// Null-terminated co_await expression
    uint8_t  state;            // CoroState enum value
    uint32_t duration_state_us;// Time in current state (microseconds)
    uint16_t source_line;      // __LINE__ at suspension point
    uint16_t budget_us;        // Per-awaiter deadline budget (0 = no watchdog)
};

// Frame pool snapshot emitted periodically for the Studio frame pool gauge
struct CoroFramePoolSnapshot {
    uint32_t pool_used_bytes;     // coro::coro_pool_used()
    uint32_t pool_capacity_bytes; // coro::coro_pool_capacity()
    uint8_t  active_coro_count;   // Number of live (non-done) coroutines
    uint8_t  stalled_count;       // Coroutines exceeding their budget threshold
};

} // namespace abstractx::trace

// ============================================================================
// Instrumentation Macros (zero-cost when disabled)
// ============================================================================

#ifdef ABSTRACTX_CORO_TRACE_ENABLE

// NOTE: g_tracer must be a CtfTraceEngine instance accessible from the
// translation unit. The default is abstractx::trace::g_tracer.
// Override by defining ABSTRACTX_CORO_TRACER before including this header.
#ifndef ABSTRACTX_CORO_TRACER
#  define ABSTRACTX_CORO_TRACER abstractx::trace::g_tracer
#endif

// NOTE: ABSTRACTX_CORO_NOW_US must return the current microsecond timestamp.
// Override by defining ABSTRACTX_CORO_NOW_US to your platform's time source.
#ifndef ABSTRACTX_CORO_NOW_US
#  define ABSTRACTX_CORO_NOW_US (0ULL)
#endif

/// Emit a Spawn event when a coroutine first begins executing.
#define ABSTRACTX_CORO_SPAWN(task_id_, coro_name_) \
    ABSTRACTX_CORO_TRACER.trace_coro_named( \
        static_cast<uint32_t>(task_id_), 0U, \
        abstractx::trace::CoroState::Spawn, \
        static_cast<uint8_t>(abstractx::trace::CoroReason::Normal), \
        (coro_name_), "", ABSTRACTX_CORO_NOW_US)

/// Emit a Suspend event immediately before co_await suspends the coroutine.
/// awaiter_token_: string literal identifying what this co_await is waiting on
///                 e.g. "spi_ring.pop()", "timer.sleep()", "imu_chan.pop()"
#define ABSTRACTX_CORO_SUSPEND(task_id_, coro_name_, awaiter_token_) \
    ABSTRACTX_CORO_TRACER.trace_coro_named( \
        static_cast<uint32_t>(task_id_), 0U, \
        abstractx::trace::CoroState::Suspend, \
        static_cast<uint8_t>(abstractx::trace::CoroReason::Normal), \
        (coro_name_), (awaiter_token_), ABSTRACTX_CORO_NOW_US)

/// Emit a Resume event immediately after the coroutine handle is resumed.
#define ABSTRACTX_CORO_RESUME(task_id_, coro_name_, awaiter_token_) \
    ABSTRACTX_CORO_TRACER.trace_coro_named( \
        static_cast<uint32_t>(task_id_), 0U, \
        abstractx::trace::CoroState::Resume, \
        static_cast<uint8_t>(abstractx::trace::CoroReason::Normal), \
        (coro_name_), (awaiter_token_), ABSTRACTX_CORO_NOW_US)

/// Emit a Done event when the coroutine returns / reaches final_suspend.
#define ABSTRACTX_CORO_DONE(task_id_, coro_name_) \
    ABSTRACTX_CORO_TRACER.trace_coro_named( \
        static_cast<uint32_t>(task_id_), 0U, \
        abstractx::trace::CoroState::Done, \
        static_cast<uint8_t>(abstractx::trace::CoroReason::Normal), \
        (coro_name_), "", ABSTRACTX_CORO_NOW_US)

/// Emit a BudgetExceeded watchdog event when elapsed_us_ > budget_us_.
/// Place this after co_await returns to detect stalls.
#define ABSTRACTX_CORO_BUDGET_US(task_id_, coro_name_, awaiter_token_, budget_us_, elapsed_us_) \
    do { \
        if ((elapsed_us_) > (budget_us_)) { \
            ABSTRACTX_CORO_TRACER.trace_coro_named( \
                static_cast<uint32_t>(task_id_), 0U, \
                abstractx::trace::CoroState::Resume, \
                static_cast<uint8_t>(abstractx::trace::CoroReason::BudgetExceeded), \
                (coro_name_), (awaiter_token_), ABSTRACTX_CORO_NOW_US); \
        } \
    } while (0)

#else // ABSTRACTX_CORO_TRACE_ENABLE not defined → zero-cost no-ops

#define ABSTRACTX_CORO_SPAWN(task_id_, coro_name_)                                    do {} while(0)
#define ABSTRACTX_CORO_SUSPEND(task_id_, coro_name_, awaiter_token_)                  do {} while(0)
#define ABSTRACTX_CORO_RESUME(task_id_, coro_name_, awaiter_token_)                   do {} while(0)
#define ABSTRACTX_CORO_DONE(task_id_, coro_name_)                                     do {} while(0)
#define ABSTRACTX_CORO_BUDGET_US(task_id_, coro_name_, awaiter_token_, budget, elapsed) do {} while(0)

#endif // ABSTRACTX_CORO_TRACE_ENABLE

#endif // ABSTRACTX_TRACE_CORO_TRACE_HPP
