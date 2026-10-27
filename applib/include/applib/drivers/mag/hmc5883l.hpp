/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HMC5883L 3-Axis Digital Magnetometer Driver
 * -----------------------------------------------------
 * Fully asynchronous C++20 coroutine I2C driver:
 * - Zero dynamic heap allocation (0 B)
 * - 75 Hz / 15 Hz continuous magnetic field sampling
 * - Non-blocking register initialization and burst reading
 * - Standard 64-byte TLP telemetry packaging
 */
// @impl [SPEC-MAG-01] docs/DESIGN_SPECIFICATION.md

#ifndef APPLIB_DRIVERS_MAG_HMC5883L_HPP
#define APPLIB_DRIVERS_MAG_HMC5883L_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include <cmath>

#include "abstractx/coro.hpp"
#include "abstractx/hal/i2c.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/trace/tracer.hpp"
#include "abstractx/sensor/sensor_interface.hpp"
#include "asp_tlp64.hpp"

namespace applib::drivers::mag {

namespace hal = abstractx::hal;
namespace coro = abstractx::coro;
namespace trace = abstractx::trace;
using abstractx::Tlp64;
using abstractx::Channel;
using abstractx::sensor::IMagDriver;
using abstractx::sensor::mag::MagSample;

namespace HmcRegs {
    constexpr uint8_t CONFIG_A    = 0x00;
    constexpr uint8_t CONFIG_B    = 0x01;
    constexpr uint8_t MODE        = 0x02;
    constexpr uint8_t DATA_X_MSB  = 0x03;
    constexpr uint8_t DATA_X_LSB  = 0x04;
    constexpr uint8_t DATA_Z_MSB  = 0x05;
    constexpr uint8_t DATA_Z_LSB  = 0x06;
    constexpr uint8_t DATA_Y_MSB  = 0x07;
    constexpr uint8_t DATA_Y_LSB  = 0x08;
    constexpr uint8_t STATUS      = 0x09;
    constexpr uint8_t ID_A        = 0x0A; // 'H'
    constexpr uint8_t ID_B        = 0x0B; // '4'
    constexpr uint8_t ID_C        = 0x0C; // '3'
}

constexpr uint8_t TLP_TAG_MAG_HMC = 0x03;
constexpr uint8_t DEFAULT_HMC_I2C_ADDR = 0x1E;

class Hmc5883l : public IMagDriver {
public:
    explicit Hmc5883l(hal::II2c& i2c_driver, uint8_t slave_addr = DEFAULT_HMC_I2C_ADDR)
        : i2c_(i2c_driver), slave_addr_(slave_addr) {}

    /*
     * C++20 Coroutine Async Lifecycle Initialization
     */
    coro::Task<bool> init_async() override {
        hal::I2cConfig config{};
        config.frequency_hz = 400'000;
        config.speed = hal::I2cSpeed::Fast_400k;
        if (!i2c_.init(config)) {
            co_return false;
        }

        // Verify ID registers ('H', '4', '3')
        uint8_t id_reg = HmcRegs::ID_A;
        std::array<uint8_t, 3> id_buf{};
        hal::I2cResult id_res = co_await i2c_.transfer_async(
            slave_addr_,
            std::span<const uint8_t>(&id_reg, 1),
            std::span<uint8_t>(id_buf.data(), id_buf.size())
        );

        if (id_res.status != hal::I2cStatus::Ok || id_buf[0] != 'H' || id_buf[1] != '4' || id_buf[2] != '3') {
            co_return false;
        }

        // CRA: 8-average, 75 Hz (0x78) or 15 Hz (0x70)
        co_await i2c_.write_reg_async(slave_addr_, HmcRegs::CONFIG_A, 0x78);

        // CRB: +-1.3 Gauss range (0x20) -> 1090 LSB/Gauss
        co_await i2c_.write_reg_async(slave_addr_, HmcRegs::CONFIG_B, 0x20);

        // Mode: Continuous measurement (0x00)
        co_await i2c_.write_reg_async(slave_addr_, HmcRegs::MODE, 0x00);

        initialized_ = true;
        co_return true;
    }

    /*
     * Asynchronously read 6-byte magnetic vector (X, Z, Y order in HMC5883L)
     */
    coro::Task<MagSample> read_sample_async() override {
        MagSample s{};
        if (!initialized_) {
            co_return s;
        }

        uint8_t reg_addr = HmcRegs::DATA_X_MSB;
        std::array<uint8_t, 6> raw_buf{};

        hal::I2cResult res = co_await i2c_.transfer_async(
            slave_addr_,
            std::span<const uint8_t>(&reg_addr, 1),
            std::span<uint8_t>(raw_buf.data(), raw_buf.size())
        );

        if (res.status == hal::I2cStatus::Ok) {
            // Note: HMC5883L register order is X, Z, Y
            int16_t raw_x = static_cast<int16_t>((raw_buf[0] << 8) | raw_buf[1]);
            int16_t raw_z = static_cast<int16_t>((raw_buf[2] << 8) | raw_buf[3]);
            int16_t raw_y = static_cast<int16_t>((raw_buf[4] << 8) | raw_buf[5]);

            // ±1.3 Gauss range -> 1090 LSB/Gauss -> ~0.9174 mG/LSB
            constexpr float SCALE_MGAUSS = 1000.0f / 1090.0f;
            s.mag_mgauss[0] = static_cast<float>(raw_x) * SCALE_MGAUSS;
            s.mag_mgauss[1] = static_cast<float>(raw_y) * SCALE_MGAUSS;
            s.mag_mgauss[2] = static_cast<float>(raw_z) * SCALE_MGAUSS;

            float angle_rad = std::atan2(s.mag_mgauss[1], s.mag_mgauss[0]);
            float heading = angle_rad * (180.0f / 3.14159265358979323846f);
            if (heading < 0.0f) heading += 360.0f;
            s.heading_deg = heading;

            s.timestamp_us = res.timestamp_us;
            s.valid = true;
        }

        co_return s;
    }

    static Tlp64 to_tlp(const MagSample& s, uint32_t seq = 0) noexcept {
        (void)seq;
        Tlp64 tlp{};
        tlp.wire.channel = static_cast<uint8_t>(Channel::Telemetry);
        tlp.wire.tag = TLP_TAG_MAG_HMC;
        tlp.wire.type = 0x01;
        tlp.wire.timestamp_ns = s.timestamp_us * 1000ULL;

        int16_t mx = static_cast<int16_t>(s.mag_mgauss[0]);
        int16_t my = static_cast<int16_t>(s.mag_mgauss[1]);
        int16_t mz = static_cast<int16_t>(s.mag_mgauss[2]);
        uint16_t hdg = static_cast<uint16_t>(s.heading_deg * 10.0f);

        std::memcpy(&tlp.wire.payload[0], &mx, sizeof(mx));
        std::memcpy(&tlp.wire.payload[2], &my, sizeof(my));
        std::memcpy(&tlp.wire.payload[4], &mz, sizeof(mz));
        std::memcpy(&tlp.wire.payload[6], &hdg, sizeof(hdg));

        return tlp;
    }

private:
    hal::II2c& i2c_;
    uint8_t    slave_addr_{DEFAULT_HMC_I2C_ADDR};
    bool       initialized_{false};
};

} // namespace applib::drivers::mag

namespace abstractx::drivers::mag {
    using applib::drivers::mag::Hmc5883l;
    using applib::drivers::mag::TLP_TAG_MAG_HMC;
    using applib::drivers::mag::DEFAULT_HMC_I2C_ADDR;
    namespace HmcRegs = applib::drivers::mag::HmcRegs;
} // namespace abstractx::drivers::mag

#endif // APPLIB_DRIVERS_MAG_HMC5883L_HPP
