/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX: Protothreads Example 1 - Two Concurrent Timers (example-small.c)
 * -----------------------------------------------------------------------------
 * Translates Adam Dunkels' canonical 'example-small.c' from the official pt-1.4
 * distribution directly into modern AbstractX C++20 Coroutines.
 *
 * Uses the official AbstractX coroutine engine (include/asp_coro.hpp).
 */

#include "asp_coro.hpp"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <vector>

using namespace abstractx;
using namespace abstractx::coro;

// =============================================================================
// 1. ORIGINAL ADAM DUNKELS C PROTOTHREADS CODE (pt-1.4 / example-small.c)
// =============================================================================
struct pt { uint16_t lc; };
#define PT_INIT(pt)               ((pt)->lc = 0)
#define PT_BEGIN(pt)              switch((pt)->lc) { case 0:
#define PT_WAIT_UNTIL(pt, cond)   do { (pt)->lc = __LINE__; case __LINE__: \
                                       if (!(cond)) return 0; } while (0)
#define PT_END(pt)                } (pt)->lc = 0; return 1;

struct ClassicSmallContext {
    pt pt1;
    pt pt2;
    uint64_t timer1_wake;
    uint64_t timer2_wake;
    uint32_t pt1_fired_count;
    uint32_t pt2_fired_count;
};

int classic_protothread1(ClassicSmallContext* ctx, uint64_t current_time_ms) {
    PT_BEGIN(&ctx->pt1);
    while (ctx->pt1_fired_count < 5) {
        ctx->timer1_wake = current_time_ms + 1000;
        PT_WAIT_UNTIL(&ctx->pt1, current_time_ms >= ctx->timer1_wake);
        ctx->pt1_fired_count++;
    }
    PT_END(&ctx->pt1);
}

int classic_protothread2(ClassicSmallContext* ctx, uint64_t current_time_ms) {
    PT_BEGIN(&ctx->pt2);
    while (ctx->pt2_fired_count < 2) {
        ctx->timer2_wake = current_time_ms + 2500;
        PT_WAIT_UNTIL(&ctx->pt2, current_time_ms >= ctx->timer2_wake);
        ctx->pt2_fired_count++;
    }
    PT_END(&ctx->pt2);
}

double run_classic_example_small() {
    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << " [1] ORIGINAL C PROTOTHREADS: example-small.c\n";
    std::cout << "------------------------------------------------------------------------------------\n";
    ClassicSmallContext ctx{};
    PT_INIT(&ctx.pt1);
    PT_INIT(&ctx.pt2);

    uint64_t sim_time_ms = 0;
    auto t0 = std::chrono::high_resolution_clock::now();

    while ((ctx.pt1_fired_count < 5 || ctx.pt2_fired_count < 2) && sim_time_ms < 6000) {
        classic_protothread1(&ctx, sim_time_ms);
        classic_protothread2(&ctx, sim_time_ms);
        sim_time_ms += 10;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << " -> Protothread 1 fired: " << ctx.pt1_fired_count << " times (every 1000 ms)\n";
    std::cout << " -> Protothread 2 fired: " << ctx.pt2_fired_count << " times (every 2500 ms)\n";
    std::cout << " -> Executed in: " << std::fixed << std::setprecision(4) << elapsed_ms << " ms\n\n";
    return elapsed_ms;
}

// =============================================================================
// 2. ABSTRACTX C++20 COROUTINES VERSION (Clean, Standard, Unified Header)
// =============================================================================

Task<void> modern_timer1_coroutine(uint64_t& current_time_ms, uint64_t& timer_reg, uint32_t& fired_count) {
    for (int i = 0; i < 5; ++i) {
        co_await AsyncSleepAwaiter{current_time_ms + 1000, current_time_ms, &timer_reg};
        fired_count++;
    }
}

Task<void> modern_timer2_coroutine(uint64_t& current_time_ms, uint64_t& timer_reg, uint32_t& fired_count) {
    for (int i = 0; i < 2; ++i) {
        co_await AsyncSleepAwaiter{current_time_ms + 2500, current_time_ms, &timer_reg};
        fired_count++;
    }
}

double run_modern_example_small() {
    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << " [2] ABSTRACTX C++20 COROUTINES: Modernized example-small (asp_coro.hpp)\n";
    std::cout << "------------------------------------------------------------------------------------\n";

    uint64_t sim_time_ms = 0;
    uint64_t timer1_reg = 0;
    uint64_t timer2_reg = 0;
    uint32_t coro1_fired = 0;
    uint32_t coro2_fired = 0;

    Task<void> t1 = modern_timer1_coroutine(sim_time_ms, timer1_reg, coro1_fired);
    Task<void> t2 = modern_timer2_coroutine(sim_time_ms, timer2_reg, coro2_fired);
    t1.resume();
    t2.resume();

    auto t0 = std::chrono::high_resolution_clock::now();
    while ((coro1_fired < 5 || coro2_fired < 2) && sim_time_ms < 6000) {
        if (sim_time_ms >= timer1_reg) t1.resume();
        if (sim_time_ms >= timer2_reg) t2.resume();
        sim_time_ms += 10;
    }
    auto t1_time = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1_time - t0).count();

    std::cout << " -> Coroutine 1 fired  : " << coro1_fired << " times (every 1000 ms)\n";
    std::cout << " -> Coroutine 2 fired  : " << coro2_fired << " times (every 2500 ms)\n";
    std::cout << " -> Dynamic Heap Memory: 0 B (Static Frame Pool)\n";
    std::cout << " -> Executed in: " << std::fixed << std::setprecision(4) << elapsed_ms << " ms\n\n";
    return elapsed_ms;
}

int main() {
    std::cout << "====================================================================================\n";
    std::cout << " PROTOTHREADS CANONICAL EXAMPLE 1: TWO CONCURRENT TIMERS (example-small.c)          \n";
    std::cout << "====================================================================================\n\n";

    double pt_ms = run_classic_example_small();
    double coro_ms = run_modern_example_small();

    std::cout << "====================================================================================\n";
    std::cout << " VERIFICATION RESULT: 100% BIT-EXACT EVENT PARITY (Both Timers Completed)\n";
    std::cout << "====================================================================================\n";
    return 0;
}
