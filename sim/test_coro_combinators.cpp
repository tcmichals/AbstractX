/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: coroutine combinators when_all (&&) and when_any (||) [SPEC-APP-01]
 * Driven like production (AGENTS.md §10.6): each task is started once and the Dispatcher is
 * pumped; children wait on gates the test opens in a chosen order. Checks results, that the
 * caller resumes exactly once, and 1000 rounds with a flat coroutine pool.
 * (Replaces a 2026-08 file that never called when_all/when_any and was never built.)
 */
// @verifies [SPEC-APP-01] apps/gps_imu_app/SPECIFICATION.md
// @verifies [SPEC-ARCH-11] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "asp_coro.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <tuple>
#include <variant>

using abstractx::Dispatcher;
using abstractx::coro::Task;
using abstractx::coro::coro_pool_high_water;
using abstractx::coro::when_all;
using abstractx::coro::when_any;

namespace {

// A completion the test delivers by hand, through the Dispatcher (like a driver interrupt).
struct Gate {
    std::coroutine_handle<> h{};
    bool opened{false};
    bool await_ready() const noexcept { return opened; }
    void await_suspend(std::coroutine_handle<> hh) noexcept { h = hh; }
    void await_resume() const noexcept {}
    void open() {
        opened = true;
        if (h) (void)Dispatcher::push(std::exchange(h, {}));
    }
};

void pump() {
    while (Dispatcher::has_pending_resumes()) Dispatcher::process();
}

Task<uint32_t> after(Gate& g, uint32_t v) {
    co_await g;
    co_return v;
}

Task<uint32_t> now(uint32_t v) { co_return v; }

Task<void> all3(std::array<Gate, 3>& g, uint32_t& sum, int& resumes) {
    auto [a, b, c] = co_await when_all(after(g[0], 1), after(g[1], 20), after(g[2], 300));
    ++resumes;
    sum = a + b + c;
}

Task<void> all_ready(uint32_t& sum) {
    auto [a, b] = co_await when_all(now(4), now(5));
    sum = a + b;
}

Task<void> any2(Gate& g1, Gate& g2, int& winner, uint32_t& value, int& resumes) {
    auto r = co_await when_any(after(g1, 7), after(g2, 9));
    ++resumes;
    winner = static_cast<int>(r.index());
    value = r.index() == 0 ? std::get<0>(r) : std::get<1>(r);
}

Task<void> any_now(Gate& g, int& winner, uint32_t& value, int& resumes) {
    auto r = co_await when_any(now(11), after(g, 13));
    ++resumes;
    winner = static_cast<int>(r.index());
    value = r.index() == 0 ? std::get<0>(r) : std::get<1>(r);
}

} // namespace

int main() {
    // 1. when_all: children finish out of order; the caller resumes once, after the last.
    {
        std::array<Gate, 3> g{};
        uint32_t sum = 0;
        int resumes = 0;
        auto t = all3(g, sum, resumes);
        t.resume();                       // start once
        assert(!t.done() && resumes == 0);
        g[2].open();
        pump();
        g[0].open();
        pump();
        assert(!t.done() && resumes == 0);
        g[1].open();
        pump();
        assert(t.done() && resumes == 1 && sum == 321);
    }

    // 2. when_all where every child is already done: no suspension.
    {
        uint32_t sum = 0;
        auto t = all_ready(sum);
        t.resume();
        assert(t.done() && sum == 9);
    }

    // 3. when_any: the first to finish wins, the caller resumes once.
    {
        Gate g1, g2;
        int winner = -1, resumes = 0;
        uint32_t value = 0;
        auto t = any2(g1, g2, winner, value, resumes);
        t.resume();
        assert(!t.done());
        g2.open();
        pump();
        assert(t.done() && resumes == 1 && winner == 1 && value == 9);
        // The loser (still waiting on g1) must not resume the caller or touch freed memory
        // when its completion arrives later (ASan checks the memory side).
        g1.open();
        pump();
        assert(resumes == 1);
    }

    // 4. Steady state: 1000 rounds of when_all with out-of-order completion.
    {
        std::array<Gate, 3> warm{};
        uint32_t sum = 0;
        int resumes = 0;
        {
            auto w = all3(warm, sum, resumes);
            w.resume();
            for (auto& x : warm) x.open();
            pump();
        }
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dq0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            std::array<Gate, 3> g{};
            resumes = 0;
            auto t = all3(g, sum, resumes);
            t.resume();
            g[static_cast<size_t>(i % 3)].open();
            g[static_cast<size_t>((i + 1) % 3)].open();
            pump();
            g[static_cast<size_t>((i + 2) % 3)].open();
            pump();
            assert(t.done() && resumes == 1 && sum == 321);
        }
        assert(coro_pool_high_water() == hw0 && Dispatcher::dropped_resumes() == dq0);
        assert(Dispatcher::queue_depth() == 0);
    }

    // 5. Steady state for when_any: each loser is detached and frees its own frame when its
    //    completion arrives, so 1000 races leave the pool flat (no leak, no use after free).
    {
        {
            Gate a, b;
            int winner = -1, resumes = 0;
            uint32_t value = 0;
            auto w = any2(a, b, winner, value, resumes);
            w.resume();
            a.open();
            pump();
            b.open();
            pump();
        }
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dq0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            Gate a, b;
            int winner = -1, resumes = 0;
            uint32_t value = 0;
            auto t = any2(a, b, winner, value, resumes);
            t.resume();
            Gate& first = (i & 1) ? a : b;
            Gate& second = (i & 1) ? b : a;
            first.open();
            pump();
            assert(t.done() && resumes == 1 && winner == ((i & 1) ? 0 : 1));
            second.open();                 // the detached loser finishes and frees itself
            pump();
            assert(resumes == 1);
        }
        assert(coro_pool_high_water() == hw0 && Dispatcher::dropped_resumes() == dq0);
    }

    // 6. when_any where the first task finishes inside await_suspend: the second was never
    //    started, so nothing will ever resume it. It must be freed, not detached (2026-10-28:
    //    each round leaked its frame and the 60 KB pool ran out after ~470 rounds).
    {
        Gate never;
        int winner = -1, resumes = 0;
        uint32_t value = 0;
        {
            auto w = any_now(never, winner, value, resumes);
            w.resume();
        }
        const size_t used0 = abstractx::coro::coro_pool_used();
        const size_t hw0 = coro_pool_high_water();
        for (int i = 0; i < 1000; ++i) {
            resumes = 0;
            auto t = any_now(never, winner, value, resumes);
            t.resume();
            assert(t.done() && resumes == 1 && winner == 0 && value == 11);
        }
        assert(abstractx::coro::coro_pool_used() == used0 && coro_pool_high_water() == hw0);
        assert(never.h == nullptr);   // the unstarted loser never reached the gate
    }

    std::printf("[PASS] when_all / when_any: out-of-order completion, single caller resume, loser detached and freed, unstarted loser freed, 1000 rounds each flat\n");
    return 0;
}
