/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Domain Sensor Driver Interfaces
 * -----------------------------------------
 * Pure abstract base classes establishing strict polymorphic contracts
 * for hardware sensor drivers, simulation mocks, and multi-sensor failover.
 * All lifecycle and sampling methods are asynchronous C++20 coroutines.
 */

#ifndef ABSTRACTX_SENSOR_SENSOR_INTERFACE_HPP
#define ABSTRACTX_SENSOR_SENSOR_INTERFACE_HPP

#include "abstractx/sensor/sensor_types.hpp"
#include "abstractx/coro.hpp"

namespace abstractx::sensor {

/*
 * Base Lifecycle Contract for all Peripheral Drivers
 * @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md#spec-arch-01
 */
class ISensorDriver {
public:
    virtual ~ISensorDriver() = default;

    /*
     * Asynchronous hardware initialization, WHO_AM_I validation,
     * and operating mode configuration.
     */
    virtual coro::Task<bool> init_async() = 0;
};

/*
 * Abstract 6-Axis / 9-Axis High-Rate IMU Driver Interface
 * @impl [SPEC-IMU-01] docs/DESIGN_SPECIFICATION.md#spec-imu-01
 */
class IImuDriver : public ISensorDriver {
public:
    virtual ~IImuDriver() = default;

    /*
     * Awaits next calibrated IMU sample (Gyroscope + Accelerometer)
     * paced by hardware DRDY interrupt or DMA completion.
     */
    virtual coro::Task<imu::ImuSample> next_sample_async() = 0;
};

/*
 * Abstract 3-Axis Magnetometer Driver Interface
 * @impl [SPEC-MAG-01] docs/DESIGN_SPECIFICATION.md
 */
class IMagDriver : public ISensorDriver {
public:
    virtual ~IMagDriver() = default;

    /*
     * Asynchronously reads calibrated magnetic field vector (milliGauss).
     */
    virtual coro::Task<mag::MagSample> read_sample_async() = 0;
};

/*
 * Abstract GNSS / GPS Navigation Receiver Interface
 * @impl [SPEC-GPS-01] [SPEC-GPS-02] docs/DESIGN_SPECIFICATION.md#spec-gps-01
 */
class IGpsDriver : public ISensorDriver {
public:
    virtual ~IGpsDriver() = default;

    /*
     * Awaits next navigation PVT solution frame parsed from UART stream.
     */
    virtual coro::Task<gps::GpsFix> next_fix_async() = 0;
};

} // namespace abstractx::sensor

namespace abstractx::drivers {
    using namespace abstractx::sensor;
} // namespace abstractx::drivers

#endif // ABSTRACTX_SENSOR_SENSOR_INTERFACE_HPP
