/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX U-Blox UBX Binary GPS Protocol Driver
 * ------------------------------------------------
 * Zero-allocation asynchronous UBX-NAV-PVT parser and driver,
 * ingesting UART streaming data via split-queue DMA/ISR and
 * emitting timestamped 64B PCIe-style TLP packets.
 */

#ifndef ABSTRACTX_DRIVERS_GPS_UBLOX_GPS_HPP
#define ABSTRACTX_DRIVERS_GPS_UBLOX_GPS_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <coroutine>
#include <array>
#include <cstring>

#include "abstractx/hal/uart.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/trace/tracer.hpp"
#include "asp_tlp64.hpp"

namespace abstractx::drivers::gps {

constexpr uint8_t TLP_TAG_GPS = 0x02;

enum class GpsFixType : uint8_t {
    NoFix = 0,
    DeadReckoning = 1,
    Fix2D = 2,
    Fix3D = 3,
    GnssPlusDeadReckoning = 4,
    TimeOnly = 5
};

/* Calibrated GPS Navigation Solution */
struct GpsFix {
    int32_t    lat_1e7{0};         // Latitude (degrees * 1e7)
    int32_t    lon_1e7{0};         // Longitude (degrees * 1e7)
    int32_t    alt_msl_mm{0};      // Altitude MSL (mm)
    int32_t    ground_speed_mm_s{0}; // Ground Speed (mm/s)
    int32_t    heading_1e5{0};     // Heading of motion (degrees * 1e5)
    uint8_t    satellites{0};      // Number of satellites tracked
    GpsFixType fix_type{GpsFixType::NoFix};
    uint32_t   itow_ms{0};         // GPS Time of Week
    uint64_t   timestamp_us{0};
    bool       valid{false};
};

// @impl [SPEC-GPS-01] [SPEC-GPS-02] docs/DESIGN_SPECIFICATION.md#spec-gps-01
// @status Complete
class UbloxGps {
public:
    explicit UbloxGps(hal::IUart& uart_driver) : uart_(uart_driver) {}

    void init(uint32_t baudrate = 115200) {
        uart_.init(baudrate);
        reset_parser();
    }

    /*
     * Stream byte-by-byte into the parser state machine.
     * Returns true when a complete, verified UBX-NAV-PVT frame is received.
     */
    bool feed_byte(uint8_t ch, GpsFix& out_fix) noexcept {
        switch (state_) {
            case State::SYNC_1:
                if (ch == 0xB5) {
                    state_ = State::SYNC_2;
                }
                break;

            case State::SYNC_2:
                if (ch == 0x62) {
                    state_ = State::CLASS;
                    ck_a_ = 0;
                    ck_b_ = 0;
                } else {
                    state_ = State::SYNC_1;
                }
                break;

            case State::CLASS:
                msg_class_ = ch;
                update_checksum(ch);
                state_ = State::ID;
                break;

            case State::ID:
                msg_id_ = ch;
                update_checksum(ch);
                state_ = State::LENGTH_L;
                break;

            case State::LENGTH_L:
                payload_len_ = ch;
                update_checksum(ch);
                state_ = State::LENGTH_H;
                break;

            case State::LENGTH_H:
                payload_len_ |= (static_cast<uint16_t>(ch) << 8);
                update_checksum(ch);
                payload_idx_ = 0;
                if (payload_len_ <= MAX_PAYLOAD) {
                    state_ = State::PAYLOAD;
                } else {
                    reset_parser();
                }
                break;

            case State::PAYLOAD:
                payload_buf_[payload_idx_++] = ch;
                update_checksum(ch);
                if (payload_idx_ >= payload_len_) {
                    state_ = State::CK_A;
                }
                break;

            case State::CK_A:
                if (ch == ck_a_) {
                    state_ = State::CK_B;
                } else {
                    reset_parser();
                }
                break;

            case State::CK_B:
                if (ch == ck_b_) {
                    bool parsed = parse_payload(out_fix);
                    reset_parser();
                    return parsed;
                }
                reset_parser();
                break;
        }
        return false;
    }

    /*
     * Package GpsFix into a standardized CTF 1.8 binary payload inside a 64-Byte TLP
     */
    static Tlp64 to_tlp(const GpsFix& fix) noexcept {
        trace::GpsFixPayload ctf{};
        ctf.event_id = 2;
        ctf.timestamp_us = fix.timestamp_us;
        ctf.itow_ms = fix.itow_ms;
        ctf.lat_1e7 = fix.lat_1e7;
        ctf.lon_1e7 = fix.lon_1e7;
        ctf.alt_mm = fix.alt_msl_mm;
        ctf.ground_speed_mm_s = fix.ground_speed_mm_s;
        ctf.heading_1e5 = fix.heading_1e5;
        ctf.sats = fix.satellites;
        ctf.fix_type = static_cast<uint8_t>(fix.fix_type);
        return Tlp64::make_ctf(Channel::Telemetry, TLP_TAG_GPS, ctf, fix.timestamp_us * 1000ULL);
    }

    /*
     * C++20 Coroutine Async Next Fix Awaiter
     */
    struct AsyncFixAwaiter {
        UbloxGps& ublox;
        GpsFix    fix{};

        explicit AsyncFixAwaiter(UbloxGps& gps) : ublox(gps) {}

        bool await_ready() const noexcept { return false; }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            // Register handle to resume when valid fix arrives
            ublox.pending_handle_ = handle;
        }

        GpsFix await_resume() noexcept {
            return ublox.last_fix_;
        }
    };

    AsyncFixAwaiter next_fix_async() noexcept {
        return AsyncFixAwaiter(*this);
    }

    void on_uart_rx_ready() noexcept {
        uint8_t ch;
        while (uart_.read_byte(ch)) {
            GpsFix fix;
            if (feed_byte(ch, fix)) {
                last_fix_ = fix;
                if (pending_handle_ && !pending_handle_.done()) {
                    IsrDispatcher::post(pending_handle_);
                    pending_handle_ = nullptr;
                }
            }
        }
    }

private:
    void reset_parser() noexcept {
        state_ = State::SYNC_1;
        payload_idx_ = 0;
        payload_len_ = 0;
    }

    void update_checksum(uint8_t ch) noexcept {
        ck_a_ += ch;
        ck_b_ += ck_a_;
    }

    bool parse_payload(GpsFix& out) noexcept {
        // NAV-PVT: Class 0x01, ID 0x07, Length 92 bytes
        if (msg_class_ == 0x01 && msg_id_ == 0x07 && payload_len_ >= 92) {
            std::memcpy(&out.itow_ms,           &payload_buf_[0], 4);
            out.fix_type = static_cast<GpsFixType>(payload_buf_[20]);
            out.satellites = payload_buf_[23];
            std::memcpy(&out.lon_1e7,          &payload_buf_[24], 4);
            std::memcpy(&out.lat_1e7,          &payload_buf_[28], 4);
            std::memcpy(&out.alt_msl_mm,       &payload_buf_[36], 4);
            std::memcpy(&out.ground_speed_mm_s,&payload_buf_[60], 4);
            std::memcpy(&out.heading_1e5,      &payload_buf_[64], 4);
            out.valid = (out.fix_type >= GpsFixType::Fix2D);
            if (out.valid) {
                trace::g_tracer.trace_gps(
                    out.itow_ms, out.lat_1e7, out.lon_1e7, out.alt_msl_mm,
                    out.ground_speed_mm_s, out.heading_1e5, out.satellites,
                    static_cast<uint8_t>(out.fix_type), out.timestamp_us
                );
            }
            return true;
        }
        return false;
    }

    enum class State : uint8_t {
        SYNC_1,
        SYNC_2,
        CLASS,
        ID,
        LENGTH_L,
        LENGTH_H,
        PAYLOAD,
        CK_A,
        CK_B
    };

    static constexpr size_t MAX_PAYLOAD = 128;

    hal::IUart&             uart_;
    State                   state_{State::SYNC_1};
    uint8_t                 msg_class_{0};
    uint8_t                 msg_id_{0};
    uint16_t                payload_len_{0};
    uint16_t                payload_idx_{0};
    uint8_t                 ck_a_{0};
    uint8_t                 ck_b_{0};
    uint8_t                 payload_buf_[MAX_PAYLOAD]{0};
    GpsFix                  last_fix_{};
    std::coroutine_handle<> pending_handle_{nullptr};
};

} // namespace abstractx::drivers::gps

#endif // ABSTRACTX_DRIVERS_GPS_UBLOX_GPS_HPP
