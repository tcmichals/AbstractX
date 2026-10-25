/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Domain Sensor Data Structures & Engineering Units
 * ------------------------------------------------------------
 * Standardized, zero-heap sensor telemetry payloads for IMU,
 * Magnetometer, and GNSS/GPS devices across all supported silicon.
 */

#ifndef ABSTRACTX_SENSOR_SENSOR_TYPES_HPP
#define ABSTRACTX_SENSOR_SENSOR_TYPES_HPP

#include <cstdint>
#include <cstddef>

namespace abstractx::sensor {

namespace imu {

/*
 * Calibrated 6-Axis / 9-Axis IMU Engineering Units Sample
 * @impl [SPEC-IMU-01] [SPEC-IMU-02] docs/DESIGN_SPECIFICATION.md#spec-imu-01
 */
struct ImuSample {
    float    accel_g[3]{0.0f, 0.0f, 0.0f};  // X, Y, Z in Gs (±16g range)
    float    gyro_dps[3]{0.0f, 0.0f, 0.0f}; // X, Y, Z in deg/sec (±2000 dps range)
    float    temp_deg_c{0.0f};              // Die temperature in degrees Celsius
    uint64_t timestamp_us{0};               // Microsecond hardware timestamp
    bool     valid{false};
};

} // namespace imu

namespace mag {

/*
 * Calibrated 3-Axis Magnetometer Sample
 * @impl [SPEC-MAG-01] docs/DESIGN_SPECIFICATION.md
 */
struct MagSample {
    float    mag_mgauss[3]{0.0f, 0.0f, 0.0f}; // X, Y, Z in milliGauss
    float    heading_deg{0.0f};                // Uncompensated 2D heading (0..360 deg)
    uint64_t timestamp_us{0};                  // Microsecond hardware timestamp
    bool     valid{false};
};

} // namespace mag

namespace gps {

enum class GpsFixType : uint8_t {
    NoFix                 = 0,
    DeadReckoning         = 1,
    Fix2D                 = 2,
    Fix3D                 = 3,
    GnssPlusDeadReckoning = 4,
    TimeOnly              = 5
};

/*
 * Calibrated GPS / GNSS Navigation Solution
 * @impl [SPEC-GPS-01] [SPEC-GPS-02] docs/DESIGN_SPECIFICATION.md#spec-gps-01
 */
struct GpsFix {
    int32_t    lat_1e7{0};           // Latitude (degrees * 1e7)
    int32_t    lon_1e7{0};           // Longitude (degrees * 1e7)
    int32_t    alt_msl_mm{0};        // Altitude MSL (mm)
    int32_t    ground_speed_mm_s{0}; // Ground Speed (mm/s)
    int32_t    heading_1e5{0};       // Heading of motion (degrees * 1e5)
    uint8_t    satellites{0};        // Number of satellites tracked
    GpsFixType fix_type{GpsFixType::NoFix};
    uint32_t   itow_ms{0};           // GPS Time of Week
    uint64_t   timestamp_us{0};      // Microsecond hardware timestamp
    bool       valid{false};
};

} // namespace gps

} // namespace abstractx::sensor

namespace abstractx::drivers {
namespace imu {
    using abstractx::sensor::imu::ImuSample;
}
namespace mag {
    using abstractx::sensor::mag::MagSample;
}
namespace gps {
    using abstractx::sensor::gps::GpsFix;
    using abstractx::sensor::gps::GpsFixType;
}
} // namespace abstractx::drivers

#endif // ABSTRACTX_SENSOR_SENSOR_TYPES_HPP
