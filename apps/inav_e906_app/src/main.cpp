/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * inav_e906_app - Dedicated XuanTie E906 C++ Hardware Co-Processor for INAV
 *
 * Hardware Peripherals:
 *   - SPI1 @ 10 MHz: ICM-42688-P 6-DoF IMU (1 kHz burst streaming)
 *   - SPI1 Dual-I/O: Companion FPGA Hardware Interface (Dual SPI)
 *   - TWI1 @ 400 kHz: HMC5883L / QMC5883L 3D Magnetometer (100 Hz)
 *   - UART3 @ 115200: u-blox NEO-M8N GPS (10 Hz)
 *   - VirtIO RPMsg (abstractx-tlp): Lock-free 64B TLP IPC to Linux SBC (inav_linux_sbc)
 */
// @impl [SPEC-APP-01] [SPEC-ARCH-03] targets/allwinner_e906/SPECIFICATION.md

#include "inav_sensor_coprocessor.hpp"
#include "applib/drivers/imu/icm42688p.hpp"
#include "applib/drivers/mag/compass.hpp"
#include "applib/drivers/gps/ublox_gps.hpp"
#include <cstdio>
#include <cstdlib>

using namespace inav::e906;

extern "C" {
    void trace_init(void);
    void trace_puts(const char *s);
    extern const struct rpmsg_resource_table global_resource_table;
}

static void put_uint(uint32_t val) {
    char buf[12];
    int idx = 0;
    if (val == 0) {
        trace_puts("0");
        return;
    }
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = idx - 1; i >= 0; i--) {
        char c[2] = {buf[i], '\0'};
        trace_puts(c);
    }
}

// Minimal NMEA Lat/Lon parser
static double parse_nmea_coord(const char* str, char hemi) {
    if (!str || str[0] == '\0') return 0.0;
    const char* dot = std::strchr(str, '.');
    if (!dot) return 0.0;
    int dot_idx = static_cast<int>(dot - str);
    int deg_len = dot_idx - 2;
    if (deg_len <= 0) return 0.0;

    char deg_buf[8] = {0};
    std::memcpy(deg_buf, str, static_cast<size_t>(deg_len));
    double deg = std::atof(deg_buf);
    double mins = std::atof(str + deg_len);
    double res = deg + (mins / 60.0);
    if (hemi == 'S' || hemi == 'W') res = -res;
    return res;
}

int main(void) {
    // 1. Remoteproc shared counters
    volatile uint32_t *sram_counter = (volatile uint32_t *)0x40000004;
    volatile uint32_t *dram_counter = (volatile uint32_t *)0x4E010004;

    // 2. Initialize Remoteproc Trace Buffer & Hardware Timer
    trace_init();
    hal::Timer::init();

    trace_puts("================================================================\n");
    trace_puts("  INAV - XuanTie E906 C++ Hardware Sensor Co-Processor Ready!  \n");
    trace_puts("  Architecture: Bare-metal RV32IMAFC (Zero Python Overhead)     \n");
    trace_puts("  Peripherals: SPI1 (IMU/FPGA), TWI1 (Mag), UART3 (GPS), RPMsg \n");
    trace_puts("================================================================\n");

    // 3. Initialize VirtIO RPMsg & TLP Channel
    static hal::Rpmsg rpmsg;
    static hal::RpmsgTlpChannel tlp_channel{rpmsg};
    rpmsg.init(&global_resource_table);
    tlp_channel.attach();

    // 4. Initialize AbstractX Applib Driver Instances
    static applib::drivers::imu::Icm42688p applib_imu(abstractx::hal::get_spi_driver());
    static applib::drivers::mag::Compass   applib_mag(abstractx::hal::get_i2c_driver(1));
    static applib::drivers::gps::UbloxGps  applib_gps(*abstractx::hal::get_uart_port(3));

    // Initialize Hardware Sensor Interfaces
    bool imu_ok = Icm42688pDriver::init(10000000);
    bool mag_ok = CompassDriver::init();
    bool gps_ok = GpsUartDriver::init(115200);

    trace_puts("[E906 Boot] Hardware Status: IMU=");
    trace_puts(imu_ok ? "OK" : "FAIL");
    trace_puts(", MAG=");
    trace_puts(mag_ok ? "OK" : "FAIL");
    trace_puts(", GPS=");
    trace_puts(gps_ok ? "OK" : "FAIL");
    trace_puts("\n");

    uint16_t seq = 0;
    uint32_t uptime_sec = 0;
    uint32_t imu_count = 0;
    uint32_t mag_count = 0;
    uint32_t gps_count = 0;

    uint64_t next_imu_tick = hal::Timer::get_ticks();
    uint64_t next_mag_tick = hal::Timer::get_ticks();
    uint64_t next_heartbeat_tick = hal::Timer::get_ticks();

    constexpr uint64_t TICKS_1MS  = hal::Timer::TICKS_PER_MS;
    constexpr uint64_t TICKS_10MS = 10ULL * hal::Timer::TICKS_PER_MS;
    constexpr uint64_t TICKS_1SEC = 1000ULL * hal::Timer::TICKS_PER_MS;

    char nmea_buf[128];
    size_t nmea_idx = 0;

    // Active GPS State
    double gps_lat = 0.0;
    double gps_lon = 0.0;
    float  gps_alt = 0.0f;
    float  gps_spd = 0.0f;
    uint8_t gps_sats = 0;
    uint8_t gps_fix = 0;

    // 5. Master Real-Time Execution Loop
    while (1) {
        uint64_t now = hal::Timer::get_ticks();
        uint64_t now_ns = now * 1000ULL; // Approximate nanosecond timestamp

        // A. 1 kHz High-Rate IMU Burst Sampling (ICM-42688-P SPI1)
        if (now >= next_imu_tick) {
            next_imu_tick += TICKS_1MS;
            if (next_imu_tick < now) next_imu_tick = now + TICKS_1MS;

            uint8_t raw_imu[14];
            if (Icm42688pDriver::read_burst(raw_imu)) {
                abstractx::Tlp64 tlp = Icm42688pDriver::format_tlp(raw_imu, ++seq, now_ns);
                tlp_channel.send(tlp);
                imu_count++;
            }
        }

        // B. 100 Hz Medium-Rate Compass Sampling (HMC5883L/QMC5883L TWI1)
        if (now >= next_mag_tick) {
            next_mag_tick += TICKS_10MS;
            if (next_mag_tick < now) next_mag_tick = now + TICKS_10MS;

            uint8_t raw_mag[6];
            if (CompassDriver::read_vector(raw_mag)) {
                abstractx::Tlp64 tlp = CompassDriver::format_tlp(raw_mag, ++seq, now_ns);
                tlp_channel.send(tlp);
                mag_count++;
            }
        }

        // C. Continuous UART3 GPS Ingestion (u-blox NEO-M8N)
        while (GpsUartDriver::has_byte()) {
            uint8_t b = GpsUartDriver::read_byte();
            if (b == '$') {
                nmea_idx = 0;
            }
            if (nmea_idx < sizeof(nmea_buf) - 1) {
                nmea_buf[nmea_idx++] = static_cast<char>(b);
            }
            if (b == '\n') {
                nmea_buf[nmea_idx] = '\0';
                
                // Parse $GNGGA / $GPGGA
                if (std::strncmp(nmea_buf, "$GNGGA", 6) == 0 || std::strncmp(nmea_buf, "$GPGGA", 6) == 0) {
                    char* fields[15];
                    size_t f_idx = 0;
                    char* p = nmea_buf;
                    fields[f_idx++] = p;
                    while (*p && f_idx < 15) {
                        if (*p == ',') {
                            *p = '\0';
                            fields[f_idx++] = p + 1;
                        }
                        p++;
                    }
                    if (f_idx >= 10) {
                        if (fields[2][0] && fields[3][0]) gps_lat = parse_nmea_coord(fields[2], fields[3][0]);
                        if (fields[4][0] && fields[5][0]) gps_lon = parse_nmea_coord(fields[4], fields[5][0]);
                        if (fields[6][0]) gps_fix = static_cast<uint8_t>(std::atoi(fields[6]));
                        if (fields[7][0]) gps_sats = static_cast<uint8_t>(std::atoi(fields[7]));
                        if (fields[9][0]) gps_alt = static_cast<float>(std::atof(fields[9]));

                        // Format 64B TLP Tag 2 (bar::GpsBase)
                        abstractx::Tlp64 tlp{};
                        tlp.wire.flags = 0x10;
                        tlp.wire.tag = 2;
                        tlp.wire.target_address = 0x40005000; // bar::GpsBase
                        tlp.wire.length = 64;
                        tlp.wire.sequence = ++seq;
                        tlp.wire.timestamp_ns = now_ns;

                        int32_t lat_i = static_cast<int32_t>(gps_lat * 1e7);
                        int32_t lon_i = static_cast<int32_t>(gps_lon * 1e7);
                        int32_t alt_i = static_cast<int32_t>(gps_alt * 1000.0f);
                        int32_t spd_i = static_cast<int32_t>(gps_spd * 1000.0f);
                        int32_t head_i = 0;
                        int32_t itow = static_cast<int32_t>((now / hal::Timer::TICKS_PER_MS) % 604800000ULL);

                        uint8_t* pl = tlp.wire.payload;
                        std::memcpy(pl + 0, &itow, 4);
                        std::memcpy(pl + 4, &lat_i, 4);
                        std::memcpy(pl + 8, &lon_i, 4);
                        std::memcpy(pl + 12, &alt_i, 4);
                        std::memcpy(pl + 16, &spd_i, 4);
                        std::memcpy(pl + 20, &head_i, 4);
                        pl[24] = gps_sats;
                        pl[25] = gps_fix;

                        tlp_channel.send(tlp);
                        gps_count++;
                    }
                }
                nmea_idx = 0;
            }
        }

        // D. Flush TLPs and Service VirtIO RPMsg
        tlp_channel.service();
        tlp_channel.flush();
        rpmsg.poll();  // irq-gated: service pending VirtIO RPMsg buffers

        // E. 1 Hz Periodic Heartbeat and Remoteproc Status
        if (now >= next_heartbeat_tick) {
            next_heartbeat_tick += TICKS_1SEC;
            uptime_sec++;
            *sram_counter = uptime_sec;
            *dram_counter = uptime_sec;

            trace_puts("[E906 INAV] Uptime: ");
            put_uint(uptime_sec);
            trace_puts("s | IMU: ");
            put_uint(imu_count);
            trace_puts(" | Mag: ");
            put_uint(mag_count);
            trace_puts(" | GPS: ");
            put_uint(gps_count);
            trace_puts(" | RPMsg: OK\n");
        }
    }

    return 0;
}
