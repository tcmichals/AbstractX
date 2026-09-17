#ifndef ABSTRACTX_FUSION_ATTITUDE_FILTER_HPP
#define ABSTRACTX_FUSION_ATTITUDE_FILTER_HPP

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

#include "abstractx/drivers/imu/icm42688p.hpp"
#include "abstractx/drivers/mag/qmc5883l.hpp"
#include "abstractx/drivers/gps/ublox_gps.hpp"
#include "asp_tlp64.hpp"

namespace abstractx::fusion {

constexpr uint8_t TLP_TAG_FUSED_AHRS = 0x04;

/* Unit Quaternion Orientation */
struct Quaternion {
    float w{1.0f};
    float x{0.0f};
    float y{0.0f};
    float z{0.0f};

    void normalize() noexcept {
        float n = std::sqrt(w * w + x * x + y * y + z * z);
        if (n > 1e-6f) {
            float inv = 1.0f / n;
            w *= inv;
            x *= inv;
            y *= inv;
            z *= inv;
        } else {
            w = 1.0f; x = y = z = 0.0f;
        }
    }
};

/* Motor Output Commands (0..1000 per mil / microseconds) */
struct MotorMix {
    uint16_t m1{0}; // Front-Right (CCW)
    uint16_t m2{0}; // Rear-Left (CCW)
    uint16_t m3{0}; // Front-Left (CW)
    uint16_t m4{0}; // Rear-Right (CW)
};

/* Flight State Estimation Vector */
struct FlightAttitude {
    Quaternion q{};                     // Unit Quaternion [q0, q1, q2, q3]
    float      roll_deg{0.0f};          // Euler Roll (±180°)
    float      pitch_deg{0.0f};         // Euler Pitch (±90°)
    float      yaw_deg{0.0f};           // Euler Yaw (0..360° North referenced)
    float      mag_heading_deg{0.0f};   // Raw tilt-compensated magnetic heading
    float      gyro_bias[3]{0.0f, 0.0f, 0.0f}; // Estimated Gyro Integral Drift
    float      altitude_m{0.0f};        // Fused MSL Altitude
    float      ground_speed_m_s{0.0f};  // GPS Horizontal Velocity
    MotorMix   motors{};                // Real-Time Motor Mixer Outputs
    uint32_t   imu_updates{0};
    uint32_t   mag_updates{0};
    uint32_t   gps_updates{0};
    uint64_t   timestamp_us{0};
};

/*
 * 9-DoF Mahony Quaternion AHRS Filter with Cascaded Rate/Attitude PID
 * @impl [SPEC-APP-04] [SPEC-APP-05] [SPEC-APP-06] apps/gps_imu_app/SPECIFICATION.md
 */
class AttitudeFilter {
public:
    AttitudeFilter(float kp = 2.0f, float ki = 0.005f)
        : kp_(kp), ki_(ki) {}

    /*
     * 1. High-Rate IMU Step: 9-DoF Mahony Quaternion Update
     */
    void update_imu(const drivers::imu::ImuSample& imu, float dt = 0.001f) noexcept {
        if (!imu.valid) return;

        // Convert Gyro deg/sec to radians/sec
        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        float gx = imu.gyro_dps[0] * DEG_TO_RAD;
        float gy = imu.gyro_dps[1] * DEG_TO_RAD;
        float gz = imu.gyro_dps[2] * DEG_TO_RAD;

        float ax = imu.accel_g[0];
        float ay = imu.accel_g[1];
        float az = imu.accel_g[2];

        float q0 = state_.q.w;
        float q1 = state_.q.x;
        float q2 = state_.q.y;
        float q3 = state_.q.z;

        float ex = 0.0f, ey = 0.0f, ez = 0.0f;

        // Normalize accelerometer reading
        float a_norm = std::sqrt(ax * ax + ay * ay + az * az);
        if (a_norm > 1e-4f) {
            ax /= a_norm;
            ay /= a_norm;
            az /= a_norm;

            // Estimated gravity vector in body frame: v = R^T * [0, 0, 1]^T
            float vx = 2.0f * (q1 * q3 - q0 * q2);
            float vy = 2.0f * (q0 * q1 + q2 * q3);
            float vz = q0 * q0 - q1 * q1 - q2 * q2 + q3 * q3;

            // Error is cross product of measured gravity (a) and estimated gravity (v)
            ex = (ay * vz - az * vy);
            ey = (az * vx - ax * vz);
            ez = (ax * vy - ay * vx);
        }

        // Add magnetometer error if valid
        if (has_mag_) {
            ex += mag_error_[0];
            ey += mag_error_[1];
            ez += mag_error_[2];
        }

        // Apply PI correction: integral drift tracking (Ki)
        e_int_[0] += ex * ki_ * dt;
        e_int_[1] += ey * ki_ * dt;
        e_int_[2] += ez * ki_ * dt;

        // Gyroscope measurement corrected by Mahony filter
        gx += kp_ * ex + e_int_[0];
        gy += kp_ * ey + e_int_[1];
        gz += kp_ * ez + e_int_[2];

        state_.gyro_bias[0] = e_int_[0];
        state_.gyro_bias[1] = e_int_[1];
        state_.gyro_bias[2] = e_int_[2];

        // Quaternion linear differential kinematics: q_dot = 0.5 * q (x) omega
        float dq0 = 0.5f * (-q1 * gx - q2 * gy - q3 * gz);
        float dq1 = 0.5f * ( q0 * gx + q2 * gz - q3 * gy);
        float dq2 = 0.5f * ( q0 * gy - q1 * gz + q3 * gx);
        float dq3 = 0.5f * ( q0 * gz + q1 * gy - q2 * gx);

        state_.q.w += dq0 * dt;
        state_.q.x += dq1 * dt;
        state_.q.y += dq2 * dt;
        state_.q.z += dq3 * dt;
        state_.q.normalize();

        // Convert Quaternion to Euler Angles for flight controller loops
        update_euler_angles();

        // Run Cascaded Flight Controller PID and Motor Mixer
        update_flight_control(imu.gyro_dps[0], imu.gyro_dps[1], imu.gyro_dps[2], dt);

        state_.imu_updates++;
        state_.timestamp_us = imu.timestamp_us;
    }

    /*
     * 2. Medium-Rate Magnetometer Step: Earth Reference Magnetic Correction
     */
    void update_mag(const drivers::mag::MagSample& mag) noexcept {
        if (!mag.valid) return;

        float mx = mag.mag_mgauss[0];
        float my = mag.mag_mgauss[1];
        float mz = mag.mag_mgauss[2];

        float m_norm = std::sqrt(mx * mx + my * my + mz * mz);
        if (m_norm < 1e-4f) return;
        mx /= m_norm;
        my /= m_norm;
        mz /= m_norm;

        float q0 = state_.q.w;
        float q1 = state_.q.x;
        float q2 = state_.q.y;
        float q3 = state_.q.z;

        // Rotate magnetic vector to Earth reference frame: h = q (x) m (x) q*
        float hx = 2.0f * (mx * (0.5f - q2 * q2 - q3 * q3) + my * (q1 * q2 - q0 * q3) + mz * (q1 * q3 + q0 * q2));
        float hy = 2.0f * (mx * (q1 * q2 + q0 * q3) + my * (0.5f - q1 * q1 - q3 * q3) + mz * (q2 * q3 - q0 * q1));
        float bx = std::sqrt(hx * hx + hy * hy);
        float bz = 2.0f * (mx * (q1 * q3 - q0 * q2) + my * (q2 * q3 + q0 * q1) + mz * (0.5f - q1 * q1 - q2 * q2));

        // Rotate reference back to body frame: w = R^T * [bx, 0, bz]^T
        float wx = 2.0f * bx * (0.5f - q2 * q2 - q3 * q3) + 2.0f * bz * (q1 * q3 - q0 * q2);
        float wy = 2.0f * bx * (q1 * q2 - q0 * q3)         + 2.0f * bz * (q0 * q1 + q2 * q3);
        float wz = 2.0f * bx * (q0 * q2 + q1 * q3)         + 2.0f * bz * (0.5f - q1 * q1 - q2 * q2);

        // Magnetic error is cross product: e_m = m x w
        mag_error_[0] = my * wz - mz * wy;
        mag_error_[1] = mz * wx - mx * wz;
        mag_error_[2] = mx * wy - my * wx;

        state_.mag_heading_deg = mag.heading_deg;
        has_mag_ = true;
        state_.mag_updates++;
    }

    /*
     * 3. Low-Rate GPS Step: Ground Speed, MSL Altitude, Course Fusion
     */
    void update_gps(const drivers::gps::GpsFix& gps) noexcept {
        if (!gps.valid) return;

        state_.altitude_m = static_cast<float>(gps.alt_msl_mm) * 0.001f;
        state_.ground_speed_m_s = static_cast<float>(gps.ground_speed_mm_s) * 0.001f;

        // When ground speed > 1.5 m/s, GPS course heading reinforces yaw
        if (state_.ground_speed_m_s > 1.5f && gps.fix_type >= drivers::gps::GpsFixType::Fix3D) {
            float gps_course = static_cast<float>(gps.heading_1e5) * 1e-5f;
            float yaw_error = wrap_180(gps_course - state_.yaw_deg);
            state_.yaw_deg += 0.02f * yaw_error;
        }

        state_.gps_updates++;
    }

    const FlightAttitude& state() const noexcept {
        return state_;
    }

    static Tlp64 to_tlp(const FlightAttitude& a) noexcept {
        Tlp64 tlp{};
        tlp.wire.channel = static_cast<uint8_t>(Channel::Telemetry);
        tlp.wire.tag = TLP_TAG_FUSED_AHRS;
        tlp.wire.type = 0x02; // Fused State
        tlp.wire.timestamp_ns = a.timestamp_us * 1000ULL;

        int16_t roll_cdeg  = static_cast<int16_t>(a.roll_deg * 100.0f);
        int16_t pitch_cdeg = static_cast<int16_t>(a.pitch_deg * 100.0f);
        uint16_t yaw_cdeg  = static_cast<uint16_t>(a.yaw_deg * 100.0f);
        int32_t alt_mm     = static_cast<int32_t>(a.altitude_m * 1000.0f);
        uint16_t spd_cm_s  = static_cast<uint16_t>(a.ground_speed_m_s * 100.0f);

        std::memcpy(&tlp.wire.payload[0], &roll_cdeg,  sizeof(roll_cdeg));
        std::memcpy(&tlp.wire.payload[2], &pitch_cdeg, sizeof(pitch_cdeg));
        std::memcpy(&tlp.wire.payload[4], &yaw_cdeg,   sizeof(yaw_cdeg));
        std::memcpy(&tlp.wire.payload[6], &alt_mm,     sizeof(alt_mm));
        std::memcpy(&tlp.wire.payload[10], &spd_cm_s,  sizeof(spd_cm_s));
        std::memcpy(&tlp.wire.payload[12], &a.motors.m1, sizeof(uint16_t));
        std::memcpy(&tlp.wire.payload[14], &a.motors.m2, sizeof(uint16_t));

        return tlp;
    }

private:
    static float wrap_180(float angle) noexcept {
        while (angle > 180.0f)  angle -= 360.0f;
        while (angle < -180.0f) angle += 360.0f;
        return angle;
    }

    void update_euler_angles() noexcept {
        float q0 = state_.q.w;
        float q1 = state_.q.x;
        float q2 = state_.q.y;
        float q3 = state_.q.z;

        constexpr float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;

        // Roll: atan2(2(q0*q1 + q2*q3), 1 - 2(q1^2 + q2^2))
        state_.roll_deg = std::atan2(2.0f * (q0 * q1 + q2 * q3), 1.0f - 2.0f * (q1 * q1 + q2 * q2)) * RAD_TO_DEG;

        // Pitch: asin(2(q0*q2 - q3*q1))
        float sinp = 2.0f * (q0 * q2 - q3 * q1);
        if (std::abs(sinp) >= 1.0f) {
            state_.pitch_deg = std::copysign(90.0f, sinp);
        } else {
            state_.pitch_deg = std::asin(sinp) * RAD_TO_DEG;
        }

        // Yaw: atan2(2(q0*q3 + q1*q2), 1 - 2(q2^2 + q3^2))
        float yaw = std::atan2(2.0f * (q0 * q3 + q1 * q2), 1.0f - 2.0f * (q2 * q2 + q3 * q3)) * RAD_TO_DEG;
        if (yaw < 0.0f) yaw += 360.0f;
        state_.yaw_deg = yaw;
    }

    /*
     * Cascaded PID Flight Controller:
     * [Desired Angle (0°)] -> (Outer Loop PID) -> [Desired Rate]
     * [Desired Rate]       -> (Inner Loop PID) -> [Motor Torques]
     * Motor Mixer (Quad X): Distributes Torques + Base Hover Throttle to 4 ESCs
     */
    void update_flight_control(float gyro_x, float gyro_y, float gyro_z, float dt) noexcept {
        // Outer Loop: Angle Error -> Target Rate
        constexpr float KP_ANGLE = 4.5f;
        float target_rate_roll  = (0.0f - state_.roll_deg)  * KP_ANGLE;
        float target_rate_pitch = (0.0f - state_.pitch_deg) * KP_ANGLE;
        float target_rate_yaw   = 0.0f; // Maintain current heading

        // Inner Loop: Angular Velocity Error -> Motor Torque Commands
        constexpr float KP_RATE = 1.2f;
        constexpr float KD_RATE = 0.03f;

        float err_roll  = target_rate_roll  - gyro_x;
        float err_pitch = target_rate_pitch - gyro_y;
        float err_yaw   = target_rate_yaw   - gyro_z;

        float d_roll  = (err_roll  - last_rate_err_[0]) / dt;
        float d_pitch = (err_pitch - last_rate_err_[1]) / dt;
        float d_yaw   = (err_yaw   - last_rate_err_[2]) / dt;

        last_rate_err_[0] = err_roll;
        last_rate_err_[1] = err_pitch;
        last_rate_err_[2] = err_yaw;

        float u_roll  = KP_RATE * err_roll  + KD_RATE * d_roll;
        float u_pitch = KP_RATE * err_pitch + KD_RATE * d_pitch;
        float u_yaw   = KP_RATE * err_yaw   + KD_RATE * d_yaw;

        // Quadcopter X Motor Mixer: Base throttle = 500 (50% hover)
        constexpr float BASE_THROTTLE = 500.0f;
        float m1 = BASE_THROTTLE - u_roll + u_pitch + u_yaw; // Front-Right
        float m2 = BASE_THROTTLE + u_roll - u_pitch + u_yaw; // Rear-Left
        float m3 = BASE_THROTTLE + u_roll + u_pitch - u_yaw; // Front-Left
        float m4 = BASE_THROTTLE - u_roll - u_pitch - u_yaw; // Rear-Right

        auto clamp_motor = [](float val) -> uint16_t {
            if (val < 100.0f)  return 100;
            if (val > 1000.0f) return 1000;
            return static_cast<uint16_t>(val);
        };

        state_.motors.m1 = clamp_motor(m1);
        state_.motors.m2 = clamp_motor(m2);
        state_.motors.m3 = clamp_motor(m3);
        state_.motors.m4 = clamp_motor(m4);
    }

    FlightAttitude state_{};
    float kp_{2.0f};
    float ki_{0.005f};
    float e_int_[3]{0.0f, 0.0f, 0.0f};
    float mag_error_[3]{0.0f, 0.0f, 0.0f};
    float last_rate_err_[3]{0.0f, 0.0f, 0.0f};
    bool  has_mag_{false};
};

} // namespace abstractx::fusion

#endif // ABSTRACTX_FUSION_ATTITUDE_FILTER_HPP
