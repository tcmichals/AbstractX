// @verifies [SPEC-HAL-09] docs/DESIGN_SPECIFICATION.md
/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * [SPEC-HAL-09] never Ok without a transfer (review 2026-11-04 Q-1). The driver runs on /dev/null:
 * the fd is valid but every I2C_RDWR fails (ENOTTY), so any Ok means nothing was sent.
 * Before the fix, write_regs_async (two writes, NoStart), three messages and a zero-length probe
 * returned Ok with no ioctl.
 */
#undef NDEBUG
#include "abstractx/abstractx.hpp"
#include "linux_i2c_driver.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using namespace abstractx;
using namespace abstractx::coro;

namespace {
hal::I2cResult g_r[4]{};
int g_done = 0;

Task<void> run(hal::LinuxI2cDriver& d) {
    static const uint8_t data[2] = {0x11, 0x22};
    g_r[0] = co_await d.write_regs_async(uint16_t{0x68}, uint8_t{0x10}, std::span<const uint8_t>(data, 2));
    ++g_done;
    static uint8_t rx[2];
    static const uint8_t a = 1, b = 2;
    const std::array<hal::I2cMsg, 3> three{hal::I2cMsg::write({&a, 1}), hal::I2cMsg::write({&b, 1}),
                                           hal::I2cMsg::read({rx, 2})};
    g_r[1] = co_await d.transfer_async(uint16_t{0x68}, std::span<const hal::I2cMsg>(three));
    ++g_done;
    g_r[2] = co_await d.probe_async(uint16_t{0x68});
    ++g_done;
    static uint8_t out[2];
    g_r[3] = co_await d.read_regs_async(uint16_t{0x68}, uint8_t{0x10}, std::span<uint8_t>(out, 2));
    ++g_done;
}
} // namespace

int main() {
    Config cfg{};
    cfg.trace.status_period_ms = 0;
    assert(init(cfg));
    hal::LinuxI2cDriver d("/dev/null");
    hal::I2cConfig c{};
    d.init(c);
    assert(spawn(run(d), "i2c"));
    const uint64_t t0 = hal::get_timer_driver().get_time_us();
    while (g_done < 4) {
        if (step() != kNow) hal::platform_idle_wait();
        assert(hal::get_timer_driver().get_time_us() - t0 < 5'000'000);
    }
    const char* what[4] = {"write_regs (2 writes, NoStart)", "3 messages", "zero-length probe", "write+read"};
    for (int i = 0; i < 4; ++i) {
        std::printf("[+] %-32s status %d, %zu bytes\n", what[i], static_cast<int>(g_r[i].status), g_r[i].transferred_bytes);
        assert(g_r[i].status != hal::I2cStatus::Ok && "Ok without a transfer");
    }
    d.shutdown();
    std::puts("[SUCCESS] Linux I2C never reports Ok without a transfer [SPEC-HAL-09]");
    return 0;
}
