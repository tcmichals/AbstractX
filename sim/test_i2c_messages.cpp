/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: I2C message transfers, distinct errors, timeout and bus recovery [SPEC-HAL-09]
 * A fake controller with a register-file slave at 0x1E (7-bit) and 0x2A5 (10-bit) runs
 * Linux/Zephyr-style message lists. Refusals (NotSupported / InvalidArgument) never reach the
 * hardware; NACK address/data, arbitration lost and timeout are reported distinctly with
 * msgs_done; recovery succeeds or reports BusError; 1000 register round trips stay flat.
 */
// @verifies [SPEC-HAL-09] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG

#include "abstractx/hal/i2c.hpp"
#include "asp_coro.hpp"

#include <array>
#include <cassert>
#include <cstdio>

using namespace abstractx::hal;
using abstractx::Dispatcher;
using abstractx::coro::Task;
using abstractx::coro::coro_pool_high_water;

namespace {

constexpr uint16_t kCompass = 0x1E;
constexpr uint16_t kTenBitDev = 0x2A5;

enum class Fault : uint8_t { None, NackDataAt2, ArbLost, Stretch };

class FakeI2c : public II2c {
public:
    explicit FakeI2c(I2cCapabilities caps) : caps_(caps) {}

    bool init(const I2cConfig&) override { return true; }
    bool write_read_sync(uint8_t, std::span<const uint8_t>, std::span<uint8_t>) override { return false; }
    bool write_sync(uint8_t, std::span<const uint8_t>) override { return false; }
    bool read_sync(uint8_t, std::span<uint8_t>) override { return false; }
    I2cCapabilities capabilities() const noexcept override { return caps_; }

    int starts{0};
    Fault fault{Fault::None};
    int sda_stuck_pulses{0};       // SCL pulses until the slave lets SDA go
    size_t last_msgs{0};
    std::array<uint8_t, 32> regs{};
    std::array<uint8_t, 4> regs10{};

    bool has_active() const { return active_valid_; }

    // The "ISR": run the message list. A stretching slave never completes; the driver's
    // deadline (timeout_us) then completes it with Timeout via expire_active().
    void complete_one() {
        assert(active_valid_);
        if (fault == Fault::Stretch && !active_.recover_bus) return;
        finish(run(active_));
    }
    void expire_active() {
        assert(active_valid_ && active_.timeout_us != 0);
        I2cResult r{};
        r.status = I2cStatus::Timeout;
        finish(r);
    }

protected:
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override {
        active_ = req;
        active_valid_ = true;
        ++starts;
    }

private:
    void finish(const I2cResult& r) {
        I2cRequest req = active_;
        active_valid_ = false;
        push_completion_from_isr(req, r);
        set_hardware_idle_from_isr();
    }

    I2cResult run(const I2cRequest& req) {
        I2cResult res{};
        if (req.recover_bus) {
            res.status = sda_stuck_pulses <= 9 ? I2cStatus::Ok : I2cStatus::BusError;
            if (res.status == I2cStatus::Ok) sda_stuck_pulses = 0;
            return res;
        }
        last_msgs = req.msgs.size();
        const bool ten = (req.address & kI2cTenBit) != 0;
        const uint16_t a = req.address & static_cast<uint16_t>(~kI2cTenBit);
        uint8_t* file = nullptr;
        size_t file_size = 0;
        if (!ten && a == kCompass) { file = regs.data(); file_size = regs.size(); }
        if (ten && a == kTenBitDev) { file = regs10.data(); file_size = regs10.size(); }
        if (file == nullptr) {
            res.status = I2cStatus::NackAddress;
            return res;
        }
        if (fault == Fault::ArbLost) {
            res.status = I2cStatus::ArbitrationLost;
            return res;
        }
        size_t ptr = 0;
        bool have_ptr = false;
        size_t written = 0;
        for (const I2cMsg& m : req.msgs) {
            if (m.is_read()) {
                for (uint8_t& b : m.rx) b = file[ptr++ % file_size];
                res.transferred_bytes += m.rx.size();
            } else {
                for (uint8_t b : m.tx) {
                    if (fault == Fault::NackDataAt2 && written == 2) {
                        res.status = I2cStatus::NackData;
                        return res;
                    }
                    ++written;
                    if (!have_ptr) { ptr = b; have_ptr = true; }   // first byte = register pointer
                    else { file[ptr++ % file_size] = b; }
                    ++res.transferred_bytes;
                }
            }
            if ((m.flags & I2cFlag::NoStart) == 0 && m.is_read()) have_ptr = true;
            ++res.msgs_done;
        }
        return res;
    }

    I2cCapabilities caps_;
    I2cRequest active_{};
    bool active_valid_{false};
};

void pump(FakeI2c& bus) {
    while ((bus.has_active() && bus.fault != Fault::Stretch) || Dispatcher::has_pending_resumes()) {
        if (bus.has_active()) bus.complete_one();
        Dispatcher::process();
    }
}

I2cCapabilities caps_full() {
    I2cCapabilities c{};
    c.max_hz = 400'000;
    c.fifo_bytes = 32;
    c.max_msgs = 4;
    c.ten_bit = true;
    c.no_start = true;
    c.stop_between = true;
    c.zero_length_write = true;
    c.bus_recovery = true;
    return c;
}

Task<void> xfer(FakeI2c& bus, uint16_t addr, std::span<const I2cMsg> msgs, uint32_t to, I2cResult& out) {
    out = co_await bus.transfer_async(addr, msgs, to);
}

Task<void> regs_roundtrip(FakeI2c& bus, uint8_t reg, uint8_t seed, bool& ok) {
    const std::array<uint8_t, 6> cfg{seed, uint8_t(seed + 1), uint8_t(seed + 2), uint8_t(seed + 3),
                                     uint8_t(seed + 4), uint8_t(seed + 5)};
    I2cResult w = co_await bus.write_regs_async(kCompass, reg, cfg);
    std::array<uint8_t, 6> back{};
    I2cResult r = co_await bus.read_regs_async(kCompass, reg, back);
    ok = w.status == I2cStatus::Ok && w.transferred_bytes == 7 && r.status == I2cStatus::Ok &&
         r.msgs_done == 2 && back == cfg;
}

Task<void> recover(FakeI2c& bus, I2cResult& out) { out = co_await bus.recover_bus_async(); }

} // namespace

int main() {
    std::array<uint8_t, 1> reg{0x03};
    std::array<uint8_t, 6> buf{};
    const std::array<I2cMsg, 2> wr_rd{I2cMsg::write(reg), I2cMsg::read(buf)};
    I2cResult r{};

    auto run_now = [&](FakeI2c& bus, uint16_t addr, std::span<const I2cMsg> msgs, uint32_t to) {
        auto t = xfer(bus, addr, msgs, to, r);
        t.resume();
        pump(bus);
        assert(t.done());
        return r.status;
    };

    // 1. Default driver: every message transfer and recovery refused, hardware untouched.
    {
        FakeI2c legacy({});
        assert(run_now(legacy, kCompass, wr_rd, kI2cDefaultTimeoutUs) == I2cStatus::NotSupported);
        auto t = recover(legacy, r);
        t.resume();
        assert(t.done() && r.status == I2cStatus::NotSupported && legacy.starts == 0);
    }

    // 2. Malformed (InvalidArgument) and unsupported (NotSupported): never reach the hardware.
    {
        I2cCapabilities basic{};
        basic.max_msgs = 2;
        FakeI2c bus(basic);
        assert(run_now(bus, kCompass, wr_rd, 0) == I2cStatus::InvalidArgument);           // no deadline
        assert(run_now(bus, 0x80, wr_rd, 1000) == I2cStatus::InvalidArgument);            // not 7-bit
        assert(run_now(bus, i2c_addr10(0x400), wr_rd, 1000) == I2cStatus::InvalidArgument);
        assert(run_now(bus, kCompass, std::span<const I2cMsg>{}, 1000) == I2cStatus::InvalidArgument);
        const std::array<I2cMsg, 1> empty_read{I2cMsg::read(std::span<uint8_t>{})};
        assert(run_now(bus, kCompass, empty_read, 1000) == I2cStatus::InvalidArgument);
        const std::array<I2cMsg, 1> nostart_first{I2cMsg::write(reg, I2cFlag::NoStart)};
        assert(run_now(bus, kCompass, nostart_first, 1000) == I2cStatus::InvalidArgument);
        const std::array<I2cMsg, 2> nostart_dir{I2cMsg::write(reg), I2cMsg::read(buf, I2cFlag::NoStart)};
        assert(run_now(bus, kCompass, nostart_dir, 1000) == I2cStatus::InvalidArgument);

        assert(run_now(bus, i2c_addr10(kTenBitDev), wr_rd, 1000) == I2cStatus::NotSupported);
        const std::array<I2cMsg, 2> nostart{I2cMsg::write(reg), I2cMsg::write(reg, I2cFlag::NoStart)};
        assert(run_now(bus, kCompass, nostart, 1000) == I2cStatus::NotSupported);
        const std::array<I2cMsg, 1> probe{I2cMsg::write(std::span<const uint8_t>{})};
        assert(run_now(bus, kCompass, probe, 1000) == I2cStatus::NotSupported);
        const std::array<I2cMsg, 3> three{I2cMsg::write(reg), I2cMsg::write(reg), I2cMsg::read(buf)};
        assert(run_now(bus, kCompass, three, 1000) == I2cStatus::NotSupported);
        const std::array<I2cMsg, 2> mid_stop{I2cMsg::write(reg, I2cFlag::Stop), I2cMsg::read(buf)};
        assert(run_now(bus, kCompass, mid_stop, 1000) == I2cStatus::NotSupported);
        // write_regs without NoStart: built in the awaiter up to kI2cRegWriteMax, else refused.
        std::array<uint8_t, kI2cRegWriteMax> big{};
        auto t = [](FakeI2c& b, std::span<const uint8_t> d, I2cResult& o) -> Task<void> {
            o = co_await b.write_regs_async(kCompass, 0, d);
        }(bus, big, r);
        t.resume();
        assert(t.done() && r.status == I2cStatus::NotSupported);
        assert(bus.starts == 0);

        // Combined-buffer path works: one message {reg, data...}.
        bool ok = false;
        auto rt = regs_roundtrip(bus, 0x04, 0x40, ok);
        rt.resume();
        pump(bus);
        assert(rt.done() && ok && bus.regs[0x04] == 0x40);
    }

    // 3. Full controller: messages, 10-bit address, distinct errors.
    FakeI2c bus(caps_full());
    assert(run_now(bus, kCompass, wr_rd, 1000) == I2cStatus::Ok && r.msgs_done == 2 && r.transferred_bytes == 7);
    assert(run_now(bus, i2c_addr10(kTenBitDev), wr_rd, 1000) == I2cStatus::Ok);
    assert(run_now(bus, 0x55, wr_rd, 1000) == I2cStatus::NackAddress && r.msgs_done == 0);
    assert(to_io_status(r.status) == IoStatus::NackAddress);
    const std::array<uint8_t, 4> payload{1, 2, 3, 4};
    const std::array<I2cMsg, 2> two_writes{I2cMsg::write(reg), I2cMsg::write(payload, I2cFlag::NoStart)};
    bus.fault = Fault::NackDataAt2;
    assert(run_now(bus, kCompass, two_writes, 1000) == I2cStatus::NackData && r.msgs_done == 1);
    assert(to_io_status(r.status) == IoStatus::NackData);
    bus.fault = Fault::ArbLost;
    assert(run_now(bus, kCompass, wr_rd, 1000) == I2cStatus::ArbitrationLost);
    assert(to_io_status(r.status) == IoStatus::ArbitrationLost);

    // Clock stretching forever: the request's deadline completes it with Timeout.
    bus.fault = Fault::Stretch;
    {
        auto t = xfer(bus, kCompass, wr_rd, 500, r);
        t.resume();
        bus.complete_one();             // slave holds SCL: nothing happens
        Dispatcher::process();
        assert(!t.done());
        bus.expire_active();            // driver deadline
        while (Dispatcher::has_pending_resumes()) Dispatcher::process();
        assert(t.done() && r.status == I2cStatus::Timeout);
    }
    bus.fault = Fault::None;

    // 4. Bus recovery.
    bus.sda_stuck_pulses = 5;
    {
        auto t = recover(bus, r);
        t.resume();
        pump(bus);
        assert(t.done() && r.status == I2cStatus::Ok);
    }
    bus.sda_stuck_pulses = 20;
    {
        auto t = recover(bus, r);
        t.resume();
        pump(bus);
        assert(t.done() && r.status == I2cStatus::BusError);
    }

    // 5. Steady state: 1000 register write/read round trips (NoStart path).
    {
        bool ok = false;
        {
            auto w = regs_roundtrip(bus, 0, 0, ok);
            w.resume();
            pump(bus);
            assert(ok);
        }
        const size_t hw0 = coro_pool_high_water();
        const uint32_t dropped0 = Dispatcher::dropped_resumes();
        for (int i = 0; i < 1000; ++i) {
            auto t = regs_roundtrip(bus, static_cast<uint8_t>(i % 26), static_cast<uint8_t>(i), ok);
            t.resume();
            pump(bus);
            assert(t.done() && ok && bus.last_msgs == 2);
        }
        assert(coro_pool_high_water() == hw0);
        assert(Dispatcher::dropped_resumes() == dropped0);
        assert(!bus.has_pending_requests() && !bus.has_active() && Dispatcher::queue_depth() == 0);
    }

    std::printf("[PASS] I2C messages: refusals never reach hardware; NACK/arbitration/timeout distinct; recovery; 1000 rounds flat\n");
    return 0;
}
