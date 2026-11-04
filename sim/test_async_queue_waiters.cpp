// @verifies [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * [SPEC-ARCH-10] AsyncQueue (review 2026-11-04 Q-4): any number of waiters, hand-off, counted rejects.
 * 1. two consumers wait on an empty queue; two pushes wake both, each with its own item (before: the
 *    second consumer overwrote the first's slot and the first never woke)
 * 2. three producers wait on a full queue; pops let each in, in order, nothing lost
 * 3. a destroyed waiting consumer leaves the list; the next push goes to the live one
 * 4. try_push on a full queue is counted
 * 5. steady state: 1000 rounds, no pool growth, no dropped resumes
 */
#undef NDEBUG
#include "abstractx/coro.hpp"

#include <cassert>
#include <cstdio>

using namespace abstractx;
using namespace abstractx::coro;

namespace {
void drain() { for (int i = 0; i < 100 && Dispatcher::has_pending_resumes(); ++i) Dispatcher::process(); }

AsyncQueue<int, 4> q;
int got[2] = {-1, -1};
Task<void> consumer(int i) { got[i] = co_await q.pop(); }

AsyncQueue<int, 2> fq;
int pushed_done = 0;
Task<void> producer(int v) { co_await fq.push(v); ++pushed_done; }

AsyncQueue<int, 4> sq;
uint64_t sum = 0;
Task<void> sink() { for (int i = 0; i < 1000; ++i) sum += static_cast<uint64_t>(co_await sq.pop()); }
} // namespace

int main() {
    {   // 1
        auto a = consumer(0); a.resume();  // resume: starts the never-run task; it waits
        auto b = consumer(1); b.resume();  // resume: starts the never-run task; it waits
        assert(q.try_push(10) && q.try_push(20));
        drain();
        std::printf("[+] two consumers got %d and %d\n", got[0], got[1]);
        assert(a.done() && b.done() && got[0] == 10 && got[1] == 20 && q.empty());
    }
    {   // 2
        assert(fq.try_push(1) && fq.try_push(2));
        auto p3 = producer(3); p3.resume();  // resume: starts the never-run task; full queue: it waits
        auto p4 = producer(4); p4.resume();  // resume: starts the never-run task
        auto p5 = producer(5); p5.resume();  // resume: starts the never-run task
        assert(pushed_done == 0);
        int out = 0, seen = 0;
        for (int expect = 1; expect <= 5; ++expect) {
            assert(fq.try_pop(out) && out == expect);
            ++seen;
            drain();
        }
        std::printf("[+] three waiting producers: %d items in order, %d producers resumed\n", seen, pushed_done);
        assert(pushed_done == 3 && fq.empty());
    }
    {   // 3
        got[0] = got[1] = -1;
        {
            auto dead = consumer(0); dead.resume();  // resume: starts the never-run task; it waits
        }  // destroyed while waiting
        auto live = consumer(1); live.resume();  // resume: starts the never-run task
        assert(q.try_push(7));
        drain();
        assert(live.done() && got[1] == 7 && got[0] == -1);
        std::puts("[+] a destroyed waiting consumer left the list");
    }
    {   // 4
        AsyncQueue<int, 2> small;
        assert(small.try_push(1) && small.try_push(2) && !small.try_push(3) && !small.try_push(4));
        assert(small.rejected() == 2);
        std::puts("[+] two rejected try_push calls counted");
    }
    {   // 5
        const uint32_t dropped0 = Dispatcher::dropped_resumes();
        auto s = sink(); s.resume();  // resume: starts the never-run task
        const size_t pool0 = coro_pool_high_water();
        for (int i = 1; i <= 1000; ++i) {
            while (!sq.try_push(i)) drain();
            drain();
        }
        drain();
        assert(s.done() && sum == 1000u * 1001u / 2u);
        assert(coro_pool_high_water() == pool0 && Dispatcher::dropped_resumes() == dropped0);
        std::puts("[+] 1000 rounds: nothing lost, no pool growth, no dropped resumes");
    }
    std::puts("[SUCCESS] AsyncQueue waiters [SPEC-ARCH-10]");
    return 0;
}
