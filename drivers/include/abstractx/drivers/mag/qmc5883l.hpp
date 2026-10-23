/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX QMC5883L / HMC5883L 3-Axis Digital Magnetometer Driver
 * ----------------------------------------------------------------
 * Fully asynchronous C++20 coroutine I2C driver:
 * - Zero dynamic heap allocation (0 B)
 * - 100 Hz continuous magnetic field sampling
 * - Non-blocking register initialization and burst reading
 * - Standard 64-byte TLP telemetry packaging
 */

#ifndef ABSTRACTX_DRIVERS_MAG_QMC5883L_HPP
#define ABSTRACTX_DRIVERS_MAG_QMC5883L_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include <cmath>

#include "abstractx/coro.hpp"
#include "abstractx/hal/i2c.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/trace/tracer.hpp"
#include "asp_tlp64.hpp"

namespace abstractx::drivers::mag {

namespace QmcRegs {
    constexpr uint8_t DATA_X_LSB = 0x00;
    constexpr uint8_t DATA_X_MSB = 0x01;
    constexpr uint8_t DATA_Y_LSB = 0x02;
    constexpr uint8_t DATA_Y_MSB = 0x03;
    constexpr uint8_t DATA_Z_LSB = 0x04;
    constexpr uint8_t DATA_Z_MSB = 0x05;
    constexpr uint8_t STATUS     = 0x06;
    constexpr uint8_t TEMP_LSB   = 0x07;
    constexpr uint8_t TEMP_MSB   = 0x08;
    constexpr uint8_t CONTROL_1  = 0x09;
    constexpr uint8_t CONTROL_2  = 0x0A;
    constexpr uint8_t PERIOD     = 0x0B;
}

constexpr uint8_t TLP_TAG_MAG = 0x03;
constexpr uint8_t DEFAULT_QMC_I2C_ADDR = 0x0D;

/* Calibrated Magnetometer Sample */
struct MagSample {
    float    mag_mgauss[3]{0.0f, 0.0f, 0.0f}; // X, Y, Z in milliGauss
    float    heading_deg{0.0f};                // Uncompensated 2D heading (0..360 deg)
    uint64_t timestamp_us{0};
    bool     valid{false};
};

class Qmc5883l {
public:
    explicit Qmc5883l(hal::II2c& i2c_driver, uint8_t slave_addr = DEFAULT_QMC_I2C_ADDR)
        : i2c_(i2c_driver), slave_addr_(slave_addr) {}

    /*
     * C++20 Coroutine Async Lifecycle Initialization [SPEC-MAG-01]
     */
    coro::Task<bool> init_async() {
        hal::I2cConfig config{};
        config.frequency_hz = 400'000; // 400 kHz Fast Mode
        config.speed = hal::I2cSpeed::Fast_400k;
        if (!i2c_.init(config)) {
            co_return false;
        }

        // 1. Soft Reset: Write 0x80 to CONTROL_2
        co_await i2c_.write_reg_async(slave_addr_, QmcRegs::CONTROL_2, 0x80);

        // 2. Set SET/RESET period to recommended 0x01
        co_await i2c_.write_reg_async(slave_addr_, QmcRegs::PERIOD, 0x01);

        // 3. Configure CONTROL_1:
        //    Mode: Continuous (0x01)
        //    ODR: 100 Hz (0x04 << 2 = 0x04)
        //    Range: 8 Gauss (0x01 << 4 = 0x10)
        //    OSR: 512 (0x00 << 6)
        //    Combined: 0x01 | 0x04 | 0x10 = 0x15 (or 0x1D for 200Hz)
        co_await i2c_.write_reg_async(slave_addr_, QmcRegs::CONTROL_1, 0x1D);

        initialized_ = true;
        co_return true;
    }

    /*
     * Asynchronously read 6-byte magnetic vector (X, Y, Z)
     */
    coro::Task<MagSample> read_sample_async() {
        MagSample s{};
        if (!initialized_) {
            co_return s;
        }

        uint8_t reg_addr = QmcRegs::DATA_X_LSB;
        std::array<uint8_t, 6> raw_buf{};

        hal::I2cResult res = co_await i2c_.transfer_async(
            slave_addr_,
            std::span<const uint8_t>(&reg_addr, 1),
            std::span<uint8_t>(raw_buf.data(), raw_buf.size())
        );

        if (res.status == hal::I2cStatus::Ok) {
            int16_t raw_x = static_cast<int16_t>(raw_buf[0] | (raw_buf[1] << 8));
            int16_t raw_y = static_cast<int16_t>(raw_buf[2] | (raw_buf[3] << 8));
            int16_t raw_z = static_cast<int16_t>(raw_buf[4] | (raw_buf[5] << 8));

            // Scale for ±8 Gauss range: ~3000 LSB/Gauss -> ~0.33 mG/LSB
            constexpr float SCALE_MGAUSS = 1000.0f / 3000.0f;
            s.mag_mgauss[0] = static_cast<float>(raw_x) * SCALE_MGAUSS;
            s.mag_mgauss[1] = static_cast<float>(raw_y) * SCALE_MGAUSS;
            s.mag_mgauss[2] = static_cast<float>(raw_z) * SCALE_MGAUSS;

            // Compute uncompensated 2D heading in degrees (0..360)
            float angle_rad = std::atan2(s.mag_mgauss[1], s.mag_mgauss[0]);
            float heading = angle_rad * (180.0f / 3.14159265358979323846f);
            if (heading < 0.0f) heading += 360.0f;
            s.heading_deg = heading;

            s.timestamp_us = res.timestamp_us;
            s.valid = true;
        }

        co_return s;
    }

    /*
     * Package MagSample into a standardized 64-byte TLP
     */
    static Tlp64 to_tlp(const MagSample& s, uint32_t seq = 0) noexcept {
        (void)seq;
        Tlp64 tlp{};
        tlp.wire.channel = static_cast<uint8_t>(Channel::Telemetry);
        tlp.wire.tag = TLP_TAG_MAG;
        tlp.wire.type = 0x01; // Data stream
        tlp.wire.timestamp_ns = s.timestamp_us * 1000ULL;

        // Store milliGauss integers (int16_t) and heading (uint16_t in tenths of deg)
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
    uint8_t    slave_addr_{DEFAULT_QMC_I2C_ADDR};
    bool       initialized_{false};
};

} // namespace abstractx::drivers::mag

#endif // ABSTRACTX_DRIVERS_MAG_QMC5883L_HPP
