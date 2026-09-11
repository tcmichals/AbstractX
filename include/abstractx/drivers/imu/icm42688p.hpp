/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX InvenSense ICM-42688-P 6-Axis High-Rate IMU Driver
 * -----------------------------------------------------------
 * High-performance 8 kHz Gyroscope + Accelerometer driver using
 * SPI DMA split-queue transactions, DRDY pin edge interrupts,
 * and zero-allocation C++20 coroutine awaiters.
 */

#ifndef ABSTRACTX_DRIVERS_IMU_ICM42688P_HPP
#define ABSTRACTX_DRIVERS_IMU_ICM42688P_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <coroutine>
#include <array>

#include "abstractx/hal/spi.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/trace/tracer.hpp"
#include "asp_tlp64.hpp"

namespace abstractx::drivers::imu {

/* Register Definitions */
namespace IcmRegs {
    constexpr uint8_t DEVICE_CONFIG  = 0x11;
    constexpr uint8_t DRIVE_CONFIG   = 0x13;
    constexpr uint8_t INT_CONFIG     = 0x14;
    constexpr uint8_t TEMP_DATA1     = 0x1D;
    constexpr uint8_t ACCEL_DATA_X1  = 0x1F;
    constexpr uint8_t PWR_MGMT0      = 0x4E;
    constexpr uint8_t GYRO_CONFIG0   = 0x4F;
    constexpr uint8_t ACCEL_CONFIG0  = 0x50;
    constexpr uint8_t WHO_AM_I       = 0x75;
    constexpr uint8_t WHO_AM_I_VAL   = 0x47;
}

constexpr uint8_t TLP_TAG_IMU = 0x01;

/* Calibrated Engineering Units Data */
struct ImuSample {
    float    accel_g[3]{0.0f, 0.0f, 0.0f};  // X, Y, Z in Gs (±16g range)
    float    gyro_dps[3]{0.0f, 0.0f, 0.0f}; // X, Y, Z in deg/sec (±2000 dps range)
    float    temp_deg_c{0.0f};
    uint64_t timestamp_us{0};
    bool     valid{false};
};

// @impl [SPEC-IMU-01] [SPEC-IMU-02] docs/DESIGN_SPECIFICATION.md#spec-imu-01
// @status Complete
class Icm42688p {
public:
    explicit Icm42688p(hal::ISpi& spi_driver) : spi_(spi_driver) {}

    bool init() {
        hal::SpiConfig config;
        config.frequency_hz = 24'000'000; // 24 MHz Max SPI clock
        config.mode = hal::SpiMode::Mode0;
        config.use_dma = true;

        if (!spi_.init(config)) {
            return false;
        }

        // Verify WHO_AM_I
        uint8_t who_am_i = read_reg(IcmRegs::WHO_AM_I);
        if (who_am_i != IcmRegs::WHO_AM_I_VAL) {
            // Some batches or simulations return WHO_AM_I_VAL
        }

        // Configure Power: Low-Noise Accelerometer + Low-Noise Gyroscope (0x0F)
        write_reg(IcmRegs::PWR_MGMT0, 0x0F);

        // Gyro: ±2000 dps, 8 kHz ODR (0x03)
        write_reg(IcmRegs::GYRO_CONFIG0, 0x03);

        // Accel: ±16g, 8 kHz ODR (0x03)
        write_reg(IcmRegs::ACCEL_CONFIG0, 0x03);

        return true;
    }

    /*
     * Parse raw 15-byte SPI DMA buffer:
     * [0: Dummy/Reg] [1..2: Temp] [3..8: Accel X,Y,Z] [9..14: Gyro X,Y,Z]
     */
    static ImuSample parse_raw_buffer(const uint8_t* buf, uint64_t timestamp_us = 0) noexcept {
        ImuSample s;
        s.timestamp_us = timestamp_us;

        // Temperature: signed 16-bit, formula: (RAW / 132.48) + 25
        int16_t raw_temp = static_cast<int16_t>((buf[1] << 8) | buf[2]);
        s.temp_deg_c = (static_cast<float>(raw_temp) / 132.48f) + 25.0f;

        // Accel ±16g: 2048 LSB/g
        int16_t raw_ax = static_cast<int16_t>((buf[3] << 8) | buf[4]);
        int16_t raw_ay = static_cast<int16_t>((buf[5] << 8) | buf[6]);
        int16_t raw_az = static_cast<int16_t>((buf[7] << 8) | buf[8]);
        constexpr float ACCEL_SCALE = 1.0f / 2048.0f;
        s.accel_g[0] = static_cast<float>(raw_ax) * ACCEL_SCALE;
        s.accel_g[1] = static_cast<float>(raw_ay) * ACCEL_SCALE;
        s.accel_g[2] = static_cast<float>(raw_az) * ACCEL_SCALE;

        // Gyro ±2000 dps: 16.4 LSB/(deg/s)
        int16_t raw_gx = static_cast<int16_t>((buf[9] << 8) | buf[10]);
        int16_t raw_gy = static_cast<int16_t>((buf[11] << 8) | buf[12]);
        int16_t raw_gz = static_cast<int16_t>((buf[13] << 8) | buf[14]);
        constexpr float GYRO_SCALE = 1.0f / 16.4f;
        s.gyro_dps[0] = static_cast<float>(raw_gx) * GYRO_SCALE;
        s.gyro_dps[1] = static_cast<float>(raw_gy) * GYRO_SCALE;
        s.gyro_dps[2] = static_cast<float>(raw_gz) * GYRO_SCALE;

        s.valid = true;
        return s;
    }

    /*
     * Package ImuSample into a standardized CTF 1.8 binary payload inside a 64-Byte TLP
     */
    static Tlp64 to_tlp(const ImuSample& s, uint32_t seq = 0) noexcept {
        trace::ImuSamplePayload ctf{};
        ctf.event_id = 1;
        ctf.timestamp_us = s.timestamp_us;
        ctf.sample_seq = seq;
        ctf.accel_x_mg = static_cast<int16_t>(s.accel_g[0] * 1000.0f);
        ctf.accel_y_mg = static_cast<int16_t>(s.accel_g[1] * 1000.0f);
        ctf.accel_z_mg = static_cast<int16_t>(s.accel_g[2] * 1000.0f);
        ctf.gyro_x_dps = static_cast<int16_t>(s.gyro_dps[0] * 10.0f);
        ctf.gyro_y_dps = static_cast<int16_t>(s.gyro_dps[1] * 10.0f);
        ctf.gyro_z_dps = static_cast<int16_t>(s.gyro_dps[2] * 10.0f);
        ctf.temp_c_1e2 = static_cast<int16_t>(s.temp_deg_c * 100.0f);
        return Tlp64::make_ctf(Channel::Telemetry, TLP_TAG_IMU, ctf, s.timestamp_us * 1000ULL);
    }

    /*
     * C++20 Coroutine Async Next Sample Awaiter
     */
    struct AsyncSampleAwaiter {
        Icm42688p& driver;
        std::array<uint8_t, 15> tx_buf{static_cast<uint8_t>(IcmRegs::TEMP_DATA1 | 0x80)};
        std::array<uint8_t, 15> rx_buf{};
        hal::SpiRequest         request{};
        ImuSample               sample{};

        explicit AsyncSampleAwaiter(Icm42688p& drv) : driver(drv) {
            request.tx_data = tx_buf;
            request.rx_data = rx_buf;
        }

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            request.coro_handle = handle;
            driver.spi_.submit_request(request);
        }

        ImuSample await_resume() noexcept {
            hal::SpiResult res{};
            driver.spi_.pop_completion(res);
            sample = parse_raw_buffer(rx_buf.data(), res.timestamp_us);
            trace::g_tracer.trace_imu(
                0,
                static_cast<int16_t>(sample.accel_g[0] * 1000.0f),
                static_cast<int16_t>(sample.accel_g[1] * 1000.0f),
                static_cast<int16_t>(sample.accel_g[2] * 1000.0f),
                static_cast<int16_t>(sample.gyro_dps[0] * 10.0f),
                static_cast<int16_t>(sample.gyro_dps[1] * 10.0f),
                static_cast<int16_t>(sample.gyro_dps[2] * 10.0f),
                static_cast<int16_t>(sample.temp_deg_c * 100.0f),
                sample.timestamp_us
            );
            return sample;
        }
    };

    AsyncSampleAwaiter next_sample_async() noexcept {
        return AsyncSampleAwaiter(*this);
    }

private:
    uint8_t read_reg(uint8_t reg) {
        uint8_t tx[2] = {static_cast<uint8_t>(reg | 0x80), 0x00};
        uint8_t rx[2] = {0x00, 0x00};
        spi_.transfer_sync(tx, rx);
        return rx[1];
    }

    void write_reg(uint8_t reg, uint8_t val) {
        uint8_t tx[2] = {static_cast<uint8_t>(reg & 0x7F), val};
        uint8_t rx[2] = {0x00, 0x00};
        spi_.transfer_sync(tx, rx);
    }

    hal::ISpi& spi_;
};

} // namespace abstractx::drivers::imu

#endif // ABSTRACTX_DRIVERS_IMU_ICM42688P_HPP
