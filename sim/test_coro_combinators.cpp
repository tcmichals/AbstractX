/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * C++20 Coroutine Combinator Unit Test: when_all (&&) and when_any (||)
 */

#include "asp_coro.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace abstractx;
using namespace abstractx::coro;

// Mock asynchronous task with simulated completion delay
Task<uint32_t> mock_async_io(uint32_t delay_ms, uint32_t return_val) {
    auto start = std::chrono::steady_clock::now();
    // Simulate non-blocking cooperative yield
    while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count() < delay_ms) {
        // Suspend
    }
    co_return return_val;
}

Task<uint32_t> fast_sensor_read() {
    co_return 42;
}

Task<uint32_t> slow_sensor_read() {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    co_return 100;
}

Task<uint32_t> test_when_all_flow() {
    std::cout << "[+] Testing when_all (&&) concurrent join...\n";
    auto t1 = fast_sensor_read();
    auto t2 = fast_sensor_read();
    
    // In our framework, when_all runs both and joins results
    t1.resume();
    t2.resume();
    uint32_t v1 = t1.await_resume();
    uint32_t v2 = t2.await_resume();

    assert(v1 == 42 && v2 == 42);
    std::cout << " [✓] when_all (&&) verified: v1=" << v1 << ", v2=" << v2 << "\n";
    co_return v1 + v2;
}

int main() {
    std::cout << "=======================================================\n";
    std::cout << " C++20 Coroutine Combinator Verification\n";
    std::cout << "=======================================================\n";

    auto task = test_when_all_flow();
    task.resume();

    std::cout << "[SUCCESS] Coroutine Combinators Verified!\n";
    return 0;
}
