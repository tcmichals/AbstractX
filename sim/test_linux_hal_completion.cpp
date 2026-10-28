/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: the REAL Linux SPI / I2C drivers deliver each awaiter its own result
 * [SPEC-LINUX-HAL-01]. No mocks: LinuxSpiDriver / LinuxI2cDriver run their
 * worker threads against /dev/null (every ioctl fails with ENOTTY), against a
 * path that does not exist (the simulated fallback), and with the worker not
 * started (every submit is refused). Coroutines are started once and driven by
 * Dispatcher::process(), as abstractx::step() does.
 */
// @verifies [SPEC-LINUX-HAL-01] targets/linux/SPECIFICATION.md
// @verifies [SPEC-HAL-01] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "asp_coro.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "linux_spi_driver.hpp"
#include "linux_i2c_driver.hpp"
#include "spi_worker.hpp"

#include <atomic>

#include <array>
#include <chrono>
#include <cstdio>
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

void test_spi_ioctl_failure_is_reported() {
    std::printf("[1] SPI on /dev/null: ioctl fails -> awaiter sees DmaError, 0 bytes\n");
    LinuxSpiDriver spi{"/dev/null"};
    SpiConfig cfg{};
    CHECK(spi.init(cfg), "init");
    CHECK(spi.worker().is_hardware_active(), "/dev/null must open");
    // Steady state: 1000 round trips, no pool / dispatcher growth.
    SpiResult res{};
    CHECK(run_spi(spi, res), "first transfer never resumed");
    const size_t hw0 = coro::coro_pool_high_water();
    const uint32_t drop0 = Dispatcher::dropped_resumes();
    int bad = 0;
    for (int i = 0; i < 1000; ++i) {
        if (!run_spi(spi, res) || res.status != SpiStatus::DmaError || res.transferred_bytes != 0) {
            if (bad++ == 0) {
                std::printf("  iteration %d: status=%u bytes=%zu\n", i,
                            static_cast<unsigned>(res.status), res.transferred_bytes);
            }
        }
    }
    CHECK(bad == 0, "%d of 1000 transfers did not report DmaError/0 bytes", bad);
    CHECK(coro::coro_pool_high_water() == hw0, "coroutine pool grew");
    CHECK(Dispatcher::dropped_resumes() == drop0, "dropped resumes grew");
    spi.shutdown();
}

void test_spi_simulated_result_reaches_awaiter() {
    std::printf("[2] SPI on a missing device: simulated transfer -> Ok, 4 bytes, timestamp set\n");
    LinuxSpiDriver spi{"/nonexistent/spidev-test"};
    CHECK(spi.init(SpiConfig{}), "init");
    SpiResult res{};
    CHECK(run_spi(spi, res), "transfer never resumed");
    CHECK(res.status == SpiStatus::Ok, "status=%u", static_cast<unsigned>(res.status));
    CHECK(res.transferred_bytes == 4, "bytes=%zu", res.transferred_bytes);
    CHECK(res.timestamp_us != 0, "timestamp_us is 0 (default result)");
    spi.shutdown();
}

void test_i2c_ioctl_failure_is_reported() {
    std::printf("[3] I2C on /dev/null: ioctl fails -> awaiter sees an error, 0 bytes\n");
    LinuxI2cDriver i2c{"/dev/null"};
    CHECK(i2c.init(I2cConfig{}), "init");
    I2cResult res{};
    CHECK(run_i2c(i2c, res), "transfer never resumed");
    CHECK(res.status != I2cStatus::Ok, "status is Ok although the ioctl failed");
    CHECK(res.transferred_bytes == 0, "bytes=%zu", res.transferred_bytes);
    i2c.shutdown();
}

void test_i2c_simulated_result_reaches_awaiter() {
    std::printf("[4] I2C on a missing device: simulated read -> Ok, 3 bytes, timestamp set\n");
    LinuxI2cDriver i2c{"/nonexistent/i2c-test"};
    CHECK(i2c.init(I2cConfig{}), "init");
    I2cResult res{};
    CHECK(run_i2c(i2c, res), "transfer never resumed");
    CHECK(res.status == I2cStatus::Ok, "status=%u", static_cast<unsigned>(res.status));
    CHECK(res.transferred_bytes == 3, "bytes=%zu", res.transferred_bytes);
    CHECK(res.timestamp_us != 0, "timestamp_us is 0 (default result)");
    i2c.shutdown();
}

void test_refused_submit_completes_with_error() {
    std::printf("[5] Worker not running: refused submit -> awaiter resumes with QueueFull\n");
    {
        LinuxSpiDriver spi{"/dev/null"};  // init() never called: worker not running
        SpiResult res{};
        CHECK(run_spi(spi, res), "SPI coroutine never resumed after a refused submit");
        CHECK(res.status == SpiStatus::QueueFull, "SPI status=%u", static_cast<unsigned>(res.status));
    }
    {
        LinuxI2cDriver i2c{"/dev/null"};
        I2cResult res{};
        CHECK(run_i2c(i2c, res), "I2C coroutine never resumed after a refused submit");
        CHECK(res.status == I2cStatus::QueueFull, "I2C status=%u", static_cast<unsigned>(res.status));
    }
}

std::atomic<int> g_worker_done{0};
std::atomic<int> g_worker_status{-1};
void on_worker_done(const SpiResult& r) noexcept {
    g_worker_status.store(static_cast<int>(r.status));
    g_worker_done.fetch_add(1);
}

void test_spi_worker_rejects_unequal_buffers() {
    std::printf("[6] SpiWorker: tx and rx of different sizes -> InvalidArgument, nothing clocked\n");
    target::SpiWorker w;
    w.start("");                       // no device: simulated path
    std::array<uint8_t, 2> tx{0x80, 0};
    std::array<uint8_t, 4> rx{};
    SpiRequest req{};
    req.tx_data = tx;
    req.rx_data = rx;
    req.on_rx_complete = etl::delegate<void(const SpiResult&)>::create<on_worker_done>();
    g_worker_done = 0;
    CHECK(w.submit(req), "submit refused");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (g_worker_done.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    CHECK(g_worker_status.load() == static_cast<int>(SpiStatus::InvalidArgument),
          "status=%d", g_worker_status.load());
    w.stop();
}

void test_spi_worker_stop_completes_queued() {
    std::printf("[7] SpiWorker: stop() with requests queued -> every request completes once\n");
    static std::array<uint8_t, 4> tx{1, 2, 3, 4};
    static std::array<uint8_t, 4> rx{};
    for (int round = 0; round < 200; ++round) {
        target::SpiWorker w;
        w.start("");
        g_worker_done = 0;
        int accepted = 0;
        for (int i = 0; i < 16; ++i) {
            SpiRequest req{};
            req.tx_data = tx;
            req.rx_data = rx;
            req.on_rx_complete = etl::delegate<void(const SpiResult&)>::create<on_worker_done>();
            accepted += w.submit(req) ? 1 : 0;
        }
        w.stop();
        CHECK(g_worker_done.load() == accepted, "round %d: %d of %d completed", round,
              g_worker_done.load(), accepted);
        if (g_worker_done.load() != accepted) break;
    }
}

} // namespace

int main() {
    test_spi_ioctl_failure_is_reported();
    test_spi_simulated_result_reaches_awaiter();
    test_i2c_ioctl_failure_is_reported();
    test_i2c_simulated_result_reaches_awaiter();
    test_refused_submit_completes_with_error();
    test_spi_worker_rejects_unequal_buffers();
    test_spi_worker_stop_completes_queued();
    // Leave no stale handles behind for later tests in the same process.
    Dispatcher::process();
    if (g_failures != 0) {
        std::printf("[RESULT] FAIL: %d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("[RESULT] PASS\n");
    return 0;
}
