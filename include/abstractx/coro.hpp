/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Generic C++20 Coroutine Library (Modern Protothreads Replacement)
 * ----------------------------------------------------------------------------
 * A zero-heap, freestanding, type-safe cooperative multitasking library for
 * bare-metal microcontrollers, robotics, and real-time systems.
 *
 * Core Primitives:
 * - Task<T> / Task<void>    : Stackless coroutines with static atomic frame allocation (0 B heap)
 * - yield()                 : Cooperative yield (replaces PT_YIELD)
 * - wait_until(predicate)   : Type-safe condition wait (replaces PT_WAIT_UNTIL)
 * - sleep_for() / sleep_until(): Asynchronous hardware timer delay (replaces timer loops)
 * - Event                   : Cooperative one-shot and multi-shot event signaling
 * - Semaphore               : Cooperative counting semaphore (replaces pt-sem.h)
 * - AsyncQueue<T, N>        : Cooperative lock-free SPSC queue
 * - when_all() / when_any() : Structured concurrency combinators
 */

#ifndef ABSTRACTX_CORO_HPP
#define ABSTRACTX_CORO_HPP

#include "../asp_coro.hpp"

namespace abstractx {
    // Export standard coroutine primitives to top-level abstractx namespace for clean client usage
    using coro::Task;
    using coro::YieldAwaiter;
    using coro::AsyncDelayAwaiter;
    using coro::AsyncSleepAwaiter;
    using coro::yield;
    using coro::delay_us;
    using coro::delay_ms;
    using coro::delay_until;
    using coro::sleep_for;
    using coro::sleep_until;
    using coro::wait_until;
    using coro::Event;
    using coro::Semaphore;
    using coro::AsyncQueue;
    using coro::when_all;
    using coro::when_any;
}

#endif // ABSTRACTX_CORO_HPP
