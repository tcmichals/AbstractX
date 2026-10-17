/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX SITL CppUTest: ICM-42688-P 8 kHz IMU Driver Unit Tests
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

// Temporarily disable CppUTest memory macros around ETL includes
#undef new
#undef delete
#include "abstractx/drivers/imu/icm42688p.hpp"
#include "abstractx/hal/spi.hpp"
#include "CppUTest/MemoryLeakDetectorNewMacros.h"

using namespace abstractx;
using namespace abstractx::drivers::imu;

// @verifies [SPEC-HAL-02] [SPEC-IMU-01] [SPEC-IMU-02] include/abstractx/drivers/imu/icm42688p.hpp
class MockSpi : public hal::ISpi {
public:
    bool init(const hal::SpiConfig& config) override {
        mock().actualCall("init")
              .withParameter("frequency_hz", config.frequency_hz)
              .withParameter("mode", static_cast<int>(config.mode));
        return true;
    }

    void select(bool active) override {
        mock().actualCall("select").withParameter("active", active);
    }


    void start_hardware_transfer_from_isr(const hal::SpiRequest& req) noexcept override {
        mock().actualCall("start_hardware_transfer_from_isr");
        hal::SpiResult res{};
        res.status = hal::SpiStatus::Ok;
        res.transferred_bytes = req.tx_data.size();
        res.timestamp_us = 12345678ULL;

        if (!req.rx_data.empty() && !req.tx_data.empty()) {
            uint8_t reg = req.tx_data[0];
            if ((reg & 0x7F) == IcmRegs::WHO_AM_I) {
                if (req.rx_data.size() > 1) req.rx_data[1] = IcmRegs::WHO_AM_I_VAL;
                else req.rx_data[0] = IcmRegs::WHO_AM_I_VAL;
            } else if ((reg & 0x7F) == IcmRegs::TEMP_DATA1) {
                // Synthesize 15-byte IMU sample
                // [0: Dummy] [1..2: Temp] [3..8: Accel] [9..14: Gyro]
                if (req.rx_data.size() >= 15) {
                    req.rx_data[0] = 0x00;
                    req.rx_data[1] = 0x00; req.rx_data[2] = 0x00; // Temp ~ 25 C
                    // Accel X=0, Y=0, Z=2048 (1.0g)
                    req.rx_data[3] = 0x00; req.rx_data[4] = 0x00;
                    req.rx_data[5] = 0x00; req.rx_data[6] = 0x00;
                    req.rx_data[7] = 0x08; req.rx_data[8] = 0x00; // 0x0800 = 2048 -> 1.0g
                    // Gyro X=164 (10 dps), Y=0, Z=0
                    req.rx_data[9] = 0x00; req.rx_data[10] = 0xA4; // 164 -> ~10 dps
                    req.rx_data[11] = 0x00; req.rx_data[12] = 0x00;
                    req.rx_data[13] = 0x00; req.rx_data[14] = 0x00;
                }
            }
        }

        push_completion_from_isr(req, res);
        set_hardware_idle_from_isr();
    }
};

// Start a task and pump the dispatcher, as abstractx::step() does. Resuming the
// outer task directly while it awaits a child would re-enter it before the
// child has produced a result.
template <typename TTask>
static bool run_to_completion(TTask& task) {
    task.resume();
    for (int spins = 0; spins < 1000 && !task.done(); ++spins) {
        Dispatcher::process();
    }
    return task.done();
}

TEST_GROUP(SitlImuTestGroup) {
    MockSpi* spi{nullptr};
    Icm42688p* imu{nullptr};

    void setup() {
        spi = new MockSpi();
        imu = new Icm42688p(*spi);
        mock().clear();
        Dispatcher::reset();
    }

    void teardown() {
        mock().checkExpectations();
        mock().clear();
        delete imu;
        delete spi;
    }
};

TEST(SitlImuTestGroup, VerifyRawBufferParsingCalculations) {
    // 15-byte test buffer: Temp=25C, Accel Z = 1.0g (2048 LSB), Gyro X = 10 dps (164 LSB)
    uint8_t raw[15] = {
        0x00,
        0x00, 0x00,             // Temp = 0 -> 25.0 C
        0x00, 0x00,             // Accel X = 0
        0x00, 0x00,             // Accel Y = 0
        0x08, 0x00,             // Accel Z = 2048 -> 1.0g
        0x00, 0xA4,             // Gyro X = 164 -> 10.0 dps
        0x00, 0x00,             // Gyro Y = 0
        0x00, 0x00              // Gyro Z = 0
    };

    ImuSample sample = Icm42688p::parse_raw_buffer(raw, 5000000ULL);

    CHECK_TRUE(sample.valid);
    LONGS_EQUAL(5000000ULL, sample.timestamp_us);
    DOUBLES_EQUAL(25.0, sample.temp_deg_c, 0.1);
    DOUBLES_EQUAL(0.0, sample.accel_g[0], 0.01);
    DOUBLES_EQUAL(0.0, sample.accel_g[1], 0.01);
    DOUBLES_EQUAL(1.0, sample.accel_g[2], 0.01);
    DOUBLES_EQUAL(10.0, sample.gyro_dps[0], 0.1);
    DOUBLES_EQUAL(0.0, sample.gyro_dps[1], 0.1);
    DOUBLES_EQUAL(0.0, sample.gyro_dps[2], 0.1);
}

TEST(SitlImuTestGroup, VerifyTlpEncapsulation) {
    ImuSample sample;
    sample.timestamp_us = 1000000ULL;
    sample.accel_g[0] = 0.5f;
    sample.accel_g[1] = -0.5f;
    sample.accel_g[2] = 1.0f;
    sample.gyro_dps[0] = 15.0f;
    sample.gyro_dps[1] = 0.0f;
    sample.gyro_dps[2] = -25.0f;
    sample.temp_deg_c = 32.5f;
    sample.valid = true;

    Tlp64 tlp = Icm42688p::to_tlp(sample, 42);

        CHECK_EQUAL(static_cast<uint8_t>(Channel::Telemetry), static_cast<uint8_t>(tlp.channel()));
    CHECK_EQUAL(TLP_TAG_IMU, tlp.tag());
}

TEST(SitlImuTestGroup, VerifyAsyncInitSequenceWithMocks) {
    mock().expectOneCall("init")
          .withParameter("frequency_hz", 24000000)
          .withParameter("mode", static_cast<int>(hal::SpiMode::Mode0));

    // Async register read for WHO_AM_I
    mock().expectOneCall("start_hardware_transfer_from_isr");
    // Async register write for PWR_MGMT0
    mock().expectOneCall("start_hardware_transfer_from_isr");
    // Async register write for GYRO_CONFIG0
    mock().expectOneCall("start_hardware_transfer_from_isr");
    // Async register write for ACCEL_CONFIG0
    mock().expectOneCall("start_hardware_transfer_from_isr");

    auto task = imu->init_async();
    CHECK_TRUE(run_to_completion(task));
    CHECK_TRUE(task.get_result());
    LONGS_EQUAL(0, Dispatcher::dropped_resumes());
}

// Repeated register traffic must recycle coroutine frames rather than
// permanently consuming the static frame pool.
TEST(SitlImuTestGroup, RepeatedAsyncInitDoesNotGrowFramePool) {
    mock().ignoreOtherCalls();

    {
        auto first = imu->init_async();
        CHECK_TRUE(run_to_completion(first));
    }
    const size_t high_water = coro::coro_pool_high_water();
    const size_t live = coro::coro_pool_used();

    for (int i = 0; i < 1000; ++i) {
        auto task = imu->init_async();
        CHECK_TRUE(run_to_completion(task));
        CHECK_TRUE(task.get_result());
    }

    LONGS_EQUAL(high_water, coro::coro_pool_high_water());
    LONGS_EQUAL(live, coro::coro_pool_used());
}
