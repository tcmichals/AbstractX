/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX SITL CppUTest: Sensor Fusion & AHRS Filter Unit Tests
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#undef new
#undef delete
#include "abstractx/fusion/attitude_filter.hpp"
#include "CppUTest/MemoryLeakDetectorNewMacros.h"

using namespace abstractx;
using namespace abstractx::fusion;
using namespace abstractx::drivers::imu;
using namespace abstractx::drivers::gps;

TEST_GROUP(SitlFusionTestGroup) {
    AttitudeFilter* filter{nullptr};

    void setup() {
        filter = new AttitudeFilter(2.0f, 0.005f);
        mock().clear();
    }

    void teardown() {
        mock().checkExpectations();
        mock().clear();
        delete filter;
    }
};

TEST(SitlFusionTestGroup, VerifyIdentityAttitudeOnLevelImu) {
    ImuSample imu{};
    imu.accel_g[0] = 0.0f;
    imu.accel_g[1] = 0.0f;
    imu.accel_g[2] = 1.0f; // Level 1g gravity vector
    imu.gyro_dps[0] = 0.0f;
    imu.gyro_dps[1] = 0.0f;
    imu.gyro_dps[2] = 0.0f;
    imu.valid = true;

    for (int i = 0; i < 100; ++i) {
        filter->update_imu(imu, 0.001f);
    }

    const auto& s = filter->state();
    DOUBLES_EQUAL(0.0, s.roll_deg, 1.0);
    DOUBLES_EQUAL(0.0, s.pitch_deg, 1.0);
    LONGS_EQUAL(100, s.imu_updates);
}

TEST(SitlFusionTestGroup, VerifyGpsUpdateIntegration) {
    GpsFix gps{};
    gps.alt_msl_mm = 250000;         // 250 m
    gps.ground_speed_mm_s = 18500;   // 18.5 m/s
    gps.heading_1e5 = 9000000;       // 90 deg
    gps.satellites = 16;
    gps.valid = true;

    filter->update_gps(gps);

    const auto& s = filter->state();
    DOUBLES_EQUAL(250.0, s.altitude_m, 0.1);
    DOUBLES_EQUAL(18.5, s.ground_speed_m_s, 0.1);
    LONGS_EQUAL(1, s.gps_updates);
}

TEST(SitlFusionTestGroup, VerifyAhrsTlpEncapsulation) {
    FlightAttitude att{};
    att.roll_deg = 12.5f;
    att.pitch_deg = -5.0f;
    att.yaw_deg = 180.0f;
    att.altitude_m = 100.0f;
    att.ground_speed_m_s = 15.0f;
    att.motors.m1 = 550;
    att.motors.m2 = 560;
    att.motors.m3 = 540;
    att.motors.m4 = 550;
    att.timestamp_us = 9999999ULL;

    Tlp64 tlp = AttitudeFilter::to_tlp(att);

        CHECK_EQUAL(static_cast<uint8_t>(Channel::Telemetry), static_cast<uint8_t>(tlp.channel()));
    CHECK_EQUAL(TLP_TAG_FUSED_AHRS, tlp.tag());
}
