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

#ifndef APPLIB_DRIVERS_IMU_ICM42688P_HPP
#define APPLIB_DRIVERS_IMU_ICM42688P_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <coroutine>
#include <array>

#include "abstractx/coro.hpp"
#include "abstractx/hal/spi.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/trace/tracer.hpp"
#include "abstractx/sensor/sensor_interface.hpp"
#include "asp_tlp64.hpp"

namespace applib::drivers::imu {

namespace hal = abstractx::hal;
namespace coro = abstractx::coro;
namespace trace = abstractx::trace;
using abstractx::Tlp64;
using abstractx::Channel;
using abstractx::IsrDispatcher;
using abstractx::sensor::IImuDriver;
using abstractx::sensor::imu::ImuSample;

/* Register Definitions */
namespace IcmRegs {
    constexpr uint8_t DEVICE_CONFIG  = 0x11;
    constexpr uint8_t DRIVE_CONFIG   = 0x13;
    constexpr uint8_t INT_CONFIG     = 0x14;
    constexpr uint8_t TEMP_DATA1     = 0x1D;
    constexpr uint8_t ACCEL_DATA_X1  = 0x1F;
    constexpr uint8_t INT_STATUS     = 0x2D;
    constexpr uint8_t PWR_MGMT0      = 0x4E;
    constexpr uint8_t GYRO_CONFIG0   = 0x4F;
    constexpr uint8_t ACCEL_CONFIG0  = 0x50;
    constexpr uint8_t INT_CONFIG0    = 0x63;
    constexpr uint8_t INT_CONFIG1    = 0x64;
    constexpr uint8_t INT_SOURCE0    = 0x65;
    constexpr uint8_t WHO_AM_I       = 0x75;
    constexpr uint8_t WHO_AM_I_VAL   = 0x47;

    // GYRO_CONFIG0 / ACCEL_CONFIG0 bits [3:0]
    constexpr uint8_t ODR_8KHZ       = 0x03;
    constexpr uint8_t ODR_1KHZ       = 0x06;
}

// @impl [SPEC-IMU-01] docs/DESIGN_SPECIFICATION.md#spec-imu-01
struct Icm42688pConfig {
    uint32_t spi_hz{24'000'000};
    uint8_t  odr{IcmRegs::ODR_8KHZ};   // same rate for gyro and accel
};

constexpr uint8_t TLP_TAG_IMU = 0x01;

// @impl [SPEC-IMU-01] [SPEC-IMU-02] docs/DESIGN_SPECIFICATION.md#spec-imu-01
// @status Complete
class Icm42688p : public IImuDriver {
public:
    explicit Icm42688p(hal::ISpi& spi_driver, Icm42688pConfig cfg = {}) : spi_(spi_driver), cfg_(cfg) {}

    /*
     * C++20 Coroutine Async Register Read (Zero Heap, Non-Blocking)
     */
    coro::Task<uint8_t> read_reg_async(uint8_t reg) {
        co_return co_await spi_.read_reg_async(reg, 0x80);
    }

    /*
     * C++20 Coroutine Async Register Write (Zero Heap, Non-Blocking)
     */
    coro::Task<bool> write_reg_async(uint8_t reg, uint8_t val) {
        co_await spi_.write_reg_async(reg, val, 0x7F);
        co_return true;
    }

    /*
     * C++20 Coroutine Async Lifecycle Initialization [SPEC-IMU-01]
     */
    // Fails on a wrong WHO_AM_I or on any register that does not read back as written.
    coro::Task<bool> init_async() override {
        hal::SpiConfig config;
        config.frequency_hz = cfg_.spi_hz;
        config.mode = hal::SpiMode::Mode0;
        config.use_dma = true;

        if (!spi_.init(config)) {
            co_return false;
        }

        if (co_await read_reg_async(IcmRegs::WHO_AM_I) != IcmRegs::WHO_AM_I_VAL) {
            co_return false;
        }

        // DRDY on INT1, cleared by the data read ([SPEC-IMU-01]); power last (200 us rule).
        const uint8_t odr = static_cast<uint8_t>(cfg_.odr & 0x0F);
        const std::array<std::array<uint8_t, 2>, 7> writes{{
            {IcmRegs::INT_CONFIG,    0x03},   // INT1 pulsed, push-pull, active high
            {IcmRegs::INT_CONFIG0,   0x20},   // UI_DRDY cleared on sensor register read
            {IcmRegs::INT_CONFIG1,   0x00},   // 100 us pulse, INT_ASYNC_RESET = 0
            {IcmRegs::INT_SOURCE0,   0x08},   // UI_DRDY -> INT1
            {IcmRegs::GYRO_CONFIG0,  odr},    // +-2000 dps
            {IcmRegs::ACCEL_CONFIG0, odr},    // +-16 g
            {IcmRegs::PWR_MGMT0,     0x0F},   // gyro + accel low noise
        }};
        for (const auto& w : writes) {
            co_await write_reg_async(w[0], w[1]);
            if (co_await read_reg_async(w[0]) != w[1]) {
                co_return false;
            }
        }
        co_return true;
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
        hal::SpiResult          result{};
        ImuSample               sample{};

        explicit AsyncSampleAwaiter(Icm42688p& drv) : driver(drv) {}

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            // Spans are bound here, not in the constructor: the awaiter may be
            // moved into the coroutine frame after construction.
            request.tx_data = tx_buf;
            request.rx_data = rx_buf;
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.spi_.submit_request(request)) {
                result.status = hal::SpiStatus::QueueFull;
                return false;
            }
            return true;
        }

        ImuSample await_resume() noexcept {
            if (result.status != hal::SpiStatus::Ok) {
                return sample; // valid == false
            }
            sample = parse_raw_buffer(rx_buf.data(), result.timestamp_us);
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

    AsyncSampleAwaiter sample_awaiter() noexcept {
        return AsyncSampleAwaiter(*this);
    }

    /*
     * Polymorphic C++20 Coroutine Async Next Sample Ingestion (IImuDriver)
     */
    coro::Task<ImuSample> next_sample_async() override {
        co_return co_await sample_awaiter();
    }

private:
    hal::ISpi& spi_;
    Icm42688pConfig cfg_;
};

} // namespace applib::drivers::imu

namespace abstractx::drivers::imu {
    using applib::drivers::imu::Icm42688p;
    using applib::drivers::imu::TLP_TAG_IMU;
    using applib::drivers::imu::Icm42688pConfig;
    namespace IcmRegs = applib::drivers::imu::IcmRegs;
} // namespace abstractx::drivers::imu

#endif // APPLIB_DRIVERS_IMU_ICM42688P_HPP
