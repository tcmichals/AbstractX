/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * inav_sensor_coprocessor.hpp - XuanTie E906 C++ Hardware Sensor Co-Processor for INAV
 *
 * Handles physical sensor acquisition directly from bare-metal RISC-V:
 *   1. SPI1 @ 10 MHz: ICM-42688-P 6-DoF IMU (1 kHz / 8 kHz burst streaming)
 *   2. SPI1 Dual-I/O: Companion FPGA Hardware Interface (QMTECH Zynq / FPGA)
 *   3. TWI1 @ 400 kHz: HMC5883L / QMC5883L 3D Magnetometer / Compass
 *   4. UART3 @ 115200: u-blox NEO-M8N GNSS (GPS) NMEA/UBX stream
 *   5. VirtIO RPMsg (abstractx-tlp): 64-byte TLP streaming directly to inav_linux_sbc
 */
// @impl [SPEC-E906-HAL-01] [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>

#include "hal/spi.hpp"
#include "hal/timer.hpp"
#include "hal/rpmsg.hpp"
#include "hal/rpmsg_endpoints.hpp"
#include "hal/trace.hpp"
#include "hal/clic.hpp"
#include "abstractx/hal/platform.hpp"
#include "asp_tlp64.hpp"
#include "pcie_bar_map.hpp"

namespace inav::e906 {

namespace ahal = abstractx::hal;

// ============================================================================
// Direct Register Access Helpers
// ============================================================================
template <uintptr_t Addr>
struct Reg {
    static volatile uint32_t& r() noexcept { return *reinterpret_cast<volatile uint32_t*>(Addr); }
    static void touch() noexcept { const uint32_t v = r(); (void)v; }
};

// ============================================================================
// 1. ICM-42688-P High-Rate 6-DoF IMU Driver (SPI1)
// ============================================================================
class Icm42688pDriver {
public:
    static constexpr uint8_t REG_DEVICE_CONFIG   = 0x11;
    static constexpr uint8_t REG_TEMP_DATA1      = 0x1D;
    static constexpr uint8_t REG_INTF_CONFIG1    = 0x4D;
    static constexpr uint8_t REG_PWR_MGMT0       = 0x4E;
    static constexpr uint8_t REG_GYRO_CONFIG0    = 0x4F;
    static constexpr uint8_t REG_ACCEL_CONFIG0   = 0x50;
    static constexpr uint8_t REG_WHO_AM_I        = 0x75;
    static constexpr uint8_t REG_BANK_SEL        = 0x76;

    static constexpr uint8_t WHO_AM_I_ICM42688P  = 0x47;
    static constexpr uint8_t WHO_AM_I_ICM42605   = 0x42;

    static bool init(uint32_t speed_hz = 10000000) noexcept {
        fc::hal::Spi1::init(speed_hz);
        hal::Timer::delay_ms(10);

        // 1. Verify WHO_AM_I
        uint8_t who = read_reg(REG_WHO_AM_I);
        if (who != WHO_AM_I_ICM42688P && who != WHO_AM_I_ICM42605) {
            trace_puts("[E906 IMU] WHO_AM_I mismatch: 0x");
            put_hex(who);
            trace_puts(" (expected 0x47)\n");
            return false;
        }

        trace_puts("[E906 IMU] ICM-42688-P detected (WHO_AM_I=0x47). Configuring...\n");

        // 2. Soft Reset
        write_reg(REG_DEVICE_CONFIG, 0x01);
        hal::Timer::delay_ms(2);

        // 3. Bank 0: Disable AFSR
        write_reg(REG_BANK_SEL, 0x00);
        write_reg(REG_INTF_CONFIG1, 0x40);

        // 4. Power Management: Accel + Gyro Low-Noise Mode
        write_reg(REG_PWR_MGMT0, 0x0F);
        hal::Timer::delay_ms(1);

        // 5. Gyro & Accel ODR / Full-Scale (8 kHz ODR, +-2000 dps, +-16g)
        write_reg(REG_GYRO_CONFIG0, 0x03);  // +-2000 dps, 8 kHz
        write_reg(REG_ACCEL_CONFIG0, 0x03); // +-16g, 8 kHz

        trace_puts("[E906 IMU] Initialized at 8 kHz Low-Noise Mode.\n");
        return true;
    }

    // Burst read 14 bytes: Temp (2B), Accel (6B), Gyro (6B)
    static bool read_burst(uint8_t* out14) noexcept {
        uint8_t tx[15] = { static_cast<uint8_t>(0x80 | REG_TEMP_DATA1), 0 };
        uint8_t rx[15] = { 0 };

        bool ok = fc::hal::Spi1::transceive_fifo_sync(0 /* CS0 */, tx, rx, 15, false /* Single SPI */);
        if (ok) {
            std::memcpy(out14, &rx[1], 14);
        }
        return ok;
    }

    // Format 64-byte TLP Tag 1 (bar::ImuBase)
    static abstractx::Tlp64 format_tlp(const uint8_t* raw14, uint16_t seq, uint64_t ts) noexcept {
        abstractx::Tlp64 tlp{};
        tlp.wire.flags = 0x10;
        tlp.wire.tag = 1;
        tlp.wire.target_address = 0x40001000; // bar::ImuBase
        tlp.wire.length = 64;
        tlp.wire.sequence = seq;
        tlp.wire.timestamp_ns = ts;

        // Payload: 6B Accel + 6B Gyro + 2B Temp
        // raw14[0..1] = Temp, raw14[2..7] = Accel, raw14[8..13] = Gyro
        std::memcpy(&tlp.wire.payload[0], &raw14[2], 6); // Accel X,Y,Z
        std::memcpy(&tlp.wire.payload[6], &raw14[8], 6); // Gyro X,Y,Z
        std::memcpy(&tlp.wire.payload[12], &raw14[0], 2); // Temp
        return tlp;
    }

private:
    static uint8_t read_reg(uint8_t reg) noexcept {
        uint8_t tx[2] = { static_cast<uint8_t>(0x80 | reg), 0x00 };
        uint8_t rx[2] = { 0x00, 0x00 };
        fc::hal::Spi1::transceive_fifo_sync(0, tx, rx, 2, false);
        return rx[1];
    }

    static void write_reg(uint8_t reg, uint8_t val) noexcept {
        uint8_t tx[2] = { static_cast<uint8_t>(reg & 0x7F), val };
        uint8_t rx[2] = { 0x00, 0x00 };
        fc::hal::Spi1::transceive_fifo_sync(0, tx, rx, 2, false);
    }

    static void put_hex(uint8_t val) noexcept {
        const char hex[] = "0123456789ABCDEF";
        char buf[3] = { hex[(val >> 4) & 0xF], hex[val & 0xF], '\0' };
        trace_puts(buf);
    }
};

// ============================================================================
// 2. FPGA Dual-SPI Interface (SPI1 Dual-I/O Mode)
// ============================================================================
class FpgaDualSpiDriver {
public:
    static bool transceive_frame(const uint8_t* tx, uint8_t* rx, size_t len) noexcept {
        if (len <= fc::hal::Spi1::DMA_THRESHOLD) {
            return fc::hal::Spi1::transceive_fifo_sync(0 /* CS0 */, tx, rx, len, true /* Dual SPI */);
        }
        return fc::hal::Spi1::transceive_fpga_dual_sync(tx, rx, len);
    }
};

// ============================================================================
// 3. TWI1 Magnetometer / Compass Driver (Delegating to AbstractX II2c(1))
// ============================================================================
class CompassDriver {
public:
    static constexpr uint8_t HMC5883L_ADDR = 0x1E;
    static constexpr uint8_t QMC5883L_ADDR = 0x0D;

    enum class ChipType : uint8_t {
        None     = 0,
        Hmc5883l = 1,
        Qmc5883l = 2
    };

    static bool init() noexcept {
        auto& i2c = ahal::get_i2c_driver(1);
        ahal::I2cConfig cfg{};
        cfg.frequency_hz = 400000;
        cfg.speed = ahal::I2cSpeed::Fast_400k;
        if (!i2c.init(cfg)) return false;
        hal::Timer::delay_ms(10);

        // Probe HMC5883L (ID check "H43" at reg 0x0A)
        uint8_t id_reg = 0x0A;
        uint8_t id[3] = {0};
        if (i2c.write_read_sync(HMC5883L_ADDR, std::span<const uint8_t>(&id_reg, 1), id) &&
            id[0] == 'H' && id[1] == '4' && id[2] == '3') {
            uint8_t cra[2] = {0x00, 0x70}; // 8-avg, 15 Hz
            uint8_t crb[2] = {0x01, 0x20}; // +-1.3 Ga
            uint8_t mode[2] = {0x02, 0x00}; // Continuous mode
            i2c.write_sync(HMC5883L_ADDR, cra);
            i2c.write_sync(HMC5883L_ADDR, crb);
            i2c.write_sync(HMC5883L_ADDR, mode);
            chip_ = ChipType::Hmc5883l;
            trace_puts("[E906 Compass] HMC5883L detected and configured via AbstractX II2c(1).\n");
            return true;
        }

        // Probe QMC5883L
        uint8_t reset_cmd[2] = {0x0B, 0x01};
        uint8_t cfg_cmd[2] = {0x09, 0x1D};
        i2c.write_sync(QMC5883L_ADDR, reset_cmd);
        i2c.write_sync(QMC5883L_ADDR, cfg_cmd);
        uint8_t qmc_reg = 0x09;
        uint8_t qmc_ctrl = 0;
        if (i2c.write_read_sync(QMC5883L_ADDR, std::span<const uint8_t>(&qmc_reg, 1), std::span<uint8_t>(&qmc_ctrl, 1)) &&
            qmc_ctrl == 0x1D) {
            chip_ = ChipType::Qmc5883l;
            trace_puts("[E906 Compass] QMC5883L detected and configured via AbstractX II2c(1).\n");
            return true;
        }

        trace_puts("[E906 Compass] Warning: No compass detected on TWI1.\n");
        return false;
    }

    static bool read_vector(uint8_t* raw6) noexcept {
        auto& i2c = ahal::get_i2c_driver(1);
        if (chip_ == ChipType::Hmc5883l) {
            uint8_t reg = 0x03;
            return i2c.write_read_sync(HMC5883L_ADDR, std::span<const uint8_t>(&reg, 1), std::span<uint8_t>(raw6, 6));
        } else if (chip_ == ChipType::Qmc5883l) {
            uint8_t reg = 0x00;
            return i2c.write_read_sync(QMC5883L_ADDR, std::span<const uint8_t>(&reg, 1), std::span<uint8_t>(raw6, 6));
        }
        return false;
    }

    static abstractx::Tlp64 format_tlp(const uint8_t* raw6, uint16_t seq, uint64_t ts) noexcept {
        abstractx::Tlp64 tlp{};
        tlp.wire.flags = 0x10;
        tlp.wire.tag = 3;
        tlp.wire.target_address = 0x40003000; // bar::MagBase
        tlp.wire.length = 64;
        tlp.wire.sequence = seq;
        tlp.wire.timestamp_ns = ts;

        // Payload: 6B Raw Mag Vector + 1B Chip Type + 1B Pad
        std::memcpy(&tlp.wire.payload[0], raw6, 6);
        tlp.wire.payload[6] = static_cast<uint8_t>(chip_);
        tlp.wire.payload[7] = 0x00;
        return tlp;
    }

    static ChipType chip_type() noexcept { return chip_; }

private:
    static inline ChipType chip_{ChipType::None};
};

// ============================================================================
// 4. UART3 GPS Driver (Delegating to AbstractX IUart(3))
// ============================================================================
class GpsUartDriver {
public:
    static bool init(uint32_t baud = 115200) noexcept {
        auto* u = ahal::get_uart_port(3);
        if (!u) return false;
        u->init(baud);
        trace_puts("[E906 GPS] UART3 configured at 115200 baud via AbstractX IUart(3).\n");
        return true;
    }

    static bool has_byte() noexcept {
        auto& u_lsr = *reinterpret_cast<volatile uint32_t*>(0x02500C00U + 0x14);
        return (u_lsr & 0x01) != 0;
    }

    static uint8_t read_byte() noexcept {
        uint8_t b = 0;
        auto* u = ahal::get_uart_port(3);
        if (u) u->read_byte(b);
        return b;
    }
};

} // namespace inav::e906
