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
#include <new>

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

// Sizing and Topology Profiles for CTF Trace Buffers
enum class BufferProfile : uint8_t {
    PingPong_1K_x2  = 0, // 2 x 1024 B ping-pong (2 KB total - recommended default)
    Ring_1K_x4      = 1, // 4 x 1024 B ring (4 KB total - for high-burst buffering)
    Compact_512B_x2 = 2, // 2 x 512 B ping-pong (1 KB total - for ultra-constrained SRAM)
    Large_2K_x4     = 3  // 4 x 2048 B ring (8 KB total - for Linux SITL / logging)
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
// Stream 0: Coroutine Lifecycle Event with Name & Awaiter Token (38 bytes payload)
// Fits within the 40-byte ASP_TLP64_PAYLOAD_SIZE window.
//
// Wire layout (packed, 38 B):
//  [0]     event_id       : uint8  (always 1)
//  [1..8]  timestamp_us   : uint64
//  [9..12] task_id        : uint32  (coroutine slot index 1..N)
//  [13..16]handle_addr    : uint32  (lower 32-bits of coroutine_handle address)
//  [17]    state          : uint8   (CoroState)
//  [18]    reason         : uint8   (legacy reason enum, kept for binary compat)
//  [19..30]coro_name      : char[12] (null-terminated coroutine name)
//  [21..37]awaiter_token  : char[16] (null-terminated co_await expression)
struct CoroEventPayload {
    uint8_t  event_id{1};
    uint64_t timestamp_us{0};
    uint32_t task_id{0};
    uint32_t handle_addr{0};
    uint8_t  state{0};
    uint8_t  reason{0};
    char     coro_name[8]{"?"};
    char     awaiter_token[11]{"?"};
};

static_assert(sizeof(CoroEventPayload) == 38, "CoroEventPayload must be exactly 38 bytes");

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

// Flush callback for the static-pool transport (host UDP / file sinks).
using TracePacketFlushFn = void (*)(const uint8_t* packet_data, size_t packet_len, void* context);

// In-buffer view of CtfPacketHeader: identical 40-byte layout, but packed so the
// header can be written into a transport buffer that is not 8-byte aligned
// (an lwIP UDP payload starts ~42 bytes into the frame). Readers on aligned
// host memory keep using CtfPacketHeader.
#pragma pack(push, 1)
struct CtfPacketHeaderWire {
    uint32_t magic;
    uint8_t  stream_id;
    uint8_t  reserved[3];
    uint32_t packet_size_bits;
    uint32_t content_size_bits;
    uint64_t timestamp_begin_us;
    uint64_t timestamp_end_us;
    uint32_t events_discarded;
    uint32_t pad;
};
#pragma pack(pop)

static_assert(sizeof(CtfPacketHeaderWire) == sizeof(CtfPacketHeader), "CTF header wire size");
static_assert(offsetof(CtfPacketHeaderWire, packet_size_bits) == offsetof(CtfPacketHeader, packet_size_bits), "CTF header layout");
static_assert(offsetof(CtfPacketHeaderWire, content_size_bits) == offsetof(CtfPacketHeader, content_size_bits), "CTF header layout");
static_assert(offsetof(CtfPacketHeaderWire, timestamp_begin_us) == offsetof(CtfPacketHeader, timestamp_begin_us), "CTF header layout");
static_assert(offsetof(CtfPacketHeaderWire, timestamp_end_us) == offsetof(CtfPacketHeader, timestamp_end_us), "CTF header layout");
static_assert(offsetof(CtfPacketHeaderWire, events_discarded) == offsetof(CtfPacketHeader, events_discarded), "CTF header layout");

// A trace transport lends packet buffers to the engine, which writes the CTF
// packet in place and hands the same memory back: no copy between the tracer
// and the transport (RPMsg buffer on the E906, lwIP pbuf on the Pico / ESP32-P4).
// @impl [SPEC-TRACE-07] [SPEC-ARCH-08] docs/DESIGN_SPECIFICATION.md#spec-trace-07
// @status Partial (engine + static pool done; E906 RPMsg and Pico/ESP32 pbuf transports pending)
class ITraceTransport {
public:
    virtual ~ITraceTransport() = default;

    // Next writable packet buffer of at most max_bytes, or an empty span if none
    // is free. Must never block. A non-empty buffer holds at least a CTF header.
    virtual std::span<uint8_t> acquire(size_t max_bytes) noexcept = 0;

    // Send exactly buffer[0, used_bytes). Ownership passes to the transport.
    virtual void commit(std::span<uint8_t> buffer, size_t used_bytes) noexcept = 0;
};

// Default transport: a static ring of packet buffers (BufferProfile,
// [SPEC-TRACE-06]) handed to a flush callback on commit. Used on Linux / SITL,
// where the host kernel's copy on write()/sendto() is outside AbstractX.
// @impl [SPEC-TRACE-06] [SPEC-TRACE-07] docs/DESIGN_SPECIFICATION.md#spec-trace-07
template <size_t PacketSize, size_t NumPackets>
class StaticPoolTraceTransport final : public ITraceTransport {
public:
    void set_flush_handler(TracePacketFlushFn flush_fn, void* context) noexcept {
        flush_fn_ = flush_fn;
        context_ = context;
    }

    std::span<uint8_t> acquire(size_t max_bytes) noexcept override {
        uint8_t* buf = pool_[next_];
        next_ = (next_ + 1) % NumPackets;
        return {buf, max_bytes < PacketSize ? max_bytes : PacketSize};
    }

    void commit(std::span<uint8_t> buffer, size_t used_bytes) noexcept override {
        if (flush_fn_) {
            flush_fn_(buffer.data(), used_bytes, context_);
        }
    }

    size_t next_index() const noexcept { return next_; }
    void reset() noexcept { next_ = 0; }

private:
    alignas(8) uint8_t pool_[NumPackets][PacketSize]{};
    size_t             next_{0};
    TracePacketFlushFn flush_fn_{nullptr};
    void*              context_{nullptr};
};

// Default number of packets allocated in the fallback static pool.
// On targets providing a native zero-copy transport (e.g. E906 RPMsg in SRAM,
// [SPEC-TRACE-07], [SPEC-E906-IPC-04]), this defaults to 0 to prevent allocating
// unused ping-pong buffers in .bss.
#ifndef ABSTRACTX_STATIC_TRACE_PACKETS
#if defined(CONFIG_RPROC_RPMSG) || defined(TARGET_ALLWINNER_E906) || defined(TARGET_PICO2W_RP2350)
#define ABSTRACTX_STATIC_TRACE_PACKETS 0
#else
#define ABSTRACTX_STATIC_TRACE_PACKETS 2
#endif
#endif

// Internal storage helper for CtfTraceEngine.
// When NumPackets > 0, embeds a StaticPoolTraceTransport.
// When NumPackets == 0, allocates zero bytes and provides empty/null fallbacks.
// @impl [SPEC-TRACE-06] [SPEC-TRACE-07] docs/DESIGN_SPECIFICATION.md#spec-trace-07
template <size_t PacketSize, size_t NumPackets>
struct TracePoolStorage {
    StaticPoolTraceTransport<PacketSize, NumPackets> pool_{};
    size_t pool_next_index() const noexcept { return pool_.next_index(); }
    void pool_reset() noexcept { pool_.reset(); }
    void pool_set_flush_handler(TracePacketFlushFn flush_fn, void* context) noexcept {
        pool_.set_flush_handler(flush_fn, context);
    }
    ITraceTransport* pool_transport() noexcept { return &pool_; }
};

template <size_t PacketSize>
struct TracePoolStorage<PacketSize, 0> {
    size_t pool_next_index() const noexcept { return 0; }
    void pool_reset() noexcept {}
    void pool_set_flush_handler(TracePacketFlushFn, void*) noexcept {}
    ITraceTransport* pool_transport() noexcept { return nullptr; }
};

// Events are constructed directly in the transport's buffer (emplace), so a
// traced sample is written once, where it will be sent.
// @impl [SPEC-TRACE-01] [SPEC-TRACE-02] [SPEC-TRACE-06] [SPEC-TRACE-07] docs/DESIGN_SPECIFICATION.md#spec-trace-07
// @status Complete
template <size_t PacketSize = 1024, size_t NumPackets = ABSTRACTX_STATIC_TRACE_PACKETS>
class CtfTraceEngine : private TracePoolStorage<PacketSize, NumPackets> {
public:
    static constexpr size_t PACKET_SIZE = PacketSize;
    static constexpr size_t NUM_PACKETS = NumPackets;
    static constexpr size_t TOTAL_CAPACITY_BYTES = PacketSize * NumPackets;

    static constexpr bool is_ping_pong() noexcept { return NumPackets == 2; }
    // Static-pool buffer the next packet will be written into.
    size_t get_active_fill_idx() const noexcept { return this->pool_next_index(); }
    size_t get_ready_transmit_idx() const noexcept {
        if constexpr (NumPackets == 0) {
            return 0;
        } else {
            return (this->pool_next_index() + NumPackets - 1) % NumPackets;
        }
    }
    size_t get_current_offset() const noexcept {
        return buf_.empty() ? sizeof(CtfPacketHeader) : offset_;
    }

    CtfTraceEngine() noexcept {
        init();
    }

    void init(TracePacketFlushFn flush_fn = nullptr, void* context = nullptr) noexcept {
        this->pool_set_flush_handler(flush_fn, context);
        this->pool_reset();
        transport_ = this->pool_transport();
        buf_ = {};
        offset_ = 0;
        discarded_count_ = 0;
    }

    // Send packets to a callback through the built-in static pool.
    void set_flush_handler(TracePacketFlushFn flush_fn, void* context) noexcept {
        flush(last_event_us_);
        this->pool_set_flush_handler(flush_fn, context);
        transport_ = this->pool_transport();
    }

    // Write packets directly into a zero-copy transport's buffers
    // (nullptr returns to the static pool if available).
    void set_transport(ITraceTransport* transport) noexcept {
        flush(last_event_us_);
        transport_ = transport ? transport : this->pool_transport();
    }

    // Trace Coroutine Lifecycle Event (legacy, no name/token)
    void trace_coro(uint32_t task_id, uint32_t handle_addr, CoroState state, uint8_t reason, uint64_t now_us) noexcept {
        trace_coro_named(task_id, handle_addr, state, reason, "?", "?", now_us);
    }

    // Trace Coroutine Lifecycle Event with coroutine name and awaiter token string.
    // coro_name     : human-readable name (e.g. "imu_pipeline"), truncated to 11 chars
    // awaiter_token : co_await expression string (e.g. "spi_ring.pop()"), truncated to 15 chars
    void trace_coro_named(uint32_t task_id, uint32_t handle_addr, CoroState state, uint8_t reason,
                          const char* coro_name, const char* awaiter_token, uint64_t now_us) noexcept {
        auto* ev = emplace<CoroEventPayload>(now_us);
        if (!ev) {
            return;
        }
        ev->event_id     = 1;
        ev->timestamp_us = now_us;
        ev->task_id      = task_id;
        ev->handle_addr  = handle_addr;
        ev->state        = static_cast<uint8_t>(state);
        ev->reason       = reason;
        // Null-terminated with hard truncation
        if (coro_name) {
            for (size_t i = 0; i < sizeof(ev->coro_name) - 1 && coro_name[i]; ++i)
                ev->coro_name[i] = coro_name[i];
        }
        if (awaiter_token) {
            for (size_t i = 0; i < sizeof(ev->awaiter_token) - 1 && awaiter_token[i]; ++i)
                ev->awaiter_token[i] = awaiter_token[i];
        }
    }

    static Tlp64 make_coro_tlp(uint32_t task_id, uint32_t handle_addr, CoroState state, uint8_t reason,
                               const char* coro_name, const char* awaiter_token, uint64_t now_us) noexcept {
        CoroEventPayload payload{};
        payload.event_id    = 1;
        payload.timestamp_us = now_us;
        payload.task_id     = task_id;
        payload.handle_addr = handle_addr;
        payload.state       = static_cast<uint8_t>(state);
        payload.reason      = reason;
        if (coro_name) {
            for (size_t i = 0; i < sizeof(payload.coro_name) - 1 && coro_name[i]; ++i)
                payload.coro_name[i] = coro_name[i];
        }
        if (awaiter_token) {
            for (size_t i = 0; i < sizeof(payload.awaiter_token) - 1 && awaiter_token[i]; ++i)
                payload.awaiter_token[i] = awaiter_token[i];
        }
        return Tlp64::make_ctf(Channel::Debug, 0, payload, now_us * 1000ULL);
    }

    // Legacy overload (no name/token) for backward compat
    static Tlp64 make_coro_tlp(uint32_t task_id, uint32_t handle_addr, CoroState state, uint8_t reason, uint64_t now_us) noexcept {
        return make_coro_tlp(task_id, handle_addr, state, reason, "?", "?", now_us);
    }

    // Trace IMU Sample Burst
    void trace_imu(uint32_t seq, int16_t ax, int16_t ay, int16_t az,
                   int16_t gx, int16_t gy, int16_t gz, int16_t temp, uint64_t now_us) noexcept {
        auto* ev = emplace<ImuSamplePayload>(now_us);
        if (!ev) {
            return;
        }
        ev->event_id     = 1;
        ev->timestamp_us = now_us;
        ev->sample_seq   = seq;
        ev->accel_x_mg   = ax;
        ev->accel_y_mg   = ay;
        ev->accel_z_mg   = az;
        ev->gyro_x_dps   = gx;
        ev->gyro_y_dps   = gy;
        ev->gyro_z_dps   = gz;
        ev->temp_c_1e2   = temp;
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
        auto* ev = emplace<GpsFixPayload>(now_us);
        if (!ev) {
            return;
        }
        ev->event_id          = 2;
        ev->timestamp_us      = now_us;
        ev->itow_ms           = itow;
        ev->lat_1e7           = lat;
        ev->lon_1e7           = lon;
        ev->alt_mm            = alt_mm;
        ev->ground_speed_mm_s = speed_mm_s;
        ev->heading_1e5       = heading;
        ev->sats              = sats;
        ev->fix_type          = fix_type;
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
        auto* ev = emplace<HalIoPayload>(now_us);
        if (!ev) {
            return;
        }
        ev->event_id          = 1;
        ev->timestamp_us      = now_us;
        ev->peripheral_id     = peripheral_id;
        ev->req_size          = req_size;
        ev->bytes_transferred = bytes_transferred;
        ev->duration_us       = duration_us;
        ev->status            = status;
    }

    // Trace 64-Byte TLP Routing Event
    void trace_tlp(bool is_push, uint8_t tag, uint8_t channel, uint32_t target_addr, uint16_t len_dw, uint64_t now_us) noexcept {
        auto* ev = emplace<TlpTracePayload>(now_us);
        if (!ev) {
            return;
        }
        ev->event_id     = 2;
        ev->timestamp_us = now_us;
        ev->is_push      = is_push ? 1 : 0;
        ev->tag          = tag;
        ev->channel      = channel;
        ev->target_addr  = target_addr;
        ev->len_dw       = len_dw;
    }

    // One packet of its own for a stream (e.g. runtime status, [SPEC-TRACE-08]): any open
    // packet is sent first, then fill(dst) writes exactly `bytes` after the header.
    // Returns false (counted as discarded) if no buffer of that size is free.
    template <typename Fill>
    bool emit_stream_packet(uint8_t stream_id, size_t bytes, Fill&& fill, uint64_t now_us) noexcept {
        flush(now_us);
        if (!buf_.empty() || !open_packet(now_us)) {
            ++discarded_count_;
            return false;
        }
        if (offset_ + bytes > buf_.size()) {
            ++discarded_count_;
            commit_packet(now_us);  // header only: returns the buffer, readers skip it
            return false;
        }
        reinterpret_cast<CtfPacketHeaderWire*>(buf_.data())->stream_id = stream_id;
        fill(buf_.data() + offset_);
        offset_ += bytes;
        commit_packet(now_us);
        return true;
    }

    // Hand the current packet to the transport if it holds any events.
    void flush(uint64_t now_us) noexcept {
        if (!buf_.empty() && offset_ > sizeof(CtfPacketHeaderWire)) {
            commit_packet(now_us);
        }
    }

    size_t get_discarded_count() const noexcept {
        return discarded_count_;
    }

private:
    // Reserve sizeof(T) bytes in the transport buffer and construct T there.
    // Returns nullptr (event counted as discarded) if no buffer is available.
    template <typename T>
    T* emplace(uint64_t now_us) noexcept {
        static_assert(alignof(T) == 1, "in-place events must be packed (any buffer alignment)");
        if (!buf_.empty() && offset_ + sizeof(T) > buf_.size()) {
            commit_packet(now_us);
        }
        if (buf_.empty() && !open_packet(now_us)) {
            ++discarded_count_;
            return nullptr;
        }
        if (offset_ + sizeof(T) > buf_.size()) {
            ++discarded_count_;
            return nullptr;
        }
        T* ev = ::new (static_cast<void*>(buf_.data() + offset_)) T{};
        offset_ += sizeof(T);
        last_event_us_ = now_us;
        return ev;
    }

    bool open_packet(uint64_t now_us) noexcept {
        if (!transport_) {
            return false;
        }
        std::span<uint8_t> buf = transport_->acquire(PacketSize);
        if (buf.size() < sizeof(CtfPacketHeaderWire)) {
            return false;
        }
        buf_ = buf;
        auto* hdr = ::new (static_cast<void*>(buf_.data())) CtfPacketHeaderWire{};
        hdr->magic              = CTF_MAGIC;
        hdr->packet_size_bits   = static_cast<uint32_t>(buf_.size() * 8);
        hdr->content_size_bits  = static_cast<uint32_t>(sizeof(CtfPacketHeaderWire) * 8);
        hdr->timestamp_begin_us = now_us;
        hdr->events_discarded   = discarded_count_;
        offset_ = sizeof(CtfPacketHeaderWire);
        return true;
    }

    void commit_packet(uint64_t now_us) noexcept {
        if (!transport_ || buf_.empty()) {
            return;
        }
        auto* hdr = reinterpret_cast<CtfPacketHeaderWire*>(buf_.data());
        hdr->content_size_bits = static_cast<uint32_t>(offset_ * 8);
        hdr->timestamp_end_us  = now_us;
        hdr->events_discarded  = discarded_count_;
        std::span<uint8_t> buf = buf_;
        const size_t used = offset_;
        buf_ = {};
        offset_ = 0;
        transport_->commit(buf, used);
    }

    ITraceTransport*   transport_{nullptr};
    std::span<uint8_t> buf_{};
    size_t             offset_{0};
    uint32_t           discarded_count_{0};
    uint64_t           last_event_us_{0};
};

// Global default tracer instance:
// When ABSTRACTX_STATIC_TRACE_PACKETS > 0 (host/SITL), embeds a ping-pong static pool (2 x 1024 B).
// When ABSTRACTX_STATIC_TRACE_PACKETS == 0 (e.g. E906 zero-copy), no static pool is allocated.
// @impl [SPEC-TRACE-06] [SPEC-TRACE-07] docs/DESIGN_SPECIFICATION.md#spec-trace-07
inline CtfTraceEngine<1024, ABSTRACTX_STATIC_TRACE_PACKETS> g_tracer;

} // namespace abstractx::trace

#endif // ABSTRACTX_TRACE_TRACER_HPP
