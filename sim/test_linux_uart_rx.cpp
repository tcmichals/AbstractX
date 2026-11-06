// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// @verifies [SPEC-LINUX-HAL-03] targets/linux/SPECIFICATION.md (LinuxUartDriver rx_ring reader_loop)
// A pseudo-terminal is the device: bytes written to the master reach read_async on the slave through
// the reader thread; a partial frame ends the read; nothing arriving ends it with Timeout.
#undef NDEBUG
#include "linux_uart_driver.hpp"
#include "asp_coro.hpp"
#include "abstractx/domain_dispatcher.hpp"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <thread>

using namespace abstractx;
using namespace abstractx::hal;
using abstractx::coro::Task;

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

UartRxView g_view{};
Task<void> reader(IUart& u, size_t min, uint32_t to) { g_view = co_await u.read_async(min, to); }

bool pump(Task<void>& t, int ms = 2000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!t.done()) {
        Dispatcher::process();
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    return true;
}
} // namespace

int main() {
    const int master = ::posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(master >= 0 && ::grantpt(master) == 0 && ::unlockpt(master) == 0, "pty");
    const char* slave = ::ptsname(master);
    LinuxUartDriver uart;
    CHECK(uart.open(slave, 115200), "open %s", slave);

    // bytes -> read_async (min 4): one frame of 4
    {
        auto t = reader(uart, 4, 0);
        t.resume();
        CHECK(::write(master, "ABCD", 4) == 4, "write");
        CHECK(pump(t), "read never completed");
        CHECK(g_view.status == UartStatus::Ok && g_view.size() == 4 && g_view[0] == 'A' && g_view[3] == 'D',
              "status=%u size=%zu", static_cast<unsigned>(g_view.status), g_view.size());
        CHECK(uart.release_rx(g_view.size()) == UartStatus::Ok, "release");
    }
    // one consumer: polled read gets nothing while the ring owns the fd
    {
        uint8_t b[4];
        CHECK(::write(master, "Z", 1) == 1, "write");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(uart.read(b) == 0, "polled read took ring bytes");
        auto t = reader(uart, 1, 0);
        t.resume();
        CHECK(pump(t) && g_view.size() == 1 && g_view[0] == 'Z', "byte went to the ring");
        (void)uart.release_rx(g_view.size());
    }
    // partial frame (3 of 10) ends the read at end of frame
    {
        auto t = reader(uart, 10, 0);
        t.resume();
        CHECK(::write(master, "xyz", 3) == 3, "write");
        CHECK(pump(t), "partial frame never ended the read");
        CHECK(g_view.status == UartStatus::Ok && g_view.size() == 3, "size=%zu", g_view.size());
        (void)uart.release_rx(g_view.size());
    }
    // deadline: nothing arrives -> Timeout with 0 bytes
    {
        const auto t0 = std::chrono::steady_clock::now();
        auto t = reader(uart, 8, 30'000);
        t.resume();
        CHECK(pump(t), "deadline never fired");
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        CHECK(g_view.status == UartStatus::Timeout && g_view.size() == 0,
              "status=%u size=%zu", static_cast<unsigned>(g_view.status), g_view.size());
        CHECK(ms >= 25 && ms < 1000, "timeout after %lld ms", static_cast<long long>(ms));
    }
    // 1000 reads, pool flat
    {
        size_t hw0 = 0;
        for (int i = 0; i < 1000; ++i) {
            if (i == 1) hw0 = coro::coro_pool_high_water();
            auto t = reader(uart, 2, 100'000);
            t.resume();
            if (::write(master, "ok", 2) != 2 || !pump(t) || g_view.status != UartStatus::Ok || g_view.size() != 2) {
                CHECK(false, "iteration %d: status=%u size=%zu", i, static_cast<unsigned>(g_view.status), g_view.size());
                break;
            }
            (void)uart.release_rx(g_view.size());
        }
        CHECK(coro::coro_pool_high_water() == hw0, "coroutine pool grew");
        CHECK(Dispatcher::dropped_resumes() == 0, "dropped resumes");
    }
    uart.close();
    ::close(master);
    std::printf("%s: %d failure(s)\n", g_fail ? "FAIL" : "PASS", g_fail);
    return g_fail ? 1 : 0;
}
