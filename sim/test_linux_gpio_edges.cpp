// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @verifies [SPEC-LINUX-GPIO-02] targets/linux/SPECIFICATION.md (LinuxGpioDriver configure_interrupt edge_loop)
// A missing chip gives the simulated edge eventfd ([SPEC-LINUX-GPIO-01]); each edge must reach the
// handler from the edge thread. Before 2026-11-06 nothing read the events, so the handler never ran.
#include "linux_gpio_driver.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace abstractx::hal;

namespace {
int g_fail = 0;
#define CHECK(c, ...)                                                                \
    do {                                                                             \
        if (!(c)) {                                                                  \
            std::printf("FAIL %s:%d: %s -- ", __FILE__, __LINE__, #c);              \
            std::printf(__VA_ARGS__);                                                \
            std::printf("\n");                                                       \
            ++g_fail;                                                                \
        }                                                                            \
    } while (0)

std::atomic<uint32_t> g_calls{0};
std::atomic<uint32_t> g_last_pin{0};
std::thread::id g_main_id;
std::atomic<bool> g_on_main{false};

void on_edge(uint32_t pin, void* ctx) {
    (void)ctx;
    g_last_pin.store(pin);
    if (std::this_thread::get_id() == g_main_id) g_on_main.store(true);
    g_calls.fetch_add(1);
}

bool wait_for(uint32_t n) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (g_calls.load() < n) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    return true;
}
} // namespace

int main() {
    g_main_id = std::this_thread::get_id();
    LinuxGpioDriver gpio{"/nonexistent/gpiochip-test"};
    CHECK(gpio.configure_interrupt(20, EdgeTrigger::Rising, &on_edge, nullptr), "configure");
    CHECK(gpio.monitor().is_simulated(), "missing chip must give the simulated line");

    // disabled: edges are drained, not delivered
    gpio.monitor().trigger_simulated_edge();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(g_calls.load() == 0, "handler ran while disabled (%u)", g_calls.load());

    gpio.enable_interrupt(20, true);
    for (uint32_t i = 1; i <= 1000; ++i) {
        gpio.monitor().trigger_simulated_edge();
        if (!wait_for(i)) {
            CHECK(false, "edge %u never reached the handler (%u calls)", i, g_calls.load());
            break;
        }
    }
    CHECK(g_calls.load() == 1000, "calls=%u", g_calls.load());
    CHECK(g_last_pin.load() == 20, "pin=%u", g_last_pin.load());
    CHECK(!g_on_main.load(), "handler must run on the edge thread, not the caller");
    CHECK(gpio.edges_delivered() == 1000, "delivered=%llu", static_cast<unsigned long long>(gpio.edges_delivered()));

    // reconfigure stops and restarts the thread; stop joins
    CHECK(gpio.configure_interrupt(21, EdgeTrigger::Both, &on_edge, nullptr), "reconfigure");
    gpio.monitor().trigger_simulated_edge();
    CHECK(wait_for(1001), "edge after reconfigure");
    CHECK(g_last_pin.load() == 21, "pin=%u", g_last_pin.load());
    gpio.stop_edge_thread();
    gpio.monitor().trigger_simulated_edge();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(g_calls.load() == 1001, "handler ran after stop (%u)", g_calls.load());

    std::printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
