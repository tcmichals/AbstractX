/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * e906_rpmsg_studio_bridge.cpp - Linux C++20 Coroutine Sensor & Studio Bridge
 * ---------------------------------------------------------------------------
 * Implements the Linux A55 C++20 Coroutine side of the split-core gps_imu_app
 * architecture ([SPEC-APP-11], [SPEC-E906-IPC-04]):
 *   - The XuanTie E906 runs a sensor-agnostic generic I/O processor (IoEngine)
 *     handling SPI, I2C, Serial/UART, GPIO, and selectable Auto-DMA.
 *   - This Linux C++20 coroutine application drives the sensors over RPMsg
 *     endpoint 1024 ("abstractx-tlp") using CoroutineIoEngine:
 *       1. compass_coro_task : I2C bridge TLPs (HMC5883L 0x1E / QMC5883L 0x0D),
 *          decodes X/Y/Z and magnetic heading on Linux -> Tag 3 TLP (0x40003000)
 *       2. imu_coro_task     : SPI bridge TLPs (ICM-42688-P on SPI1) + selectable
 *          Auto-DMA (ASP_TLP_TYPE_DMA_CFG 0x11 on PI8 DRDY pin 264) or standard
 *          polled SPI TLP calls; decodes accel/gyro/temp/roll/pitch on Linux ->
 *          Tag 1 IMU TLP (0x40001000) & Tag 5 Attitude TLP (0x40000100)
 *       3. gps_coro_task     : Serial bridge TLPs (u-blox NEO-M8N on UART3 @
 *          115200 baud, 10 Hz), validates NMEA checksums and parses $GNGGA /
 *          $GNRMC on Linux -> Tag 2 GPS TLP (0x40005000)
 *   - Forwards all synthesized 64-byte Studio TLPs and E906 CTF 1.8 trace
 *     packets ("abstractx-trace", endpoint 1025) to AbstractX Studio on UDP :9870.
 *   - Listens on UDP :9872 for Studio's live Auto-DMA toggle (0x31 TLP) to
 *     dynamically switch between PI8 DRDY Auto-DMA and polled SPI TLP calls.
 */

// @impl [SPEC-APP-11] [SPEC-E906-IPC-04] [SPEC-E906-IPC-05] apps/gps_imu_app/SPECIFICATION.md#spec-app-11 targets/allwinner_e906/SPECIFICATION.md

#include "abstractx/abstractx.hpp"
#include "asp_tlp_msg.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

namespace {

using abstractx::Channel;
using abstractx::MpscTlpRing;
using abstractx::Tlp64;
using abstractx::TlpType;
using abstractx::coro::CoroutineIoEngine;
using abstractx::coro::Task;

constexpr size_t kTlpBytes = 64;
constexpr size_t kMaxTlpsPerMsg = 7;
constexpr uint32_t kTlpEndpointAddr = 1024;
constexpr uint32_t kTraceEndpointAddr = 1025;

struct RpmsgEndpointInfo {
    char name[32];
    uint32_t src;
    uint32_t dst;
};

constexpr unsigned long kRpmsgCreateEptIoctl =
    (1UL << 30) | (sizeof(RpmsgEndpointInfo) << 16) | (0xB5UL << 8) | 0x1UL;

volatile sig_atomic_t g_stop = 0;

void on_signal(int) {
    g_stop = 1;
}

uint64_t monotonic_ns() {
    struct timespec ts {};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
           static_cast<uint64_t>(ts.tv_nsec);
}

uint64_t monotonic_ms() {
    return monotonic_ns() / 1000000ULL;
}

void poll_delay_ms(int ms) {
    (void)poll(nullptr, 0, ms);
}

bool find_rpmsg_node(const char* target_name, char* out_dev, size_t out_cap) {
    if (out_cap > 0) {
        out_dev[0] = '\0';
    }
    DIR* dir = opendir("/sys/class/rpmsg");
    if (!dir) {
        return false;
    }
    struct dirent* ent = nullptr;
    while ((ent = readdir(dir)) != nullptr) {
        if (std::strncmp(ent->d_name, "rpmsg", 5) != 0) {
            continue;
        }
        char name_path[320]{};
        std::snprintf(name_path, sizeof(name_path), "/sys/class/rpmsg/%s/name", ent->d_name);
        int fd = open(name_path, O_RDONLY);
        if (fd < 0) {
            continue;
        }
        char buf[64]{};
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) {
            continue;
        }
        while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r' || buf[n - 1] == ' ')) {
            buf[--n] = '\0';
        }
        if (std::strcmp(buf, target_name) == 0) {
            std::snprintf(out_dev, out_cap, "/dev/%.48s", ent->d_name);
            closedir(dir);
            return true;
        }
    }
    closedir(dir);
    return false;
}

bool ensure_endpoint(const char* ctrl_path, const char* name, uint32_t addr, char* out_dev, size_t out_cap) {
    if (find_rpmsg_node(name, out_dev, out_cap)) {
        return true;
    }
    RpmsgEndpointInfo info{};
    std::strncpy(info.name, name, sizeof(info.name) - 1);
    info.src = addr;
    info.dst = addr;

    int ctrl_fd = open(ctrl_path, O_RDWR);
    if (ctrl_fd >= 0) {
        (void)ioctl(ctrl_fd, kRpmsgCreateEptIoctl, &info);
        close(ctrl_fd);
    }

    const uint64_t deadline = monotonic_ms() + 2000ULL;
    while (monotonic_ms() < deadline) {
        if (find_rpmsg_node(name, out_dev, out_cap)) {
            poll_delay_ms(50);
            return true;
        }
        poll_delay_ms(10);
    }
    return false;
}

bool valid_tlp_batch(ssize_t len) {
    return len > 0 &&
           static_cast<size_t>(len) <= (kTlpBytes * kMaxTlpsPerMsg) &&
           (static_cast<size_t>(len) % kTlpBytes) == 0;
}

void send_hello(int fd) {
    if (fd >= 0) {
        const char hello[] = "helo";
        ssize_t rc = write(fd, hello, 4);
        (void)rc;
    }
}

// ============================================================================
// Shared Linux Coroutine State & Studio UDP Egress
// ============================================================================

MpscTlpRing<> g_linux_tx_ring; // Linux Coroutines -> E906 RPMsg 1024
MpscTlpRing<> g_linux_rx_ring; // E906 RPMsg 1024 -> Linux Coroutines
CoroutineIoEngine g_coro_io(g_linux_tx_ring, g_linux_rx_ring);

int g_out_sock = -1;
struct sockaddr_in g_studio_addr {};
struct sockaddr_in g_local_studio_addr {};
bool g_send_local_copy = false;

std::atomic<bool> g_auto_dma_enabled{true};
std::atomic<bool> g_auto_dma_dirty{true};
uint16_t g_studio_seq = 0;

struct SharedNavState {
    float roll_deg{0.0f};
    float pitch_deg{0.0f};
    float heading_deg{0.0f};
    double gps_lat{0.0};
    double gps_lon{0.0};
    float gps_alt_m{0.0f};
    float gps_spd_mps{0.0f};
    uint8_t gps_sats{0};
    uint8_t gps_fix{0};
} g_nav;

void emit_studio_tlp(const Tlp64& tlp) {
    if (g_out_sock < 0) {
        return;
    }
    (void)sendto(g_out_sock, &tlp, sizeof(tlp), 0,
                 reinterpret_cast<const struct sockaddr*>(&g_studio_addr), sizeof(g_studio_addr));
    if (g_send_local_copy) {
        (void)sendto(g_out_sock, &tlp, sizeof(tlp), 0,
                     reinterpret_cast<const struct sockaddr*>(&g_local_studio_addr), sizeof(g_local_studio_addr));
    }
}

inline void pack_le16(uint8_t* dst, int16_t v) noexcept {
    const auto u = static_cast<uint16_t>(v);
    dst[0] = static_cast<uint8_t>(u & 0xFFU);
    dst[1] = static_cast<uint8_t>(u >> 8);
}

inline void pack_u16_le(uint8_t* dst, uint16_t u) noexcept {
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

inline int16_t unpack_le16(const uint8_t* src) noexcept {
    return static_cast<int16_t>(static_cast<uint16_t>(src[0]) | (static_cast<uint16_t>(src[1]) << 8));
}

void emit_attitude_tag5(uint16_t seq, uint64_t ts_ns) {
    Tlp64 att{};
    att.wire.type           = static_cast<uint8_t>(TlpType::DmaStream);
    att.wire.flags          = 0;
    att.wire.tag            = 5;
    att.wire.channel        = static_cast<uint8_t>(Channel::Telemetry);
    att.wire.target_address = 0x40000100U;
    att.wire.length_dw      = 5; // 20 bytes
    att.wire.sequence       = seq;
    att.wire.timestamp_ns   = ts_ns;

    const auto roll_cdeg  = static_cast<int16_t>(g_nav.roll_deg * 100.0f);
    const auto pitch_cdeg = static_cast<int16_t>(g_nav.pitch_deg * 100.0f);
    int32_t yaw_i = static_cast<int32_t>(g_nav.heading_deg * 100.0f);
    if (yaw_i < 0) yaw_i = 0;
    if (yaw_i > 36000) yaw_i = 36000;
    const auto yaw_cdeg   = static_cast<uint16_t>(yaw_i);
    const auto alt_mm     = static_cast<int32_t>(g_nav.gps_alt_m * 1000.0f);
    int32_t spd_i = static_cast<int32_t>(g_nav.gps_spd_mps * 100.0f);
    if (spd_i < 0) spd_i = 0;
    if (spd_i > 65535) spd_i = 65535;
    const auto spd_cm_s   = static_cast<uint16_t>(spd_i);

    pack_le16(&att.wire.payload[0], roll_cdeg);
    pack_le16(&att.wire.payload[2], pitch_cdeg);
    pack_u16_le(&att.wire.payload[4], yaw_cdeg);
    pack_le32(&att.wire.payload[6], alt_mm);
    pack_u16_le(&att.wire.payload[10], spd_cm_s);
    pack_u16_le(&att.wire.payload[12], 500);
    pack_u16_le(&att.wire.payload[14], 500);
    pack_u16_le(&att.wire.payload[16], 500);
    pack_u16_le(&att.wire.payload[18], 500);
    att.wire.crc32 = 0xEFBEADDEU;
    emit_studio_tlp(att);
}

void decode_and_emit_imu(const uint8_t* raw14, bool auto_dma_flag) {
    if (!raw14) return;
    bool all_zero = true;
    bool all_ff = true;
    for (int i = 0; i < 14; ++i) {
        if (raw14[i] != 0x00) all_zero = false;
        if (raw14[i] != 0xFF) all_ff = false;
    }
    if (all_zero || all_ff) return;

    const int16_t ax_raw = unpack_be16(&raw14[2]);
    const int16_t ay_raw = unpack_be16(&raw14[4]);
    const int16_t az_raw = unpack_be16(&raw14[6]);
    const float ax_g = static_cast<float>(ax_raw) / 2048.0f;
    const float ay_g = static_cast<float>(ay_raw) / 2048.0f;
    const float az_g = static_cast<float>(az_raw) / 2048.0f;

    g_nav.roll_deg  = std::atan2(ay_g, (az_g != 0.0f) ? az_g : 1.0f) * (180.0f / 3.14159265f);
    g_nav.pitch_deg = std::atan2(-ax_g, std::sqrt(ay_g * ay_g + az_g * az_g)) * (180.0f / 3.14159265f);

    const uint16_t seq = ++g_studio_seq;
    const uint64_t ts_ns = monotonic_ns();

    Tlp64 imu_tlp{};
    imu_tlp.wire.type           = static_cast<uint8_t>(TlpType::DmaStream);
    imu_tlp.wire.flags          = auto_dma_flag ? 0x01U : 0x00U;
    imu_tlp.wire.tag            = 1;
    imu_tlp.wire.channel        = static_cast<uint8_t>(Channel::Telemetry);
    imu_tlp.wire.target_address = 0x40001000U;
    imu_tlp.wire.length_dw      = 8; // 30 bytes
    imu_tlp.wire.sequence       = seq;
    imu_tlp.wire.timestamp_ns   = ts_ns;
    std::memcpy(imu_tlp.wire.payload, raw14, 14);
    imu_tlp.wire.payload[14] = 1; // ICM42688P
    imu_tlp.wire.payload[15] = 0;
    for (int i = 0; i < 7; ++i) {
        pack_le16(&imu_tlp.wire.payload[16 + i * 2], unpack_be16(&raw14[i * 2]));
    }
    imu_tlp.wire.crc32 = 0xEFBEADDEU;
    emit_studio_tlp(imu_tlp);
    emit_attitude_tag5(seq, ts_ns);
}

// ============================================================================
// 1. Linux C++20 Compass Coroutine (Generic I2C Bridge TLPs -> E906)
// ============================================================================

Task<bool> i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
    const uint8_t tx[1] = {val};
    Tlp64 req = Tlp64::make_i2c_write_reg(
        1 /* TWI1 */, addr, reg,
        std::span<const uint8_t>(tx, 1), 0, Channel::I2cBridge);
    Tlp64 cpl = co_await g_coro_io.async_transact(req);
    abstractx::TlpI2cView view(cpl);
    const auto* hdr = view.cpl_header();
    co_return hdr && hdr->status == ASP_STATUS_OK;
}

Task<bool> i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t* out, uint8_t len) {
    Tlp64 req = Tlp64::make_i2c_read_reg(
        1 /* TWI1 */, addr, reg, len, 0, Channel::I2cBridge);
    Tlp64 cpl = co_await g_coro_io.async_transact(req);
    abstractx::TlpI2cView view(cpl);
    const auto* hdr = view.cpl_header();
    const auto rx = view.rx_data();
    if (!hdr || hdr->status != ASP_STATUS_OK || rx.size() < len) {
        co_return false;
    }
    std::memcpy(out, rx.data(), len);
    co_return true;
}

Task<void> compass_coro_task() {
    auto& timer = abstractx::hal::get_timer_driver();
    constexpr uint8_t kAddrHmc = 0x1E;
    constexpr uint8_t kAddrQmc = 0x0D;
    uint8_t mag_addr = kAddrQmc;
    uint8_t chip_code = 2; // 1 = HMC5883L, 2 = QMC5883L

    uint8_t id_bytes[3]{};
    if ((co_await i2c_read_regs(kAddrHmc, 0x0A, id_bytes, 3)) &&
        id_bytes[0] == 'H' && id_bytes[1] == '4' && id_bytes[2] == '3') {
        mag_addr = kAddrHmc;
        chip_code = 1;
        (void)co_await i2c_write_reg(kAddrHmc, 0x00, 0x70);
        (void)co_await i2c_write_reg(kAddrHmc, 0x01, 0x20);
        (void)co_await i2c_write_reg(kAddrHmc, 0x02, 0x00);
    } else {
        (void)co_await i2c_write_reg(kAddrQmc, 0x0B, 0x01);
        (void)co_await i2c_write_reg(kAddrQmc, 0x09, 0x1D);
    }

    while (!g_stop) {
        uint8_t raw6[6]{};
        const uint8_t reg = (mag_addr == kAddrHmc) ? 0x03U : 0x00U;
        if (co_await i2c_read_regs(mag_addr, reg, raw6, 6)) {
            int16_t x = 0, y = 0, z = 0;
            if (chip_code == 1) {
                // HMC5883L: X, Z, Y big-endian
                x = unpack_be16(&raw6[0]);
                z = unpack_be16(&raw6[2]);
                y = unpack_be16(&raw6[4]);
            } else {
                // QMC5883L: X, Y, Z little-endian
                x = unpack_le16(&raw6[0]);
                y = unpack_le16(&raw6[2]);
                z = unpack_le16(&raw6[4]);
            }
            float hdg = std::atan2(static_cast<float>(y), static_cast<float>(x)) * (180.0f / 3.14159265f);
            if (hdg < 0.0f) hdg += 360.0f;
            g_nav.heading_deg = hdg;

            const uint16_t seq = ++g_studio_seq;
            Tlp64 mag_tlp{};
            mag_tlp.wire.type           = static_cast<uint8_t>(TlpType::DmaStream);
            mag_tlp.wire.flags          = 0;
            mag_tlp.wire.tag            = 3;
            mag_tlp.wire.channel        = static_cast<uint8_t>(Channel::Telemetry);
            mag_tlp.wire.target_address = 0x40003000U;
            mag_tlp.wire.length_dw      = 4; // 14 bytes
            mag_tlp.wire.sequence       = seq;
            mag_tlp.wire.timestamp_ns   = monotonic_ns();
            std::memcpy(mag_tlp.wire.payload, raw6, 6);
            mag_tlp.wire.payload[6] = chip_code;
            mag_tlp.wire.payload[7] = 0;
            pack_le16(&mag_tlp.wire.payload[8], x);
            pack_le16(&mag_tlp.wire.payload[10], y);
            pack_le16(&mag_tlp.wire.payload[12], z);
            mag_tlp.wire.crc32 = 0xEFBEADDEU;
            emit_studio_tlp(mag_tlp);
        }
        (void)co_await timer.sleep_ms_async(50);
    }
}

// ============================================================================
// 2. Linux C++20 IMU Coroutine (Selectable Auto-DMA PI8 DRDY or Polled SPI1)
// ============================================================================

Task<bool> spi_write_reg(uint8_t reg, uint8_t val) {
    const uint8_t tx[2] = {static_cast<uint8_t>(reg & 0x7FU), val};
    Tlp64 req = Tlp64::make_spi_transfer(
        1 /* SPI1 */, 0 /* CS0 */,
        std::span<const uint8_t>(tx, 2), 2, ASP_SPI_FLAG_AUTO_CS, 0, Channel::SpiBridge);
    Tlp64 cpl = co_await g_coro_io.async_transact(req);
    abstractx::TlpSpiView view(cpl);
    const auto* hdr = view.cpl_header();
    co_return hdr && hdr->status == ASP_STATUS_OK;
}

Task<void> configure_e906_auto_dma(bool enable) {
    (void)co_await g_coro_io.async_transact(Tlp64::make_hw_fusion_control(0, enable));
    co_return;
}

Task<void> imu_coro_task() {
    auto& timer = abstractx::hal::get_timer_driver();

    // Ensure Auto-DMA is disabled during SPI1 register setup
    co_await configure_e906_auto_dma(false);

    // Initialize ICM-42688-P over generic SPI bridge TLPs:
    // INT_CONFIG (0x14) = 0x03 (INT1 push-pull, active-high), INT_SOURCE0 (0x65) = 0x08 (UI_DRDY_INT1_EN),
    // PWR_MGMT0 (0x4E) = 0x0F (6-axis Low-Noise mode)
    (void)co_await spi_write_reg(0x14, 0x03);
    (void)co_await spi_write_reg(0x64, 0x00);
    (void)co_await spi_write_reg(0x65, 0x08);
    (void)co_await spi_write_reg(0x4E, 0x0F);
    (void)co_await timer.sleep_ms_async(20);

    bool active_dma = false;
    g_auto_dma_dirty.store(true, std::memory_order_release);

    while (!g_stop) {
        const bool want_dma = g_auto_dma_enabled.load(std::memory_order_acquire);
        if (g_auto_dma_dirty.exchange(false, std::memory_order_acq_rel) || want_dma != active_dma) {
            co_await configure_e906_auto_dma(want_dma);
            active_dma = want_dma;
        }

        if (active_dma) {
            // Auto-DMA mode: await autonomous 14-byte DRDY SPI1 burst TLPs from E906
            Tlp64 stream_tlp = co_await g_coro_io.async_await_stream(Channel::Telemetry);
            const auto raw = stream_tlp.payload();
            if (stream_tlp.tag() == 1 && raw.size() >= 14) {
                decode_and_emit_imu(raw.data(), true);
            }
        } else {
            // Standard Polled SPI TLP mode: request 15-byte transfer (0x9D + 14B) over RPMsg
            uint8_t tx[15]{};
            tx[0] = 0x9D;
            Tlp64 req = Tlp64::make_spi_transfer(
                1 /* SPI1 */, 0 /* CS0 */,
                std::span<const uint8_t>(tx, 15), 15, ASP_SPI_FLAG_AUTO_CS, 0, Channel::SpiBridge);
            Tlp64 cpl = co_await g_coro_io.async_transact(req);
            abstractx::TlpSpiView view(cpl);
            const auto* hdr = view.cpl_header();
            const auto rx = view.rx_data();
            if (hdr && hdr->status == ASP_STATUS_OK && rx.size() >= 15) {
                decode_and_emit_imu(rx.data() + 1, false);
            }
            (void)co_await timer.sleep_ms_async(20);
        }
    }
    co_return;
}

// ============================================================================
// 3. Linux C++20 GPS Coroutine (Generic Serial Bridge TLPs -> E906 UART3)
// ============================================================================

Task<void> serial_set_baud(uint32_t baud) {
    Tlp64 req{};
    req.wire.type           = ASP_TLP_TYPE_MEM_WR;
    req.wire.channel        = static_cast<uint8_t>(Channel::EscSerial);
    req.wire.target_address = 0x40000504U;
    req.wire.length_dw      = 1;
    pack_le32(req.wire.payload, static_cast<int32_t>(baud));
    (void)co_await g_coro_io.async_transact(req);
    co_return;
}

Task<void> serial_write_bytes(const uint8_t* data, size_t len) {
    size_t off = 0;
    while (off < len) {
        const size_t chunk = (len - off > 36U) ? 36U : (len - off);
        Tlp64 req{};
        req.wire.type           = ASP_TLP_TYPE_MEM_WR;
        req.wire.channel        = static_cast<uint8_t>(Channel::EscSerial);
        req.wire.target_address = 0x40000500U;
        req.wire.length_dw      = static_cast<uint16_t>(chunk);
        std::memcpy(req.wire.payload, data + off, chunk);
        (void)co_await g_coro_io.async_transact(req);
        off += chunk;
    }
    co_return;
}

bool nmea_checksum_ok(const char* line, size_t len) {
    if (len < 6 || line[0] != '$') return false;
    size_t star = 0;
    for (size_t i = 1; i < len; ++i) {
        if (line[i] == '*') {
            star = i;
            break;
        }
    }
    if (star == 0 || star + 2 >= len) return false;
    uint8_t calc = 0;
    for (size_t i = 1; i < star; ++i) {
        calc ^= static_cast<uint8_t>(line[i]);
    }
    char hex_str[3] = {line[star + 1], line[star + 2], '\0'};
    char* endp = nullptr;
    const unsigned long expected = std::strtoul(hex_str, &endp, 16);
    if (!endp || *endp != '\0') return false;
    return calc == static_cast<uint8_t>(expected);
}

double parse_nmea_coord(const char* val, char hemi) {
    if (!val || !*val) return 0.0;
    const char* dot = std::strchr(val, '.');
    if (!dot || (dot - val) <= 2) return 0.0;
    const size_t deg_len = static_cast<size_t>((dot - val) - 2);
    char deg_buf[8]{};
    if (deg_len >= sizeof(deg_buf)) return 0.0;
    std::memcpy(deg_buf, val, deg_len);
    const double deg = std::atof(deg_buf);
    const double mins = std::atof(val + deg_len);
    double res = deg + mins / 60.0;
    if (hemi == 'S' || hemi == 'W') res = -res;
    return res;
}

void parse_nmea_sentence(const char* s, size_t len) {
    if (!nmea_checksum_ok(s, len)) return;
    char buf[128]{};
    const size_t copy_n = (len < sizeof(buf) - 1) ? len : (sizeof(buf) - 1);
    std::memcpy(buf, s, copy_n);

    char* fields[20]{};
    size_t nf = 0;
    char* p = buf;
    while (p && nf < 20) {
        fields[nf++] = p;
        char* comma = std::strchr(p, ',');
        if (!comma) break;
        *comma = '\0';
        p = comma + 1;
    }

    if (nf >= 10 && (std::strncmp(fields[0], "$GNGGA", 6) == 0 || std::strncmp(fields[0], "$GPGGA", 6) == 0)) {
        if (*fields[2] && *fields[3]) g_nav.gps_lat = parse_nmea_coord(fields[2], fields[3][0]);
        if (*fields[4] && *fields[5]) g_nav.gps_lon = parse_nmea_coord(fields[4], fields[5][0]);
        if (*fields[6]) g_nav.gps_fix = static_cast<uint8_t>(std::atoi(fields[6]));
        if (*fields[7]) g_nav.gps_sats = static_cast<uint8_t>(std::atoi(fields[7]));
        if (*fields[9]) g_nav.gps_alt_m = static_cast<float>(std::atof(fields[9]));
    } else if (nf >= 8 && (std::strncmp(fields[0], "$GNRMC", 6) == 0 || std::strncmp(fields[0], "$GPRMC", 6) == 0)) {
        if (*fields[7]) g_nav.gps_spd_mps = static_cast<float>(std::atof(fields[7]) * 0.514444);
    }
}

Task<void> gps_coro_task() {
    auto& timer = abstractx::hal::get_timer_driver();

    co_await serial_set_baud(9600);
    (void)co_await timer.sleep_ms_async(40);

    static const uint8_t kUbxPrt115200[] = {
        0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00,
        0xD0, 0x08, 0x00, 0x00, 0x00, 0xC2, 0x01, 0x00, 0x07, 0x00,
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x7A
    };
    co_await serial_write_bytes(kUbxPrt115200, sizeof(kUbxPrt115200));
    (void)co_await timer.sleep_ms_async(40);
    co_await serial_set_baud(115200);
    (void)co_await timer.sleep_ms_async(40);

    static const uint8_t kUbxRate10Hz[] = {
        0xB5, 0x62, 0x06, 0x08, 0x06, 0x00, 0x64, 0x00, 0x01, 0x00, 0x01, 0x00, 0x7A, 0x12
    };
    co_await serial_write_bytes(kUbxRate10Hz, sizeof(kUbxRate10Hz));

    char nmea_acc[512]{};
    size_t acc_len = 0;

    while (!g_stop) {
        Tlp64 req{};
        req.wire.type           = ASP_TLP_TYPE_MEM_RD;
        req.wire.channel        = static_cast<uint8_t>(Channel::EscSerial);
        req.wire.target_address = 0x40000500U;
        req.wire.length_dw      = 40;
        Tlp64 cpl = co_await g_coro_io.async_transact(req);
        const size_t n = cpl.wire.length_dw <= 40U ? cpl.wire.length_dw : 40U;
        for (size_t i = 0; i < n; ++i) {
            const char c = static_cast<char>(cpl.wire.payload[i]);
            if (c == '\n') {
                while (acc_len > 0 && (nmea_acc[acc_len - 1] == '\r' || nmea_acc[acc_len - 1] == ' ')) {
                    --acc_len;
                }
                nmea_acc[acc_len] = '\0';
                parse_nmea_sentence(nmea_acc, acc_len);
                acc_len = 0;
            } else if (acc_len + 1 < sizeof(nmea_acc)) {
                nmea_acc[acc_len++] = c;
            } else {
                acc_len = 0;
            }
        }

        const uint16_t seq = ++g_studio_seq;
        Tlp64 gps_tlp{};
        gps_tlp.wire.type           = static_cast<uint8_t>(TlpType::DmaStream);
        gps_tlp.wire.flags          = 0;
        gps_tlp.wire.tag            = 2;
        gps_tlp.wire.channel        = static_cast<uint8_t>(Channel::Telemetry);
        gps_tlp.wire.target_address = 0x40005000U;
        gps_tlp.wire.length_dw      = 7; // 26 bytes
        gps_tlp.wire.sequence       = seq;
        gps_tlp.wire.timestamp_ns   = monotonic_ns();

        const auto itow   = static_cast<int32_t>(monotonic_ms() % 604800000ULL);
        const auto lat_i  = static_cast<int32_t>(g_nav.gps_lat * 1e7);
        const auto lon_i  = static_cast<int32_t>(g_nav.gps_lon * 1e7);
        const auto alt_i  = static_cast<int32_t>(g_nav.gps_alt_m * 1000.0f);
        const auto spd_i  = static_cast<int32_t>(g_nav.gps_spd_mps * 1000.0f);
        const auto head_i = static_cast<int32_t>(g_nav.heading_deg * 100000.0f);
        pack_le32(&gps_tlp.wire.payload[0], itow);
        pack_le32(&gps_tlp.wire.payload[4], lat_i);
        pack_le32(&gps_tlp.wire.payload[8], lon_i);
        pack_le32(&gps_tlp.wire.payload[12], alt_i);
        pack_le32(&gps_tlp.wire.payload[16], spd_i);
        pack_le32(&gps_tlp.wire.payload[20], head_i);
        gps_tlp.wire.payload[24] = g_nav.gps_sats;
        gps_tlp.wire.payload[25] = g_nav.gps_fix;
        gps_tlp.wire.crc32 = 0xEFBEADDEU;
        emit_studio_tlp(gps_tlp);

        (void)co_await timer.sleep_ms_async(100);
    }
}

} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const char* ctrl_path = "/dev/rpmsg_ctrl0";
    const char* host_ip = std::getenv("STUDIO_HOST_IP");
    if (!host_ip || !*host_ip) {
        host_ip = "127.0.0.1";
    }
    int studio_port = 9870;
    if (const char* env_port = std::getenv("STUDIO_PORT")) {
        int p = std::atoi(env_port);
        if (p > 0 && p < 65536) {
            studio_port = p;
        }
    }
    int listen_port = 9872;
    bool exit_on_stdin_eof = (std::getenv("EXIT_ON_STDIN_EOF") != nullptr &&
                              std::strcmp(std::getenv("EXIT_ON_STDIN_EOF"), "1") == 0);

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--ctrl") == 0 && i + 1 < argc) {
            ctrl_path = argv[++i];
        } else if (std::strcmp(argv[i], "--udp-host") == 0 && i + 1 < argc) {
            host_ip = argv[++i];
        } else if (std::strcmp(argv[i], "--udp-port") == 0 && i + 1 < argc) {
            studio_port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--listen-port") == 0 && i + 1 < argc) {
            listen_port = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--exit-on-stdin-eof") == 0) {
            exit_on_stdin_eof = true;
        } else if (std::strcmp(argv[i], "--polled") == 0) {
            g_auto_dma_enabled.store(false, std::memory_order_release);
        }
    }

    char tlp_node[64]{};
    char trace_node[64]{};
    const bool has_tlp = ensure_endpoint(ctrl_path, "abstractx-tlp", kTlpEndpointAddr, tlp_node, sizeof(tlp_node));
    const bool has_trace = ensure_endpoint(ctrl_path, "abstractx-trace", kTraceEndpointAddr, trace_node, sizeof(trace_node));

    int tlp_fd = has_tlp ? open(tlp_node, O_RDWR | O_NONBLOCK) : -1;
    int trace_fd = has_trace ? open(trace_node, O_RDWR | O_NONBLOCK) : -1;

    if (tlp_fd < 0 && trace_fd < 0) {
        std::fprintf(stderr, "[cpp-bridge] Neither abstractx-tlp nor abstractx-trace available via %s\n", ctrl_path);
        return 1;
    }

    g_out_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    int in_sock = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
    if (g_out_sock < 0 || in_sock < 0) {
        std::perror("[cpp-bridge] socket");
        return 1;
    }

    int reuse = 1;
    (void)setsockopt(in_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    g_studio_addr.sin_family = AF_INET;
    g_studio_addr.sin_port = htons(static_cast<uint16_t>(studio_port));
    (void)inet_pton(AF_INET, host_ip, &g_studio_addr.sin_addr);

    g_local_studio_addr.sin_family = AF_INET;
    g_local_studio_addr.sin_port = htons(static_cast<uint16_t>(studio_port));
    (void)inet_pton(AF_INET, "127.0.0.1", &g_local_studio_addr.sin_addr);
    g_send_local_copy = (std::strcmp(host_ip, "127.0.0.1") != 0);

    struct sockaddr_in listen_addr {};
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port = htons(static_cast<uint16_t>(listen_port));
    listen_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    (void)bind(in_sock, reinterpret_cast<struct sockaddr*>(&listen_addr), sizeof(listen_addr));

    send_hello(tlp_fd);
    send_hello(trace_fd);

    abstractx::Config rt_cfg{};
    abstractx::init(rt_cfg);
    abstractx::spawn(compass_coro_task());
    abstractx::spawn(imu_coro_task());
    abstractx::spawn(gps_coro_task());

    std::fprintf(stderr,
                 "[cpp-bridge] Linux C++20 Coroutine Sensor & Studio Bridge online: tlp=%s trace=%s -> %s:%d (ctrl UDP :%d)\n",
                 has_tlp ? tlp_node : "none",
                 has_trace ? trace_node : "none",
                 host_ip,
                 studio_port,
                 listen_port);

    uint64_t next_hello_ms = monotonic_ms() + 1000ULL;
    uint8_t buf[2048];

    while (!g_stop) {
        // 1. Step Linux C++20 Coroutines & drain outgoing TLP requests to E906 RPMsg 1024
        abstractx::step();
        if (tlp_fd >= 0) {
            uint8_t batch[kTlpBytes * kMaxTlpsPerMsg];
            size_t count = 0;
            Tlp64 req{};
            while (count < kMaxTlpsPerMsg && g_linux_tx_ring.pop(req)) {
                std::memcpy(batch + count * kTlpBytes, &req, kTlpBytes);
                ++count;
            }
            if (count > 0) {
                ssize_t w = write(tlp_fd, batch, count * kTlpBytes);
                (void)w;
            }
        }

        // 2. Poll RPMsg endpoints & UDP control socket
        struct pollfd pfds[4]{};
        int nfds = 0;
        int tlp_idx = -1;
        int trace_idx = -1;
        int udp_idx = -1;
        int stdin_idx = -1;

        if (tlp_fd >= 0) {
            tlp_idx = nfds;
            pfds[nfds++] = {tlp_fd, POLLIN, 0};
        }
        if (trace_fd >= 0) {
            trace_idx = nfds;
            pfds[nfds++] = {trace_fd, POLLIN, 0};
        }
        if (in_sock >= 0) {
            udp_idx = nfds;
            pfds[nfds++] = {in_sock, POLLIN, 0};
        }
        if (exit_on_stdin_eof) {
            stdin_idx = nfds;
            pfds[nfds++] = {STDIN_FILENO, POLLIN | POLLHUP, 0};
        }

        int rc = poll(pfds, static_cast<nfds_t>(nfds), 5);
        if (rc < 0 && errno != EINTR) {
            break;
        }

        if (tlp_idx >= 0 && (pfds[tlp_idx].revents & POLLIN)) {
            ssize_t n = read(tlp_fd, buf, sizeof(buf));
            if (valid_tlp_batch(n)) {
                for (ssize_t off = 0; off + static_cast<ssize_t>(kTlpBytes) <= n; off += static_cast<ssize_t>(kTlpBytes)) {
                    Tlp64 rx_tlp{};
                    std::memcpy(&rx_tlp, buf + off, kTlpBytes);
                    if (rx_tlp.wire.type == 0x32U) {
                        // Forward Auto-DMA completion ACK (0x32) to Studio AND to CoroutineIoEngine
                        emit_studio_tlp(rx_tlp);
                    }
                    g_linux_rx_ring.push(rx_tlp);
                }
                g_coro_io.poll_completions();
            }
        }

        if (trace_idx >= 0 && (pfds[trace_idx].revents & POLLIN)) {
            ssize_t n = read(trace_fd, buf, sizeof(buf));
            if (n > 0) {
                (void)sendto(g_out_sock, buf, static_cast<size_t>(n), 0,
                             reinterpret_cast<const struct sockaddr*>(&g_studio_addr), sizeof(g_studio_addr));
                if (g_send_local_copy) {
                    (void)sendto(g_out_sock, buf, static_cast<size_t>(n), 0,
                                 reinterpret_cast<const struct sockaddr*>(&g_local_studio_addr), sizeof(g_local_studio_addr));
                }
            }
        }

        if (udp_idx >= 0 && (pfds[udp_idx].revents & POLLIN)) {
            ssize_t n = recvfrom(in_sock, buf, sizeof(buf), 0, nullptr, nullptr);
            if (valid_tlp_batch(n)) {
                for (ssize_t off = 0; off + static_cast<ssize_t>(kTlpBytes) <= n; off += static_cast<ssize_t>(kTlpBytes)) {
                    Tlp64 ctrl_tlp{};
                    std::memcpy(&ctrl_tlp, buf + off, kTlpBytes);
                    if (ctrl_tlp.wire.type == ASP_TLP_TYPE_DMA_CFG) {
                        const uint8_t cmd = ctrl_tlp.wire.payload[0];
                        if (cmd == ASP_DMA_CMD_ENABLE || cmd == ASP_DMA_CMD_SETUP) {
                            g_auto_dma_enabled.store(true, std::memory_order_release);
                            g_auto_dma_dirty.store(true, std::memory_order_release);
                        } else if (cmd == ASP_DMA_CMD_DISABLE) {
                            g_auto_dma_enabled.store(false, std::memory_order_release);
                            g_auto_dma_dirty.store(true, std::memory_order_release);
                        }
                    }
                    if (tlp_fd >= 0) {
                        ssize_t w = write(tlp_fd, &ctrl_tlp, kTlpBytes);
                        (void)w;
                    }
                }
            }
        }

        if (stdin_idx >= 0 && (pfds[stdin_idx].revents & (POLLIN | POLLHUP))) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) {
                break;
            }
        }

        const uint64_t now_ms = monotonic_ms();
        if (now_ms >= next_hello_ms) {
            send_hello(tlp_fd);
            send_hello(trace_fd);
            next_hello_ms = now_ms + 1000ULL;
        }
    }

    if (tlp_fd >= 0) close(tlp_fd);
    if (trace_fd >= 0) close(trace_fd);
    close(in_sock);
    close(g_out_sock);
    return 0;
}
