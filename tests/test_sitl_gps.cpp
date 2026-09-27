/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX SITL CppUTest: U-Blox UBX-NAV-PVT GPS Driver Unit Tests
 */

#include "CppUTest/TestHarness.h"
#include "CppUTestExt/MockSupport.h"

#undef new
#undef delete
#include "abstractx/drivers/gps/ublox_gps.hpp"
#include "abstractx/hal/uart.hpp"
#include "CppUTest/MemoryLeakDetectorNewMacros.h"

#include <vector>

using namespace abstractx;
using namespace abstractx::drivers::gps;

class MockUart : public hal::IUart {
public:
    void init(uint32_t baudrate) override {
        mock().actualCall("init").withParameter("baudrate", baudrate);
    }

    bool set_baud_rate(uint32_t baudrate) override {
        mock().actualCall("set_baud_rate").withParameter("baudrate", baudrate);
        return true;
    }

    void write_byte(uint8_t ch) override {
        (void)ch;
    }

    void puts(const char* str) override {
        (void)str;
    }

    size_t write(std::span<const uint8_t> buffer) override {
        return buffer.size();
    }

    bool read_byte(uint8_t& ch) override {
        if (rx_idx_ < rx_buffer_.size()) {
            ch = rx_buffer_[rx_idx_++];
            return true;
        }
        return false;
    }

    size_t read(std::span<uint8_t> buffer) override {
        size_t count = 0;
        while (count < buffer.size() && rx_idx_ < rx_buffer_.size()) {
            buffer[count++] = rx_buffer_[rx_idx_++];
        }
        return count;
    }

    void flush() override {}

    void queue_rx_bytes(const std::vector<uint8_t>& bytes) {
        rx_buffer_ = bytes;
        rx_idx_ = 0;
    }

protected:
    void start_hardware_transfer_from_isr(const hal::UartTxRequest& req) noexcept override {
        (void)req;
    }

private:
    std::vector<uint8_t> rx_buffer_{};
    size_t rx_idx_{0};
};

static std::vector<uint8_t> build_ubx_nav_pvt_packet(
    int32_t lat_1e7, int32_t lon_1e7, int32_t alt_mm,
    int32_t gspeed_mm_s, int32_t heading_1e5, uint8_t sats, uint8_t fix_type) {
    
    std::vector<uint8_t> pkt;
    pkt.push_back(0xB5); // SYNC 1
    pkt.push_back(0x62); // SYNC 2
    pkt.push_back(0x01); // CLASS: NAV
    pkt.push_back(0x07); // ID: PVT
    
    uint16_t len = 92; // Standard NAV-PVT payload length
    pkt.push_back(static_cast<uint8_t>(len & 0xFF));
    pkt.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));

    // 92-byte payload buffer
    std::vector<uint8_t> payload(92, 0);
    // iTOW at offset 0 (uint32)
    uint32_t itow = 3600000;
    std::memcpy(&payload[0], &itow, 4);

    // fixType at offset 20 (uint8)
    payload[20] = fix_type;
    // numSV at offset 23 (uint8)
    payload[23] = sats;
    // lon at offset 24 (int32)
    std::memcpy(&payload[24], &lon_1e7, 4);
    // lat at offset 28 (int32)
    std::memcpy(&payload[28], &lat_1e7, 4);
    // hMSL (alt) at offset 36 (int32)
    std::memcpy(&payload[36], &alt_mm, 4);
    // gSpeed at offset 60 (int32)
    std::memcpy(&payload[60], &gspeed_mm_s, 4);
    // headMot at offset 64 (int32)
    std::memcpy(&payload[64], &heading_1e5, 4);

    pkt.insert(pkt.end(), payload.begin(), payload.end());

    // Calculate 8-bit Fletcher checksum over CLASS, ID, LENGTH, and PAYLOAD
    uint8_t ck_a = 0;
    uint8_t ck_b = 0;
    for (size_t i = 2; i < pkt.size(); ++i) {
        ck_a += pkt[i];
        ck_b += ck_a;
    }

    pkt.push_back(ck_a);
    pkt.push_back(ck_b);
    return pkt;
}

TEST_GROUP(SitlGpsTestGroup) {
    MockUart* uart{nullptr};
    UbloxGps* gps{nullptr};

    void setup() {
        uart = new MockUart();
        gps = new UbloxGps(*uart);
        mock().clear();
    }

    void teardown() {
        mock().checkExpectations();
        mock().clear();
        delete gps;
        delete uart;
    }
};

TEST(SitlGpsTestGroup, VerifyUbxPvtChecksumAndParsingCalculations) {
    // 37.7749 N, -122.4194 W (San Francisco), Alt 120m, Speed 15 m/s, Heading 90 deg, 14 Sats, 3D Fix
    auto pkt = build_ubx_nav_pvt_packet(377749000, -1224194000, 120000, 15000, 9000000, 14, 3);

    GpsFix fix{};
    bool complete = false;
    for (uint8_t b : pkt) {
        if (gps->feed_byte(b, fix)) {
            complete = true;
        }
    }

    CHECK_TRUE(complete);
    CHECK_TRUE(fix.valid);
    LONGS_EQUAL(377749000, fix.lat_1e7);
    LONGS_EQUAL(-1224194000, fix.lon_1e7);
    LONGS_EQUAL(120000, fix.alt_msl_mm);
    LONGS_EQUAL(15000, fix.ground_speed_mm_s);
    LONGS_EQUAL(9000000, fix.heading_1e5);
    LONGS_EQUAL(14, fix.satellites);
    CHECK_EQUAL(static_cast<uint8_t>(GpsFixType::Fix3D), static_cast<uint8_t>(fix.fix_type));
}

TEST(SitlGpsTestGroup, VerifyCorruptChecksumRejection) {
    auto pkt = build_ubx_nav_pvt_packet(377749000, -1224194000, 120000, 15000, 9000000, 14, 3);
    // Invert checksum byte
    pkt[pkt.size() - 1] ^= 0xFF;

    GpsFix fix{};
    bool complete = false;
    for (uint8_t b : pkt) {
        if (gps->feed_byte(b, fix)) {
            complete = true;
        }
    }

    CHECK_FALSE(complete);
    CHECK_FALSE(fix.valid);
}

TEST(SitlGpsTestGroup, VerifyGpsTlpEncapsulation) {
    GpsFix fix{};
    fix.lat_1e7 = 476062000;
    fix.lon_1e7 = -1223321000;
    fix.alt_msl_mm = 50000;
    fix.ground_speed_mm_s = 20000;
    fix.heading_1e5 = 27000000;
    fix.satellites = 20;
    fix.fix_type = GpsFixType::Fix3D;
    fix.timestamp_us = 2000000ULL;
    fix.valid = true;

    Tlp64 tlp = UbloxGps::to_tlp(fix);

        CHECK_EQUAL(static_cast<uint8_t>(Channel::Telemetry), static_cast<uint8_t>(tlp.channel()));
    CHECK_EQUAL(TLP_TAG_GPS, tlp.tag());
}

TEST(SitlGpsTestGroup, VerifyInitAsyncBaudrate) {
    mock().expectOneCall("init").withParameter("baudrate", 115200);

    auto task = gps->init_async(115200);
    while (!task.done()) {
        task.resume();
    }

    CHECK_TRUE(task.done());
}
