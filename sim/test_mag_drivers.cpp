/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: applib magnetometer drivers on a fake I2C bus [SPEC-MAG-01] (EL-1027-23).
 * A register-file slave per part (HMC5883L at 0x1E, QMC5883L at 0x0D). Checks: a NACK on any
 * configuration write fails init_async(); axis order and scale; UniversalCompass picks the part
 * that answers; the packed TLP is telemetry (DmaStream, Telemetry, tag 3); 1000 reads stay flat.
 * Coroutines are started once and pumped through the Dispatcher.
 */
// @verifies [SPEC-MAG-01] docs/DESIGN_SPECIFICATION.md
#undef NDEBUG
#include "applib/drivers/mag/compass.hpp"
#include "asp_coro.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace abstractx::hal;
using abstractx::Dispatcher;
using abstractx::coro::Task;
using namespace applib::drivers::mag;

namespace {

constexpr uint16_t kHmc = 0x1E;
constexpr uint16_t kQmc = 0x0D;

class FakeMagBus : public II2c {
public:
    bool init(const I2cConfig&) override { return true; }
    bool write_read_sync(uint8_t, std::span<const uint8_t>, std::span<uint8_t>) override { return false; }
    bool write_sync(uint8_t, std::span<const uint8_t>) override { return false; }
    bool read_sync(uint8_t, std::span<uint8_t>) override { return false; }
    I2cCapabilities capabilities() const noexcept override {
        I2cCapabilities c{};            // a controller that can do what the drivers ask
        c.max_hz = 400'000;
        c.fifo_bytes = 32;
        c.max_msgs = 4;
        c.stop_between = true;
        c.zero_length_write = true;     // probe_async
        return c;
    }

    bool hmc_present{true};
    bool qmc_present{true};
    int nack_write_dev{-1};           // NACK a data write to this register of this device
    int nack_write_reg{-1};
    std::array<uint8_t, 16> hmc{};
    std::array<uint8_t, 16> qmc{};

    bool has_active() const { return active_valid_; }
    void complete_one() {
        I2cRequest req = active_;
        active_valid_ = false;
        push_completion_from_isr(req, run(req));
        set_hardware_idle_from_isr();
    }

protected:
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override {
        active_ = req;
        active_valid_ = true;
    }

private:
    I2cResult run(const I2cRequest& req) {
        I2cResult res{};
        const uint16_t a = req.address;
        uint8_t* file = (a == kHmc && hmc_present) ? hmc.data() : (a == kQmc && qmc_present) ? qmc.data() : nullptr;
        if (file == nullptr) {
            res.status = I2cStatus::NackAddress;
            return res;
        }
        size_t ptr = 0;
        bool have_ptr = false;
        for (const I2cMsg& m : req.msgs) {
            if (m.is_read()) {
                for (uint8_t& b : m.rx) b = file[ptr++ % 16];
                res.transferred_bytes += m.rx.size();
            } else {
                for (uint8_t b : m.tx) {
                    if (!have_ptr) {
                        ptr = b;
                        have_ptr = true;
                    } else {
                        if (static_cast<int>(a) == nack_write_dev && static_cast<int>(ptr) == nack_write_reg) {
                            res.status = I2cStatus::NackData;
                            return res;
                        }
                        file[ptr++ % 16] = b;
                    }
                    ++res.transferred_bytes;
                }
            }
            ++res.msgs_done;
        }
        return res;
    }

    I2cRequest active_{};
    bool active_valid_{false};
};

void pump(FakeMagBus& bus) {
    while (bus.has_active() || Dispatcher::has_pending_resumes()) {
        if (bus.has_active()) bus.complete_one();
        Dispatcher::process();
    }
}

template <typename Driver>
Task<void> run_init(Driver& d, bool& ok, bool& done) {
    ok = co_await d.init_async();
    done = true;
}

template <typename Driver>
bool init_on(FakeMagBus& bus, Driver& d) {
    bool ok = false, done = false;
    auto t = run_init(d, ok, done);
    t.resume();
    pump(bus);
    assert(done && "init_async must finish");
    return ok;
}

template <typename Driver>
Task<void> run_read(Driver& d, MagSample& s) {
    s = co_await d.read_sample_async();
}

template <typename Driver>
MagSample read_on(FakeMagBus& bus, Driver& d) {
    MagSample s{};
    auto t = run_read(d, s);
    t.resume();
    pump(bus);
    assert(t.done());
    return s;
}

void set_hmc_id(FakeMagBus& bus) {
    bus.hmc[HmcRegs::ID_A] = 'H';
    bus.hmc[HmcRegs::ID_B] = '4';
    bus.hmc[HmcRegs::ID_C] = '3';
}

void test_hmc_init_fails_on_any_config_nack() {
    for (uint8_t reg : {HmcRegs::CONFIG_A, HmcRegs::CONFIG_B, HmcRegs::MODE}) {
        FakeMagBus bus;
        set_hmc_id(bus);
        bus.nack_write_dev = kHmc;
        bus.nack_write_reg = reg;
        Hmc5883l hmc(bus);
        assert(!init_on(bus, hmc) && "a failed configuration write must fail init");
        assert(!read_on(bus, hmc).valid && "no sample from an unconfigured part");
    }
    FakeMagBus bus;
    set_hmc_id(bus);
    Hmc5883l hmc(bus);
    assert(init_on(bus, hmc));
    assert(bus.hmc[HmcRegs::CONFIG_A] == 0x78 && bus.hmc[HmcRegs::CONFIG_B] == 0x20 && bus.hmc[HmcRegs::MODE] == 0x00);
}

void test_qmc_init_fails_on_any_config_nack() {
    for (uint8_t reg : {QmcRegs::CONTROL_2, QmcRegs::PERIOD, QmcRegs::CONTROL_1}) {
        FakeMagBus bus;
        bus.nack_write_dev = kQmc;
        bus.nack_write_reg = reg;
        Qmc5883l qmc(bus);
        assert(!init_on(bus, qmc) && "a failed configuration write must fail init");
    }
}

void test_hmc_axis_order_and_scale() {
    FakeMagBus bus;
    set_hmc_id(bus);
    Hmc5883l hmc(bus);
    assert(init_on(bus, hmc));
    // Register order X, Z, Y (big endian): X = 1090, Z = -1090, Y = 545
    const int16_t x = 1090, z = -1090, y = 545;
    bus.hmc[3] = static_cast<uint8_t>(x >> 8); bus.hmc[4] = static_cast<uint8_t>(x);
    bus.hmc[5] = static_cast<uint8_t>(static_cast<uint16_t>(z) >> 8); bus.hmc[6] = static_cast<uint8_t>(z);
    bus.hmc[7] = static_cast<uint8_t>(y >> 8); bus.hmc[8] = static_cast<uint8_t>(y);
    const MagSample s = read_on(bus, hmc);
    assert(s.valid);
    assert(std::fabs(s.mag_mgauss[0] - 1000.0f) < 0.5f);
    assert(std::fabs(s.mag_mgauss[1] - 500.0f) < 0.5f);
    assert(std::fabs(s.mag_mgauss[2] + 1000.0f) < 0.5f);
}

void check_tlp(const abstractx::Tlp64& t) {
    assert(t.wire.type == static_cast<uint8_t>(abstractx::TlpType::DmaStream) && "telemetry, not MemRead");
    assert(t.wire.channel == static_cast<uint8_t>(abstractx::Channel::Telemetry));
    assert(t.wire.tag == 3);
}

void test_universal_picks_part_and_packs_telemetry() {
    {
        FakeMagBus bus;
        set_hmc_id(bus);
        UniversalCompass c(bus);
        assert(init_on(bus, c));
        MagSample s{};
        s.timestamp_us = 7;
        check_tlp(c.pack_tlp(s));
        assert(c.pack_tlp(s).wire.timestamp_ns == 7000);
    }
    {
        FakeMagBus bus;                 // HMC answers but its ID is wrong: fall back to QMC
        UniversalCompass c(bus);
        assert(init_on(bus, c));
        assert(bus.qmc[QmcRegs::CONTROL_1] == 0x1D && "QMC configured");
        check_tlp(c.pack_tlp(MagSample{}));
    }
    {
        FakeMagBus bus;
        bus.hmc_present = false;
        bus.qmc_present = false;
        UniversalCompass c(bus);
        assert(!init_on(bus, c));
    }
    check_tlp(Hmc5883l::to_tlp(MagSample{}));
    check_tlp(Qmc5883l::to_tlp(MagSample{}));
}

void test_steady_state_1000_reads() {
    FakeMagBus bus;
    Qmc5883l qmc(bus);
    assert(init_on(bus, qmc));
    (void)read_on(bus, qmc);
    const size_t hw0 = abstractx::coro::coro_pool_high_water();
    const uint32_t dropped0 = Dispatcher::dropped_resumes();
    for (int i = 0; i < 1000; ++i) {
        bus.qmc[0] = static_cast<uint8_t>(i);
        const MagSample s = read_on(bus, qmc);
        assert(s.valid);
    }
    assert(abstractx::coro::coro_pool_high_water() == hw0);
    assert(Dispatcher::dropped_resumes() == dropped0);
}

} // namespace

int main() {
    test_hmc_init_fails_on_any_config_nack();
    test_qmc_init_fails_on_any_config_nack();
    test_hmc_axis_order_and_scale();
    test_universal_picks_part_and_packs_telemetry();
    test_steady_state_1000_reads();
    std::printf("[PASS] magnetometer drivers: init errors, axes, part choice, telemetry TLP, 1000 reads flat\n");
    return 0;
}
