/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Heterogeneous Sensor Benchmark & Testing Tasks (gps_imu_app)
 * ---------------------------------------------------------------------
 * Extracted coroutine task pipeline for gps_imu_app, reusable by
 * targets (Pico 2 W, E906, Linux) and sim/host tests.
 */

#ifndef APPS_GPS_IMU_APP_GPS_IMU_TASKS_HPP
#define APPS_GPS_IMU_APP_GPS_IMU_TASKS_HPP

#include "abstractx/abstractx.hpp"
#include "applib/drivers/imu/icm42688p.hpp"
#include "applib/drivers/gps/ublox_gps.hpp"
#include "applib/drivers/mag/qmc5883l.hpp"
#include "attitude_filter.hpp"
#include <cstdio>
#include <cstring>

using namespace abstractx;
using namespace abstractx::coro;
using namespace applib::drivers::imu;
using namespace applib::drivers::gps;
using namespace applib::drivers::mag;
using namespace abstractx::fusion;

// Lock-free static TLP rings for inter-domain communication
// @impl [SPEC-TLP-01] [SPEC-TLP-03] [SPEC-ARCH-02] [SPEC-APP-07] [SPEC-E906-MEM-01] [SPEC-E906-IPC-04] docs/DESIGN_SPECIFICATION.md#spec-tlp-01 apps/gps_imu_app/SPECIFICATION.md#spec-app-07 targets/allwinner_e906/SPECIFICATION.md#spec-e906-mem-01
inline MpscTlpRing<> g_tx_ring;        // Flight Controller / Host RPMsg -> I/O Processor (Requests / DMA_CFG)
inline MpscTlpRing<> g_sensor_ring;    // I/O Processor -> Flight Controller (Completions & Auto-DMA Stream TLPs)
inline MpscTlpRing<> g_telemetry_ring; // Flight Controller -> Egress / RPMsg / Network (:9870)

// Lock-free asynchronous coroutine channels for multi-rate sensor streaming
// @impl [SPEC-APP-02] [SPEC-APP-03] [SPEC-ARCH-02] apps/gps_imu_app/SPECIFICATION.md#spec-app-02
inline AsyncQueue<ImuSample, 32> g_imu_channel; // High rate (1 kHz - 8 kHz)
inline AsyncQueue<MagSample, 16> g_mag_channel; // Medium rate (50 Hz - 100 Hz)
inline AsyncQueue<GpsFix, 8>     g_gps_channel; // Low rate (5 Hz - 10 Hz)

namespace detail_studio_tlp {

inline void pack_be16(uint8_t* dst, int16_t v) noexcept {
    const auto u = static_cast<uint16_t>(v);
    dst[0] = static_cast<uint8_t>(u >> 8);
    dst[1] = static_cast<uint8_t>(u & 0xFFU);
}

inline void pack_le16(uint8_t* dst, int16_t v) noexcept {
    const auto u = static_cast<uint16_t>(v);
    dst[0] = static_cast<uint8_t>(u & 0xFFU);
    dst[1] = static_cast<uint8_t>(u >> 8);
}

inline void pack_le32(uint8_t* dst, int32_t v) noexcept {
    const auto u = static_cast<uint32_t>(v);
    dst[0] = static_cast<uint8_t>(u & 0xFFU);
    dst[1] = static_cast<uint8_t>((u >> 8) & 0xFFU);
    dst[2] = static_cast<uint8_t>((u >> 16) & 0xFFU);
    dst[3] = static_cast<uint8_t>((u >> 24) & 0xFFU);
}

inline int16_t unpack_be16(const uint8_t* src) noexcept {
    return static_cast<int16_t>((static_cast<uint16_t>(src[0]) << 8) | static_cast<uint16_t>(src[1]));
}

inline ImuSample decode_raw_imu_14b(const uint8_t* raw, uint64_t ts_us, uint32_t /*seq*/) noexcept {
    ImuSample s{};
    const int16_t temp_raw = unpack_be16(raw + 0);
    const int16_t ax_raw   = unpack_be16(raw + 2);
    const int16_t ay_raw   = unpack_be16(raw + 4);
    const int16_t az_raw   = unpack_be16(raw + 6);
    const int16_t gx_raw   = unpack_be16(raw + 8);
    const int16_t gy_raw   = unpack_be16(raw + 10);
    const int16_t gz_raw   = unpack_be16(raw + 12);

    s.temp_deg_c   = (static_cast<float>(temp_raw) / 132.48f) + 25.0f;
    s.accel_g[0]   = static_cast<float>(ax_raw) / 2048.0f;
    s.accel_g[1]   = static_cast<float>(ay_raw) / 2048.0f;
    s.accel_g[2]   = static_cast<float>(az_raw) / 2048.0f;
    s.gyro_dps[0]  = static_cast<float>(gx_raw) / 16.4f;
    s.gyro_dps[1]  = static_cast<float>(gy_raw) / 16.4f;
    s.gyro_dps[2]  = static_cast<float>(gz_raw) / 16.4f;
    s.timestamp_us = ts_us;
    s.valid        = true;
    return s;
}

// Tag 1 TLP (target_address = 0x40001000): 30-byte sensor_contract.encode_imu_payload layout
inline Tlp64 make_imu_tlp_from_raw(const uint8_t* raw14, uint16_t seq, uint64_t ts_ns, uint8_t flags = 0x01) noexcept {
    Tlp64 tlp{};
    tlp.wire.type           = static_cast<uint8_t>(TlpType::DmaStream);
    tlp.wire.flags          = flags;
    tlp.wire.tag            = 1;
    tlp.wire.channel        = static_cast<uint8_t>(Channel::Telemetry);
    tlp.wire.target_address = 0x40001000U;
    tlp.wire.length_dw      = 8; // 30 bytes = 8 DWs
    tlp.wire.sequence       = seq;
    tlp.wire.timestamp_ns   = ts_ns;
    std::memcpy(tlp.wire.payload, raw14, 14);
    tlp.wire.payload[14]    = 1; // ICM42688P chip code
    tlp.wire.payload[15]    = 0;
    for (int i = 0; i < 7; ++i) {
        pack_le16(&tlp.wire.payload[16 + i * 2], unpack_be16(&raw14[i * 2]));
    }
    tlp.wire.crc32 = 0xEFBEADDEU;
    return tlp;
}

inline Tlp64 make_imu_tlp_from_sample(const ImuSample& s, uint16_t seq) noexcept {
    uint8_t raw14[14]{};
    const auto temp_raw = static_cast<int16_t>((s.temp_deg_c - 25.0f) * 132.48f);
    const auto ax_raw   = static_cast<int16_t>(s.accel_g[0] * 2048.0f);
    const auto ay_raw   = static_cast<int16_t>(s.accel_g[1] * 2048.0f);
    const auto az_raw   = static_cast<int16_t>(s.accel_g[2] * 2048.0f);
    const auto gx_raw   = static_cast<int16_t>(s.gyro_dps[0] * 16.4f);
    const auto gy_raw   = static_cast<int16_t>(s.gyro_dps[1] * 16.4f);
    const auto gz_raw   = static_cast<int16_t>(s.gyro_dps[2] * 16.4f);
    pack_be16(&raw14[0], temp_raw);
    pack_be16(&raw14[2], ax_raw);
    pack_be16(&raw14[4], ay_raw);
    pack_be16(&raw14[6], az_raw);
    pack_be16(&raw14[8], gx_raw);
    pack_be16(&raw14[10], gy_raw);
    pack_be16(&raw14[12], gz_raw);
    return make_imu_tlp_from_raw(raw14, seq, s.timestamp_us * 1000ULL, 0x00);
}

// Tag 3 TLP (target_address = 0x40003000): 14-byte sensor_contract.encode_mag_payload layout
inline Tlp64 make_mag_tlp(const MagSample& s, uint16_t seq) noexcept {
    Tlp64 tlp{};
    tlp.wire.type           = static_cast<uint8_t>(TlpType::DmaStream);
    tlp.wire.flags          = 0;
    tlp.wire.tag            = 3;
    tlp.wire.channel        = static_cast<uint8_t>(Channel::Telemetry);
    tlp.wire.target_address = 0x40003000U;
    tlp.wire.length_dw      = 4; // 14 bytes = 4 DWs
    tlp.wire.sequence       = seq;
    tlp.wire.timestamp_ns   = s.timestamp_us * 1000ULL;
    // QMC5883L (chip_code = 2): 6 raw LE bytes + [2, 0] + 6 decoded LE bytes (3 LSB/mG @ 8G)
    const auto rx = static_cast<int16_t>(s.mag_mgauss[0] * 3.0f);
    const auto ry = static_cast<int16_t>(s.mag_mgauss[1] * 3.0f);
    const auto rz = static_cast<int16_t>(s.mag_mgauss[2] * 3.0f);
    pack_le16(&tlp.wire.payload[0], rx);
    pack_le16(&tlp.wire.payload[2], ry);
    pack_le16(&tlp.wire.payload[4], rz);
    tlp.wire.payload[6] = 2; // QMC5883L
    tlp.wire.payload[7] = 0;
    pack_le16(&tlp.wire.payload[8], rx);
    pack_le16(&tlp.wire.payload[10], ry);
    pack_le16(&tlp.wire.payload[12], rz);
    tlp.wire.crc32 = 0xEFBEADDEU;
    return tlp;
}

// Tag 2 TLP (target_address = 0x40005000): 26-byte GPS Geodetic Solution (<i5i2B)
inline Tlp64 make_gps_tlp(const GpsFix& f, uint16_t seq) noexcept {
    Tlp64 tlp{};
    tlp.wire.type           = static_cast<uint8_t>(TlpType::DmaStream);
    tlp.wire.flags          = 0;
    tlp.wire.tag            = 2;
    tlp.wire.channel        = static_cast<uint8_t>(Channel::Telemetry);
    tlp.wire.target_address = 0x40005000U;
    tlp.wire.length_dw      = 7; // 26 bytes = 7 DWs
    tlp.wire.sequence       = seq;
    tlp.wire.timestamp_ns   = f.timestamp_us * 1000ULL;
    pack_le32(&tlp.wire.payload[0], static_cast<int32_t>(f.itow_ms));
    pack_le32(&tlp.wire.payload[4], f.lat_1e7);
    pack_le32(&tlp.wire.payload[8], f.lon_1e7);
    pack_le32(&tlp.wire.payload[12], f.alt_msl_mm);
    pack_le32(&tlp.wire.payload[16], f.ground_speed_mm_s);
    pack_le32(&tlp.wire.payload[20], f.heading_1e5);
    tlp.wire.payload[24] = f.satellites;
    tlp.wire.payload[25] = static_cast<uint8_t>(f.fix_type);
    tlp.wire.crc32 = 0xEFBEADDEU;
    return tlp;
}

} // namespace detail_studio_tlp

// ============================================================================
// Multi-Rate Sensor Producer Coroutines (Zero State Machines)
// ============================================================================

// High-Rate IMU Task: Supports both hardware Auto-DMA (PI8 DRDY -> g_sensor_ring)
// and Polled SPI (imu.next_sample_async()), selectable from AbstractX Studio!
// @impl [SPEC-IMU-01] [SPEC-IMU-02] [SPEC-HAL-02] [SPEC-TLP-01] docs/DESIGN_SPECIFICATION.md#spec-imu-01
inline Task<void> imu_producer_task(Icm42688p& imu) {
    uint32_t seq = 0;
    while (true) {
        bool got_auto = false;
        Tlp64 tlp{};
        while (g_sensor_ring.pop(tlp)) {
            if (tlp.wire.type == 0x32U) { // ASP_TLP_TYPE_HW_FUSION_CPL
                g_telemetry_ring.push(tlp);
                continue;
            }
            const auto raw = tlp.payload();
            if (tlp.tag() == 1 && raw.size() >= 14) {
                ++seq;
                ImuSample sample = detail_studio_tlp::decode_raw_imu_14b(
                    raw.data(), tlp.wire.timestamp_ns / 1000ULL, seq);
                g_imu_channel.try_push(sample);
                g_telemetry_ring.push(detail_studio_tlp::make_imu_tlp_from_raw(
                    raw.data(), static_cast<uint16_t>(seq), tlp.wire.timestamp_ns, 0x01));
                got_auto = true;
            }
        }
        // @impl [SPEC-IMU-03] docs/DESIGN_SPECIFICATION.md (auto mode: one sample per DRDY edge, no polling)
        if (got_auto || hal::get_target_io_processor().is_auto_mode(0)) {
            co_await abstractx::step_async();
            continue;
        }

        ImuSample sample = co_await imu.next_sample_async();
        if (sample.valid) {
            ++seq;
            g_imu_channel.try_push(sample);
            g_telemetry_ring.push(detail_studio_tlp::make_imu_tlp_from_sample(
                sample, static_cast<uint16_t>(seq)));
        }
    }
}

// Medium-Rate Magnetometer Task: Ingests 50 Hz I2C 3-axis magnetic compass samples
// @impl [SPEC-HAL-05] [SPEC-APP-03] apps/gps_imu_app/SPECIFICATION.md#spec-app-03
inline Task<void> mag_producer_task(Qmc5883l& mag, hal::ITimer& timer) {
    uint16_t seq = 0;
    while (true) {
        co_await timer.sleep_ms_async(20); // 50 Hz sampling cadence
        MagSample sample = co_await mag.read_sample_async();
        if (sample.valid) {
            g_mag_channel.try_push(sample);
            g_telemetry_ring.push(detail_studio_tlp::make_mag_tlp(sample, ++seq));
        }
    }
}

// Low-Rate GPS Navigation Task: Ingests 10 Hz UBX-NAV-PVT solutions from UART
// @impl [SPEC-GPS-01] [SPEC-GPS-02] [SPEC-HAL-03] docs/DESIGN_SPECIFICATION.md#spec-gps-01
inline Task<void> gps_producer_task(UbloxGps& gps) {
    uint16_t seq = 0;
    while (true) {
        GpsFix fix = co_await gps.next_fix_async();
        if (fix.valid) {
            g_gps_channel.try_push(fix);
            g_telemetry_ring.push(detail_studio_tlp::make_gps_tlp(fix, ++seq));
        }
    }
}

// ============================================================================
// Multi-Rate Sensor Fusion & AHRS Filter Coroutine
// ============================================================================
// @impl [SPEC-APP-02] [SPEC-APP-03] [SPEC-ARCH-02] [SPEC-APP-10] apps/gps_imu_app/SPECIFICATION.md
inline Task<void> sensor_fusion_task(hal::ITimer& timer, AttitudeFilter& filter) {
    uint32_t seq = 0;
    uint64_t last_time_us = timer.get_time_us();

    while (true) {
        // 1. Asynchronously await next high-rate IMU sample as the primary pacing clock
        ImuSample imu = co_await g_imu_channel.pop();

        uint64_t now_us = timer.get_time_us();
        float dt = static_cast<float>(now_us - last_time_us) * 1e-6f;
        if (dt <= 0.0f || dt > 0.05f) dt = 0.001f;
        last_time_us = now_us;

        // 2. High-rate Gyro integration & Accel gravity tilt correction
        filter.update_imu(imu, dt);

        // 3. Drain medium-rate I2C Magnetometer samples (tilt-compensated yaw correction)
        MagSample mag;
        while (g_mag_channel.try_pop(mag)) {
            filter.update_mag(mag);
        }

        // 4. Drain low-rate UART GPS fixes (altitude, velocity vector, course heading)
        GpsFix gps;
        while (g_gps_channel.try_pop(gps)) {
            filter.update_gps(gps);
        }

        // 5. Emit fused 64-byte TLP into telemetry stream
        seq++;
        if ((seq % 10) == 0) { // Telemetry decimation
            Tlp64 tlp = AttitudeFilter::to_tlp(filter.state());
            g_telemetry_ring.push(tlp);
        }
    }
}

// ============================================================================
// Live Flight Telemetry & Heartbeat Display
// ============================================================================

// @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md#spec-hal-05
inline Task<void> flight_monitor_task(hal::ITimer& timer, const AttitudeFilter& filter) {
    uint32_t count = 0;
    while (true) {
        co_await timer.sleep_ms_async(1000);
        count++;

        const auto& s = filter.state();
        printf("[AbstractX AHRS #%u] Roll: %+5.1f° | Pitch: %+5.1f° | Yaw: %5.1f° (Mag: %5.1f°) | Alt: %5.1fm | Spd: %4.1f m/s | IMU: %u, Mag: %u, GPS: %u | Egress: %u pkts\n",
               static_cast<unsigned>(count),
               s.roll_deg,
               s.pitch_deg,
               s.yaw_deg,
               s.mag_heading_deg,
               s.altitude_m,
               s.ground_speed_m_s,
               static_cast<unsigned>(s.imu_updates),
               static_cast<unsigned>(s.mag_updates),
               static_cast<unsigned>(s.gps_updates),
               static_cast<unsigned>(g_telemetry_ring.size()));
    }
}

// Background Telemetry Egress Task
// @impl [SPEC-TLP-01] [SPEC-TLP-03] [SPEC-TRACE-03] [SPEC-TRACE-06] [SPEC-APP-08] docs/DESIGN_SPECIFICATION.md#spec-trace-03 apps/gps_imu_app/SPECIFICATION.md#spec-app-08
inline Task<void> telemetry_egress_task() {
    while (true) {
        Tlp64 tlp{};
        while (g_telemetry_ring.pop(tlp)) {
            hal::platform_send_telemetry(reinterpret_cast<const uint8_t*>(&tlp), sizeof(tlp));
        }
        co_await abstractx::step_async();
    }
}

// ============================================================================
// Unified Application Main Task
// ============================================================================

// @impl [SPEC-APP-01] [SPEC-ARCH-03] [SPEC-ARCH-05] [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-03
inline Task<void> app_main(int max_steps = 0) {
    auto& uart  = hal::get_uart_driver(0);
    auto& timer = hal::get_timer_driver();
    auto& spi   = hal::get_spi_driver(0);
    auto& i2c   = hal::get_i2c_driver();

    printf("\n========================================================\n");
    printf("  AbstractX - Multi-Rate Flight Controller & AHRS Node   \n");
    printf("  Structured Concurrency + Channel-Based Sensor Fusion  \n");
    printf("========================================================\n");

    static Icm42688p       imu(spi);
    static UbloxGps        gps(uart);
    static Qmc5883l        mag(i2c, hal::I2cDevice{DEFAULT_QMC_I2C_ADDR, 0});
    static AttitudeFilter  filter;

    // Structured Parallel Boot: Initialize SPI IMU, UART GPS, and I2C Mag concurrently!
    // @impl [SPEC-APP-01] apps/gps_imu_app/SPECIFICATION.md
    printf("[AbstractX Boot] Parallel hardware initialization (when_all: SPI IMU + UART GPS + I2C Mag)...\n");
    auto [imu_ok, gps_ok, mag_ok] = co_await coro::when_all(
        imu.init_async(),
        gps.init_async(115200),
        mag.init_async()
    );

    printf("[AbstractX Boot] Hardware Status: IMU=%s, GPS=%s, MAG=%s\n",
           imu_ok ? "OK" : "FAIL",
           gps_ok ? "OK" : "FAIL",
           mag_ok ? "OK" : "FAIL");

    // Spawn concurrent multi-rate producer and fusion tasks into AbstractX runtime
    abstractx::spawn(imu_producer_task(imu));
    abstractx::spawn(mag_producer_task(mag, timer));
    abstractx::spawn(gps_producer_task(gps));
    abstractx::spawn(sensor_fusion_task(timer, filter));
    abstractx::spawn(flight_monitor_task(timer, filter));
    abstractx::spawn(telemetry_egress_task());

    // Yield cooperatively to runtime dispatcher
    if (max_steps > 0) {
        for (int i = 0; i < max_steps; ++i) {
            co_await abstractx::step_async();
        }
    } else {
        while (true) {
            co_await abstractx::step_async();
        }
    }
}

#endif // APPS_GPS_IMU_APP_GPS_IMU_TASKS_HPP
