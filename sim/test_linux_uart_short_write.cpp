// @verifies [SPEC-HAL-10] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * [SPEC-HAL-10] a UART write never reports Ok for bytes not sent (review 2026-11-04 Q-8).
 * The Linux UART is pointed at a FIFO whose kernel buffer is full: write() returns EAGAIN, so the
 * driver sends 0 bytes and must complete with QueueFull and a count of 0. With room again, the same
 * write completes Ok with every byte.
 */
#undef NDEBUG
#include "abstractx/abstractx.hpp"
#include "linux_uart.hpp"

#include <cassert>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

using namespace abstractx;
using namespace abstractx::coro;

namespace {
hal::UartResult g_res{};
bool g_done = false;
uint8_t g_msg[100];
Task<void> writer() {
    g_res = co_await hal::get_uart_driver().write_async(std::span<const uint8_t>(g_msg, sizeof g_msg));
    g_done = true;
}
void run_one() {
    g_done = false;
    assert(spawn(writer(), "w"));
    const uint64_t t0 = hal::get_timer_driver().get_time_us();
    while (!g_done) {
        if (step() != kNow) hal::platform_idle_wait();
        assert(hal::get_timer_driver().get_time_us() - t0 < 2'000'000);
    }
}
} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 0;
    assert(init(cfg));
    const std::string fifo = "/tmp/abx_uart_fifo_" + std::to_string(::getpid());
    assert(::mkfifo(fifo.c_str(), 0600) == 0);
    const int rd = ::open(fifo.c_str(), O_RDONLY | O_NONBLOCK);  // keep a reader so writes are accepted
    assert(rd >= 0);
    assert(hal::linux_uart_open(fifo.c_str()));
    const int filler = ::open(fifo.c_str(), O_WRONLY | O_NONBLOCK);
    uint8_t junk[4096] = {};
    while (::write(filler, junk, sizeof junk) > 0) {}  // fill the pipe buffer until EAGAIN

    run_one();
    std::printf("[+] full buffer: status %d, %zu bytes\n", static_cast<int>(g_res.status), g_res.bytes_transferred);
    assert(g_res.status == hal::UartStatus::QueueFull && g_res.bytes_transferred == 0 &&
           "a write that sent nothing must not be Ok");

    while (::read(rd, junk, sizeof junk) > 0) {}  // drain: room again
    run_one();
    std::printf("[+] room again: status %d, %zu bytes\n", static_cast<int>(g_res.status), g_res.bytes_transferred);
    assert(g_res.status == hal::UartStatus::Ok && g_res.bytes_transferred == sizeof g_msg);

    hal::linux_uart_close();
    ::close(filler);
    ::close(rd);
    ::unlink(fifo.c_str());
    std::puts("[SUCCESS] UART write never reports Ok for bytes not sent [SPEC-HAL-10]");
    return 0;
}
