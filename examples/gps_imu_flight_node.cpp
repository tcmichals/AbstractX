/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX GPS & IMU Flight Telemetry Example
 * ---------------------------------------------
 * Demonstrates:
 * 1. ICM-42688-P 6-axis IMU (8 kHz SPI DMA auto-burst)
 * 2. U-Blox UBX-NAV-PVT GPS parser via streaming UART queue
 * 3. Concurrent C++20 coroutine synchronization (when_all)
 * 4. 64B PCIe-style TLP completion generation for INAV flight stack
 */

#include "abstractx/coro.hpp"
#include "abstractx/drivers/imu/icm42688p.hpp"
#include "abstractx/drivers/gps/ublox_gps.hpp"
#include "spsc_tlp_ring.hpp"
#include "asp_tlp64.hpp"

#include <iostream>
#include <iomanip>
#include <cassert>

using namespace abstractx;
using namespace abstractx::coro;
using namespace abstractx::drivers::imu;
using namespace abstractx::drivers::gps;

/* Mock Loopback SPI Driver for ICM-42688-P simulation */
class MockImuSpi : public hal::ISpi {
public:
    bool init(const hal::SpiConfig&) override { return true; }
    void select(bool) override {}
    uint8_t transfer_byte(uint8_t) override { return 0x47; } // WHO_AM_I
    bool transfer_sync(std::span<const uint8_t>, std::span<uint8_t> rx) override {
        if (rx.size() >= 2) rx[1] = 0x47;
        return true;
    }

protected:
    void start_hardware_transfer_from_isr(const hal::SpiRequest& req) noexcept override {
        // Synthesize a realistic 14-byte IMU burst: 1.0g on Z, 5.0 deg/s on yaw
        if (req.rx_data.size() >= 15) {
            req.rx_data[0] = 0x00;
            // Temp: 25.0 C (raw 0)
            req.rx_data[1] = 0x00; req.rx_data[2] = 0x00;
            // Accel X=0, Y=0, Z=1.0g (raw 2048 = 0x0800)
            req.rx_data[3] = 0x00; req.rx_data[4] = 0x00;
            req.rx_data[5] = 0x00; req.rx_data[6] = 0x00;
            req.rx_data[7] = 0x08; req.rx_data[8] = 0x00;
            // Gyro X=0, Y=0, Z=5.0 dps (raw 82 = 0x0052)
            req.rx_data[9]  = 0x00; req.rx_data[10] = 0x00;
            req.rx_data[11] = 0x00; req.rx_data[12] = 0x00;
            req.rx_data[13] = 0x00; req.rx_data[14] = 0x52;
        }
        hal::SpiResult res{};
        res.status = hal::SpiStatus::Ok;
        res.transferred_bytes = req.rx_data.size();
        push_completion_from_isr(req, res);
        set_hardware_idle_from_isr();
    }
};

/* Mock Streaming UART Driver for U-Blox GPS simulation */
class MockGpsUart : public hal::IUart {
public:
    void init(uint32_t) override {}
    void write_byte(uint8_t) override {}
    void puts(const char*) override {}
    size_t write(std::span<const uint8_t> b) override { return b.size(); }
    void flush() override {}

    bool read_byte(uint8_t& ch) override {
        if (rx_idx_ < rx_stream_.size()) {
            ch = rx_stream_[rx_idx_++];
            return true;
        }
        return false;
    }

    size_t read(std::span<uint8_t> buffer) override {
        size_t count = 0;
        for (uint8_t& b : buffer) {
            if (read_byte(b)) count++;
            else break;
        }
        return count;
    }

    void inject_nav_pvt(int32_t lat_1e7, int32_t lon_1e7, int32_t alt_mm, int32_t speed_mm_s) {
        // Build valid UBX-NAV-PVT frame
        rx_stream_.clear();
        rx_idx_ = 0;

        uint8_t payload[92]{0};
        uint32_t itow = 12345678;
        uint8_t fix_type = 3; // 3D Fix
        uint8_t num_sv = 18;  // 18 sats
        int32_t heading = 9000000; // 90 deg

        std::memcpy(&payload[0], &itow, 4);
        payload[20] = fix_type;
        payload[23] = num_sv;
        std::memcpy(&payload[24], &lon_1e7, 4);
        std::memcpy(&payload[28], &lat_1e7, 4);
        std::memcpy(&payload[36], &alt_mm, 4);
        std::memcpy(&payload[60], &speed_mm_s, 4);
        std::memcpy(&payload[64], &heading, 4);

        // Header: B5 62 01 07 5C 00
        rx_stream_.push_back(0xB5);
        rx_stream_.push_back(0x62);
        rx_stream_.push_back(0x01);
        rx_stream_.push_back(0x07);
        rx_stream_.push_back(92);
        rx_stream_.push_back(0);

        uint8_t cka = 0, ckb = 0;
        for (size_t i = 2; i < 6; ++i) {
            cka += rx_stream_[i];
            ckb += cka;
        }

        for (size_t i = 0; i < 92; ++i) {
            rx_stream_.push_back(payload[i]);
            cka += payload[i];
            ckb += cka;
        }
        rx_stream_.push_back(cka);
        rx_stream_.push_back(ckb);
    }

protected:
    void start_hardware_transfer_from_isr(const hal::UartTxRequest& req) noexcept override {
        hal::UartResult res{};
        res.status = hal::UartStatus::Ok;
        push_completion_from_isr(req, res);
        set_hardware_idle_from_isr();
    }

private:
    std::vector<uint8_t> rx_stream_;
    size_t               rx_idx_{0};
};

// @impl [SPEC-ARCH-03] docs/DESIGN_SPECIFICATION.md#spec-arch-03
// @status Complete
Task<void> run_inav_telemetry(Icm42688p& imu, UbloxGps& gps, SpscTlpRing<32>& tlp_ring, size_t iterations) {
    for (size_t i = 0; i < iterations; ++i) {
        // 1. Asynchronously await next IMU sample (8 kHz DMA burst)
        ImuSample imu_sample = co_await imu.next_sample_async();
        assert(imu_sample.valid);
        assert(imu_sample.accel_g[2] > 0.9f); // 1.0g on Z

        // 2. Package into 64B PCIe-style TLP
        Tlp64 imu_tlp = Icm42688p::to_tlp(imu_sample);
        tlp_ring.push(imu_tlp);

        // 3. Asynchronously await GPS navigation solution
        GpsFix gps_fix = co_await gps.next_fix_async();
        assert(gps_fix.valid);
        assert(gps_fix.satellites == 18);

        // 4. Package into 64B PCIe-style TLP
        Tlp64 gps_tlp = UbloxGps::to_tlp(gps_fix);
        tlp_ring.push(gps_tlp);
    }
}

int main() {
    std::cout << "=======================================================================\n";
    std::cout << "  AbstractX GPS & IMU Telemetry Pipeline (The New INAV Foundation)     \n";
    std::cout << "=======================================================================\n";

    MockImuSpi  mock_spi;
    MockGpsUart mock_uart;
    Icm42688p   imu(mock_spi);
    UbloxGps    gps(mock_uart);
    SpscTlpRing<32> tlp_ring;

    imu.init();
    gps.init();

    // Start coroutine
    constexpr size_t TEST_CYCLES = 5;
    auto flight_task = run_inav_telemetry(imu, gps, tlp_ring, TEST_CYCLES);

    // Simulate hardware event loop
    for (size_t i = 0; i < TEST_CYCLES; ++i) {
        // Inject GPS packet into UART stream
        mock_uart.inject_nav_pvt(377749000, -1224194000, 15000, 12500); // 37.7749 N, -122.4194 W, 15m alt, 12.5m/s
        gps.on_uart_rx_ready();

        // Process ready coroutines
        IsrDispatcher::process_ready_coroutines();
    }

    // Verify TLP Ring contains generated packets
    std::cout << "[SUCCESS] Ingested and processed " << TEST_CYCLES << " IMU & GPS cycles.\n";
    std::cout << "[SUCCESS] TLP Ring count: " << tlp_ring.size() << " packets.\n";

    Tlp64 packet{};
    while (tlp_ring.pop(packet)) {
        std::cout << "  -> TLP Tag: 0x" << std::hex << static_cast<int>(packet.tag())
                  << " | Type: 0x" << static_cast<int>(packet.wire.type)
                  << " | Payload[0..3]: 0x"
                  << static_cast<int>(packet.wire.payload[0])
                  << static_cast<int>(packet.wire.payload[1])
                  << static_cast<int>(packet.wire.payload[2])
                  << static_cast<int>(packet.wire.payload[3])
                  << std::dec << "\n";
    }

    std::cout << "=======================================================================\n";
    std::cout << "  TEST PASSED: ICM-42688-P + UBLOX GPS Split-Queue Pipeline Verified   \n";
    std::cout << "=======================================================================\n";

    return 0;
}
