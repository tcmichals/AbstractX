/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX barectf Common Trace Format (CTF) Tracer Engine
 * ---------------------------------------------------------
 * Zero-allocation, high-throughput binary tracer for:
 * 1. C++20 Coroutine State Transitions (spawn, suspend, resume, done)
 * 2. High-Rate Flight Telemetry (8 kHz ICM-42688-P & U-Blox GPS)
 * 3. HAL Driver I/O & Inter-Core 64-Byte TLP Routing
 */

#ifndef ABSTRACTX_TRACE_TRACER_HPP
#define ABSTRACTX_TRACE_TRACER_HPP

#include "asp_tlp64.hpp"
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <span>
#include <atomic>

namespace abstractx::trace {

// CTF 1.8 Packet Magic Number
inline constexpr uint32_t CTF_MAGIC = 0xC1FC1FC1U;

// Stream Identifiers
enum class StreamId : uint8_t {
    Coroutine = 0,
    Telemetry = 1,
    HalTlp    = 2
};

// Coroutine States
enum class CoroState : uint8_t {
    Spawn   = 0,
    Suspend = 1,
    Resume  = 2,
    Done    = 3
};

// Packed CTF Packet Header (32 bytes)
struct alignas(4) CtfPacketHeader {
    uint32_t magic{CTF_MAGIC};
    uint8_t  stream_id{0};
    uint8_t  reserved[3]{0};
    uint32_t packet_size_bits{0};
    uint32_t content_size_bits{0};
    uint64_t timestamp_begin_us{0};
    uint64_t timestamp_end_us{0};
    uint32_t events_discarded{0};
};

#pragma pack(push, 1)

// Stream 0: Coroutine Lifecycle Event (19 bytes payload)
struct CoroEventPayload {
    uint8_t  event_id{1};
    uint64_t timestamp_us{0};
    uint32_t task_id{0};
    uint32_t handle_addr{0};
    uint8_t  state{0};
    uint8_t  reason{0};
};

// Stream 1: IMU Sample Event (27 bytes payload)
struct ImuSamplePayload {
    uint8_t  event_id{1};
    uint64_t timestamp_us{0};
    uint32_t sample_seq{0};
    int16_t  accel_x_mg{0};
    int16_t  accel_y_mg{0};
    int16_t  accel_z_mg{0};
    int16_t  gyro_x_dps{0};
    int16_t  gyro_y_dps{0};
    int16_t  gyro_z_dps{0};
    int16_t  temp_c_1e2{0};
};

// Stream 1: GPS Fix Event (35 bytes payload)
struct GpsFixPayload {
    uint8_t  event_id{2};
    uint64_t timestamp_us{0};
    uint32_t itow_ms{0};
    int32_t  lat_1e7{0};
    int32_t  lon_1e7{0};
    int32_t  alt_mm{0};
    int32_t  ground_speed_mm_s{0};
    int32_t  heading_1e5{0};
    uint8_t  sats{0};
    uint8_t  fix_type{0};
};

// Stream 2: HAL Driver I/O Event (19 bytes payload)
struct HalIoPayload {
    uint8_t  event_id{1};
    uint64_t timestamp_us{0};
    uint8_t  peripheral_id{0};
    uint16_t req_size{0};
    uint16_t bytes_transferred{0};
    uint32_t duration_us{0};
    uint8_t  status{0};
};

// Stream 2: TLP Packet Event (18 bytes payload)
struct TlpTracePayload {
    uint8_t  event_id{2};
    uint64_t timestamp_us{0};
    uint8_t  is_push{1};
    uint8_t  tag{0};
    uint8_t  channel{0};
    uint32_t target_addr{0};
    uint16_t len_dw{0};
};

#pragma pack(pop)

// Static assertion ensuring all CTF event structures fit within the 40-byte TLP payload limit
static_assert(sizeof(CoroEventPayload) <= ASP_TLP64_PAYLOAD_SIZE, "CoroEventPayload must fit in Tlp64 payload");
static_assert(sizeof(ImuSamplePayload) <= ASP_TLP64_PAYLOAD_SIZE, "ImuSamplePayload must fit in Tlp64 payload");
static_assert(sizeof(GpsFixPayload) <= ASP_TLP64_PAYLOAD_SIZE, "GpsFixPayload must fit in Tlp64 payload");
static_assert(sizeof(HalIoPayload) <= ASP_TLP64_PAYLOAD_SIZE, "HalIoPayload must fit in Tlp64 payload");
static_assert(sizeof(TlpTracePayload) <= ASP_TLP64_PAYLOAD_SIZE, "TlpTracePayload must fit in Tlp64 payload");

// Flush Callback Type for Target Transports (UDP / DRAM / File)
using TracePacketFlushFn = void (*)(const uint8_t* packet_data, size_t packet_len, void* context);

// @impl [SPEC-TRACE-01] [SPEC-TRACE-02] docs/DESIGN_SPECIFICATION.md#spec-trace-02
// @status Complete
template <size_t PacketSize = 512, size_t NumPackets = 8>
class CtfTraceEngine {
public:
    static constexpr size_t PACKET_SIZE = PacketSize;
    static constexpr size_t NUM_PACKETS = NumPackets;

    void init(TracePacketFlushFn flush_fn = nullptr, void* context = nullptr) noexcept {
        flush_fn_ = flush_fn;
        context_ = context;
        current_packet_idx_ = 0;
        offset_ = sizeof(CtfPacketHeader);
        discarded_count_ = 0;
        reset_current_packet();
    }

    void set_flush_handler(TracePacketFlushFn flush_fn, void* context) noexcept {
        flush_fn_ = flush_fn;
        context_ = context;
    }

    // Trace Coroutine Lifecycle Event
    void trace_coro(uint32_t task_id, uint32_t handle_addr, CoroState state, uint8_t reason, uint64_t now_us) noexcept {
        CoroEventPayload payload{};
        payload.event_id = 1;
        payload.timestamp_us = now_us;
        payload.task_id = task_id;
        payload.handle_addr = handle_addr;
        payload.state = static_cast<uint8_t>(state);
        payload.reason = reason;

        write_event(&payload, sizeof(payload), now_us);
    }

    static Tlp64 make_coro_tlp(uint32_t task_id, uint32_t handle_addr, CoroState state, uint8_t reason, uint64_t now_us) noexcept {
        CoroEventPayload payload{};
        payload.event_id = 1;
        payload.timestamp_us = now_us;
        payload.task_id = task_id;
        payload.handle_addr = handle_addr;
        payload.state = static_cast<uint8_t>(state);
        payload.reason = reason;
        return Tlp64::make_ctf(Channel::Debug, 0, payload, now_us * 1000ULL);
    }

    // Trace IMU Sample Burst
    void trace_imu(uint32_t seq, int16_t ax, int16_t ay, int16_t az,
                   int16_t gx, int16_t gy, int16_t gz, int16_t temp, uint64_t now_us) noexcept {
        ImuSamplePayload payload{};
        payload.event_id = 1;
        payload.timestamp_us = now_us;
        payload.sample_seq = seq;
        payload.accel_x_mg = ax;
        payload.accel_y_mg = ay;
        payload.accel_z_mg = az;
        payload.gyro_x_dps = gx;
        payload.gyro_y_dps = gy;
        payload.gyro_z_dps = gz;
        payload.temp_c_1e2 = temp;

        write_event(&payload, sizeof(payload), now_us);
    }

    static Tlp64 make_imu_tlp(uint32_t seq, int16_t ax, int16_t ay, int16_t az,
                              int16_t gx, int16_t gy, int16_t gz, int16_t temp, uint64_t now_us, uint8_t tag = 0x01) noexcept {
        ImuSamplePayload payload{};
        payload.event_id = 1;
        payload.timestamp_us = now_us;
        payload.sample_seq = seq;
        payload.accel_x_mg = ax;
        payload.accel_y_mg = ay;
        payload.accel_z_mg = az;
        payload.gyro_x_dps = gx;
        payload.gyro_y_dps = gy;
        payload.gyro_z_dps = gz;
        payload.temp_c_1e2 = temp;
        return Tlp64::make_ctf(Channel::Telemetry, tag, payload, now_us * 1000ULL);
    }

    // Trace GPS Navigation Fix
    void trace_gps(uint32_t itow, int32_t lat, int32_t lon, int32_t alt_mm,
                   int32_t speed_mm_s, int32_t heading, uint8_t sats, uint8_t fix_type, uint64_t now_us) noexcept {
        GpsFixPayload payload{};
        payload.event_id = 2;
        payload.timestamp_us = now_us;
        payload.itow_ms = itow;
        payload.lat_1e7 = lat;
        payload.lon_1e7 = lon;
        payload.alt_mm = alt_mm;
        payload.ground_speed_mm_s = speed_mm_s;
        payload.heading_1e5 = heading;
        payload.sats = sats;
        payload.fix_type = fix_type;

        write_event(&payload, sizeof(payload), now_us);
    }

    static Tlp64 make_gps_tlp(uint32_t itow, int32_t lat, int32_t lon, int32_t alt_mm,
                              int32_t speed_mm_s, int32_t heading, uint8_t sats, uint8_t fix_type, uint64_t now_us, uint8_t tag = 0x02) noexcept {
        GpsFixPayload payload{};
        payload.event_id = 2;
        payload.timestamp_us = now_us;
        payload.itow_ms = itow;
        payload.lat_1e7 = lat;
        payload.lon_1e7 = lon;
        payload.alt_mm = alt_mm;
        payload.ground_speed_mm_s = speed_mm_s;
        payload.heading_1e5 = heading;
        payload.sats = sats;
        payload.fix_type = fix_type;
        return Tlp64::make_ctf(Channel::Telemetry, tag, payload, now_us * 1000ULL);
    }

    // Trace HAL Driver I/O Event
    void trace_hal(uint8_t peripheral_id, uint16_t req_size, uint16_t bytes_transferred,
                   uint32_t duration_us, uint8_t status, uint64_t now_us) noexcept {
        HalIoPayload payload{};
        payload.event_id = 1;
        payload.timestamp_us = now_us;
        payload.peripheral_id = peripheral_id;
        payload.req_size = req_size;
        payload.bytes_transferred = bytes_transferred;
        payload.duration_us = duration_us;
        payload.status = status;

        write_event(&payload, sizeof(payload), now_us);
    }

    // Trace 64-Byte TLP Routing Event
    void trace_tlp(bool is_push, uint8_t tag, uint8_t channel, uint32_t target_addr, uint16_t len_dw, uint64_t now_us) noexcept {
        TlpTracePayload payload{};
        payload.event_id = 2;
        payload.timestamp_us = now_us;
        payload.is_push = is_push ? 1 : 0;
        payload.tag = tag;
        payload.channel = channel;
        payload.target_addr = target_addr;
        payload.len_dw = len_dw;

        write_event(&payload, sizeof(payload), now_us);
    }

    // Manually flush active packet to transport
    void flush(uint64_t now_us) noexcept {
        if (offset_ > sizeof(CtfPacketHeader)) {
            commit_and_flush(now_us);
        }
    }

    size_t get_discarded_count() const noexcept {
        return discarded_count_;
    }

private:
    void write_event(const void* data, size_t len, uint64_t now_us) noexcept {
        if (offset_ + len > PacketSize) {
            commit_and_flush(now_us);
        }

        if (offset_ + len <= PacketSize) {
            uint8_t* dest = &buffer_pool_[current_packet_idx_][offset_];
            std::memcpy(dest, data, len);
            offset_ += len;
        } else {
            discarded_count_++;
        }
    }

    void reset_current_packet() noexcept {
        uint8_t* raw = buffer_pool_[current_packet_idx_];
        std::memset(raw, 0, PacketSize);
        auto* hdr = reinterpret_cast<CtfPacketHeader*>(raw);
        hdr->magic = CTF_MAGIC;
        hdr->stream_id = 0;
        hdr->packet_size_bits = static_cast<uint32_t>(PacketSize * 8);
        hdr->content_size_bits = static_cast<uint32_t>(sizeof(CtfPacketHeader) * 8);
        hdr->events_discarded = discarded_count_;
        offset_ = sizeof(CtfPacketHeader);
    }

    void commit_and_flush(uint64_t now_us) noexcept {
        uint8_t* raw = buffer_pool_[current_packet_idx_];
        auto* hdr = reinterpret_cast<CtfPacketHeader*>(raw);
        hdr->content_size_bits = static_cast<uint32_t>(offset_ * 8);
        hdr->timestamp_end_us = now_us;

        if (flush_fn_) {
            flush_fn_(raw, offset_, context_);
        }

        // Advance to next buffer in the static ring
        current_packet_idx_ = (current_packet_idx_ + 1) % NumPackets;
        reset_current_packet();
        hdr = reinterpret_cast<CtfPacketHeader*>(buffer_pool_[current_packet_idx_]);
        hdr->timestamp_begin_us = now_us;
    }

    alignas(4) uint8_t buffer_pool_[NumPackets][PacketSize]{{0}};
    size_t             current_packet_idx_{0};
    size_t             offset_{sizeof(CtfPacketHeader)};
    uint32_t           discarded_count_{0};
    TracePacketFlushFn flush_fn_{nullptr};
    void*              context_{nullptr};
};

// Global default tracer instance
inline CtfTraceEngine<512, 8> g_tracer;

} // namespace abstractx::trace

#endif // ABSTRACTX_TRACE_TRACER_HPP
