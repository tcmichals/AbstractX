/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * No silent simulation ([SPEC-LINUX-HAL-01], full-audit buses finding 8): without ABSTRACTX_SIM_BUS a
 * missing SPI / I2C device fails the transfer (BusError, 0 bytes; sync calls return false) instead of
 * returning invented data with Ok. Runs as its own process: the variable is read once per process.
 *
 * @verifies [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md (linux_bus_sim_allowed)
 */
#undef NDEBUG

#include "asp_coro.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "bus_sim.hpp"
#include "linux_spi_driver.hpp"
#include "linux_i2c_driver.hpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

using namespace abstractx;
using namespace abstractx::hal;
using abstractx::coro::Task;

namespace {

int g_failures = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("  FAIL %s:%d: %s -- ", __FILE__, __LINE__, #cond); \
            std::printf(__VA_ARGS__);                                      \
            std::printf("\n");                                             \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

// Pump the dispatcher until the task finishes or 2 s pass. Returns false on timeout
// (a coroutine that is never resumed is a failure, not a hang of the test).
template <typename T>
bool pump_until_done(Task<T>& task) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!task.done()) {
        Dispatcher::process();
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::microseconds(20));
    }
    return true;
}

Task<void> spi_xfer(ISpi& spi, SpiResult& out) {
    // Buffers live in this coroutine frame, which outlives the completion.
    std::array<uint8_t, 4> tx{0x9F, 1, 2, 3};
    std::array<uint8_t, 4> rx{};
    out = co_await spi.transfer_async(tx, rx);
}

Task<void> i2c_xfer(II2c& i2c, I2cResult& out) {
    std::array<uint8_t, 1> tx{0x75};
    std::array<uint8_t, 3> rx{};
    out = co_await i2c.transfer_async(0x68, tx, rx);
}

bool run_spi(ISpi& spi, SpiResult& res) {
    res = SpiResult{};
    res.transferred_bytes = 0xDEAD;  // must be overwritten by the driver
    auto t = spi_xfer(spi, res);
    t.resume();  // start once
    return pump_until_done(t);
}

bool run_i2c(II2c& i2c, I2cResult& res) {
    res = I2cResult{};
    res.transferred_bytes = 0xDEAD;
    auto t = i2c_xfer(i2c, res);
    t.resume();
    return pump_until_done(t);
}

} // namespace

int main() {
    ::unsetenv("ABSTRACTX_SIM_BUS");
    CHECK(!linux_bus_sim_allowed(), "simulation must be off by default");

    LinuxSpiDriver spi{"/nonexistent/spidev-test"};
    CHECK(spi.init(SpiConfig{}), "init");
    for (int i = 0; i < 1000; ++i) {
        SpiResult res{};
        CHECK(run_spi(spi, res), "SPI transfer never resumed");
        if (res.status != SpiStatus::BusError || res.transferred_bytes != 0) {
            CHECK(false, "SPI on a missing device: status=%u bytes=%zu (iteration %d)",
                  static_cast<unsigned>(res.status), res.transferred_bytes, i);
            break;
        }
    }
    spi.shutdown();

    LinuxI2cDriver i2c{"/nonexistent/i2c-test"};
    CHECK(i2c.init(I2cConfig{}), "init");
    I2cResult ires{};
    CHECK(run_i2c(i2c, ires), "I2C transfer never resumed");
    CHECK(ires.status == I2cStatus::BusError && ires.transferred_bytes == 0,
          "I2C on a missing device: status=%u bytes=%zu", static_cast<unsigned>(ires.status), ires.transferred_bytes);
    std::array<uint8_t, 2> buf{};
    CHECK(!i2c.read_sync(0x68, buf), "sync read on a missing device reported success");
    CHECK(!i2c.write_sync(0x68, buf), "sync write on a missing device reported success");
    i2c.shutdown();

    std::printf("%s: %d failure(s)\n", g_failures ? "FAIL" : "PASS", g_failures);
    return g_failures ? 1 : 0;
}
