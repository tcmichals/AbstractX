/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * E906 TWI driver (targets/allwinner_e906/src/i2c.cpp) against a register model of the
 * Allwinner TWI controller and one slave device. Runs on the host.
 *
 * @verifies [SPEC-E906-HAL-04] E906I2c transfer_sync on_irq init capabilities poll_timeout
 * @verifies [SPEC-HAL-15] TwiPort E906I2c
 */

#include "hal/i2c.hpp"
#include "hal/timer.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "asp_coro.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <span>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

// ---------------------------------------------------------------------------
// Fakes for the target services the driver calls.
// ---------------------------------------------------------------------------
namespace {
uint64_t g_ticks = 0;
uint64_t g_wake = 0;
int g_attached = 0;
}  // namespace

namespace hal {
uint64_t Timer::get_ticks() noexcept { return g_ticks; }
uint64_t Timer::get_time_ns() noexcept { return g_ticks * 1000ULL / 24ULL; }
void Timer::arm_wake(uint64_t t) noexcept { g_wake = t; }
void Timer::delay_us(uint32_t us) noexcept { g_ticks += us * TICKS_PER_US; }
void Ccu::enable_module(BusModule) noexcept {}
namespace irq {
bool attach(uint32_t, Handler, void*) noexcept { ++g_attached; return true; }
void describe(uint32_t, const char*, const volatile uint32_t*) noexcept {}
}  // namespace irq
}  // namespace hal

namespace abstractx::hal {
void platform_idle_wait() noexcept {}
}

// ---------------------------------------------------------------------------
// Register model: offsets are word indices into `regs`.
// ---------------------------------------------------------------------------
namespace {

using abstractx::hal::E906I2c;
using abstractx::hal::I2cStatus;
using abstractx::Dispatcher;
namespace coro = abstractx::coro;

constexpr size_t kData = 0x08 / 4, kCntr = 0x0C / 4, kStat = 0x10 / 4, kCcr = 0x14 / 4;
constexpr uint32_t kIntEn = 1U << 7, kMSta = 1U << 5, kMStp = 1U << 4, kIntFlg = 1U << 3,
                   kAAck = 1U << 2;

alignas(4) uint32_t regs[0x20];
uint32_t pin_cfg = 0x0000'0077U;  // PB0/PB1 bits set: must survive TWI1 pinmux

const ::hal::ports::TwiPort kPort{reinterpret_cast<uintptr_t>(regs), 43,
                                  ::hal::Ccu::BusModule::Twi1,
                                  {reinterpret_cast<uintptr_t>(&pin_cfg), 0xFFU << 16, 0x22U << 16},
                                  "twi-test"};
E906I2c dut{kPort};

// One slave at kDev with 8 registers; a write sets the register pointer, then data.
struct Slave {
    uint8_t dev = 0x1E;
    std::array<uint8_t, 8> mem{};
    bool present = true;
    bool stuck = false;     // never answers: timeout
    enum class Phase { Idle, Addr, Write, Read } phase = Phase::Idle;
    bool started = false;
    bool first_write = true;
    uint8_t ptr = 0;
    uint32_t steps = 0;
} s;

// Called by the driver while it waits: perform one bus step for the command in CNTR.
void bus_step(uint64_t) noexcept {
    g_ticks += 24 * 100;  // 100 us per step
    uint32_t c = regs[kCntr];
    if (c & kMStp) {  // STOP finishes
        regs[kCntr] = c & ~kMStp;
        s.phase = Slave::Phase::Idle;
        s.started = false;
        return;
    }
    if (!(c & kIntEn) || s.stuck) return;
    uint32_t stat = 0;
    if (c & kMSta) {
        stat = s.started ? 0x10 : 0x08;
        s.started = true;
        s.phase = Slave::Phase::Addr;
    } else if (s.phase == Slave::Phase::Addr) {
        const uint8_t a = static_cast<uint8_t>(regs[kData] >> 1);
        const bool rd = regs[kData] & 1U;
        if (!s.present || a != s.dev) {
            stat = rd ? 0x48 : 0x20;
        } else {
            stat = rd ? 0x40 : 0x18;
            s.phase = rd ? Slave::Phase::Read : Slave::Phase::Write;
            s.first_write = true;
        }
    } else if (s.phase == Slave::Phase::Write) {
        const uint8_t b = static_cast<uint8_t>(regs[kData]);
        if (s.first_write) s.ptr = b & 7U; else s.mem[s.ptr++ & 7U] = b;
        s.first_write = false;
        stat = 0x28;
    } else if (s.phase == Slave::Phase::Read) {
        regs[kData] = s.mem[s.ptr++ & 7U];
        stat = (c & kAAck) ? 0x50 : 0x58;
    }
    ++s.steps;
    regs[kStat] = stat;
    regs[kCntr] = (c & ~kIntFlg) | kIntFlg;  // hardware raises INT_FLG
    dut.on_irq();
}

// One async write+read through the core awaiter, as an application issues it.
coro::Task<void> async_read(E906I2c& bus, uint8_t dev, uint8_t reg, uint8_t& out,
                            abstractx::hal::I2cResult& res, uint32_t timeout_us) {
    std::array<uint8_t, 1> w{reg};
    std::array<abstractx::hal::I2cMsg, 2> msgs{abstractx::hal::I2cMsg::write(w),
                                               abstractx::hal::I2cMsg::read(std::span<uint8_t>(&out, 1))};
    res = co_await bus.transfer_async(dev, msgs, timeout_us);
}

// The loop: hardware steps, the per-step timeout poll, then the dispatcher ([AGENTS 10.6]).
void pump(abstractx::hal::I2cResult& res, int max_steps) {
    for (int i = 0; i < max_steps && res.status == I2cStatus::Cancelled; ++i) {
        bus_step(0);
        abstractx::hal::poll_i2c_timeouts();  // what platform_poll_network() calls
        dut.poll_timeout();
        Dispatcher::process();
    }
}

}  // namespace

int main() {
    E906I2c::set_wait_hook(bus_step);

    // init: pins of this port only, 400 kHz CCR, one handler on the shared line
    abstractx::hal::I2cConfig cfg{};
    CHECK(dut.init(cfg));
    CHECK(pin_cfg == ((0x22U << 16) | 0x77U));
    CHECK(regs[kCcr] == ((2U << 3) | 1U));
    CHECK(dut.init(cfg) && g_attached == 1);  // re-init never attaches twice
    cfg.frequency_hz = 100'000;
    dut.set_frequency(cfg.frequency_hz);
    CHECK(regs[kCcr] == ((5U << 3) | 2U));
    CHECK(dut.capabilities().max_msgs == 2 && dut.capabilities().zero_length_write);

    // shared line: an interrupt for the other TWI (our INT_FLG clear) is ignored
    regs[kCntr] = 0;
    dut.on_irq();
    CHECK(regs[kCntr] == 0);

    // write register 2 = {0xAA, 0xBB}, then read it back with a repeated START
    const uint8_t w[] = {2, 0xAA, 0xBB};
    CHECK(dut.transfer_sync(s.dev, w, {}) == I2cStatus::Ok);
    CHECK(s.mem[2] == 0xAA && s.mem[3] == 0xBB);
    const uint8_t r_reg[] = {2};
    uint8_t rx[2] = {};
    CHECK(dut.transfer_sync(s.dev, r_reg, rx) == I2cStatus::Ok);
    CHECK(rx[0] == 0xAA && rx[1] == 0xBB && dut.bytes_read() == 2);

    // single-byte read (NACK on the only byte), read-only transfer
    s.ptr = 3;
    uint8_t one = 0;
    CHECK(dut.transfer_sync(s.dev, {}, std::span<uint8_t>(&one, 1)) == I2cStatus::Ok);
    CHECK(one == 0xBB);

    // address probe: present ACKs, absent NACKs and the bus gets a STOP
    CHECK(dut.transfer_sync(s.dev, {}, {}) == I2cStatus::Ok);
    CHECK(dut.transfer_sync(0x50, {}, {}) == I2cStatus::NackAddress);
    CHECK(!dut.is_busy());

    // stuck bus: 20 ms deadline, STOP sent, counted
    s.stuck = true;
    CHECK(dut.transfer_sync(s.dev, w, {}) == I2cStatus::Timeout);
    CHECK(dut.timeouts() == 1 && !dut.is_busy());
    s.stuck = false;
    s.phase = Slave::Phase::Idle;
    s.started = false;

    // steady state ([AGENTS 10.5]): 1000 write+read cycles, no timeouts, no stuck state
    for (int i = 0; i < 1000; ++i) {
        const uint8_t v = static_cast<uint8_t>(i);
        const uint8_t wi[] = {5, v};
        CHECK(dut.transfer_sync(s.dev, wi, {}) == I2cStatus::Ok);
        const uint8_t ri[] = {5};
        uint8_t got = 0;
        CHECK(dut.transfer_sync(s.dev, ri, std::span<uint8_t>(&got, 1)) == I2cStatus::Ok);
        CHECK(got == v);
    }
    CHECK(dut.timeouts() == 1 && !dut.is_busy());

    // async: completes once with the data; the start armed a wake at its deadline
    {
        s.mem[6] = 0x5A;
        uint8_t got = 0;
        abstractx::hal::I2cResult res{};
        res.status = I2cStatus::Cancelled;  // "not finished yet" marker for pump()
        const uint64_t t0 = g_ticks;
        auto task = async_read(dut, s.dev, 6, got, res, 5'000);
        task.resume();
        CHECK(g_wake == t0 + 5'000ULL * 24ULL);
        pump(res, 100);
        CHECK(res.status == I2cStatus::Ok && got == 0x5A && res.msgs_done == 2);
        CHECK(!dut.is_busy());
    }

    // async timeout: stuck bus, the loop poll ends it with Timeout and STOP, exactly once
    {
        s.stuck = true;
        uint8_t got = 0;
        abstractx::hal::I2cResult res{};
        res.status = I2cStatus::Cancelled;
        const uint32_t before = dut.timeouts();
        auto task = async_read(dut, s.dev, 6, got, res, 1'000);
        task.resume();
        pump(res, 100);  // 100 us per step: deadline after ~10 steps
        CHECK(res.status == I2cStatus::Timeout);
        CHECK(dut.timeouts() == before + 1 && !dut.is_busy());
        CHECK(regs[kCntr] & kMStp);  // STOP was issued
        for (int i = 0; i < 5; ++i) { dut.poll_timeout(); Dispatcher::process(); }
        CHECK(dut.timeouts() == before + 1);  // never completed twice
        s.stuck = false;
        bus_step(0);  // STOP finishes
        s.phase = Slave::Phase::Idle;
        s.started = false;
    }

    // async steady state: 1000 transfers, no growth of the coroutine pool or dropped resumes
    {
        const size_t hw0 = coro::coro_pool_high_water();
        const size_t used0 = coro::coro_pool_used();
        const auto drop0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            s.mem[7] = static_cast<uint8_t>(i);
            uint8_t got = 0;
            abstractx::hal::I2cResult res{};
            res.status = I2cStatus::Cancelled;
            auto task = async_read(dut, s.dev, 7, got, res, 5'000);
            task.resume();
            pump(res, 100);
            CHECK(res.status == I2cStatus::Ok && got == static_cast<uint8_t>(i));
        }
        CHECK(coro::coro_pool_high_water() == hw0 && coro::coro_pool_used() == used0);
        CHECK(Dispatcher::dropped_resumes() == drop0);
    }

    std::printf("PASS test_e906_twi (%u bus steps)\n", s.steps);
    return 0;
}
