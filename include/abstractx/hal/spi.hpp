/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: SPI & Dual-SPI DMA Split-Queue Interface
 * -------------------------------------------------------
 * Hardware interface for single and dual SPI high-speed TLP streaming
 * with DMA/ISR request/completion queues and coroutine awaiters.
 */

#ifndef ABSTRACTX_HAL_SPI_HPP
#define ABSTRACTX_HAL_SPI_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <array>
#include <coroutine>

#include "async_driver.hpp"
#include "io_status.hpp"

namespace abstractx::hal {

enum class SpiMode : uint8_t {
    Mode0 = 0, // CPOL=0, CPHA=0
    Mode1 = 1, // CPOL=0, CPHA=1
    Mode2 = 2, // CPOL=1, CPHA=0
    Mode3 = 3  // CPOL=1, CPHA=1
};

enum class SpiStatus : uint8_t {
    Ok = 0,
    DmaError = 1,
    Timeout = 2,
    CrcError = 3,
    QueueFull = 4,
    NotSupported = 5,     // [SPEC-HAL-08]: outside SpiCapabilities, refused before the hardware
    InvalidArgument = 6,  // [SPEC-HAL-08]: malformed transaction
    Cancelled = 7,        // [SPEC-HAL-07]
    BusError = 8
};

// @impl [SPEC-HAL-07] docs/DESIGN_SPECIFICATION.md
constexpr IoStatus to_io_status(SpiStatus s) noexcept {
    switch (s) {
    case SpiStatus::Ok: return IoStatus::Ok;
    case SpiStatus::DmaError: return IoStatus::DmaError;
    case SpiStatus::Timeout: return IoStatus::Timeout;
    case SpiStatus::CrcError: return IoStatus::Crc;
    case SpiStatus::QueueFull: return IoStatus::QueueFull;
    case SpiStatus::NotSupported: return IoStatus::NotSupported;
    case SpiStatus::InvalidArgument: return IoStatus::InvalidArgument;
    case SpiStatus::Cancelled: return IoStatus::Cancelled;
    case SpiStatus::BusError: return IoStatus::BusError;
    }
    return IoStatus::BusError;
}

// ---------------------------------------------------------------------------
// [SPEC-HAL-08] Devices and segmented transactions on one bus driver
// @impl [SPEC-HAL-08] docs/DESIGN_SPECIFICATION.md
// ---------------------------------------------------------------------------

// Data lines used by one segment (Linux tx_nbits/rx_nbits, Zephyr SPI_LINES_*).
enum class SpiLanes : uint8_t { Single = 1, Dual = 2, Quad = 4 };

// One chip on the bus; static data owned by the device driver.
struct SpiDevice {
    uint8_t  cs{0};
    SpiMode  mode{SpiMode::Mode0};
    uint32_t max_hz{1'000'000};
    bool     cs_active_high{false};
    bool     lsb_first{false};
};

// One phase of a transaction. Multi-lane segments are half duplex (tx or rx).
struct SpiSegment {
    std::span<const uint8_t> tx{};
    std::span<uint8_t>       rx{};
    SpiLanes lanes{SpiLanes::Single};
    bool     release_cs_after{false};  // deassert CS after this (non-last) segment

    static constexpr SpiSegment write(std::span<const uint8_t> data,
                                      SpiLanes l = SpiLanes::Single) noexcept {
        return SpiSegment{data, {}, l, false};
    }
    static constexpr SpiSegment read(std::span<uint8_t> data,
                                     SpiLanes l = SpiLanes::Single) noexcept {
        return SpiSegment{{}, data, l, false};
    }
    static constexpr SpiSegment duplex(std::span<const uint8_t> t, std::span<uint8_t> r) noexcept {
        return SpiSegment{t, r, SpiLanes::Single, false};
    }
    constexpr size_t bytes() const noexcept { return tx.size() > rx.size() ? tx.size() : rx.size(); }
};

// What the bus driver's hardware supports; a field is set only when verified for that chip.
struct SpiCapabilities {
    uint32_t max_hz{0};            // 0 = unknown
    uint16_t fifo_bytes{0};        // informative: the driver's FIFO/DMA rule ([SPEC-E906-HAL-01])
    uint8_t  max_segments{0};      // 0 = device transactions not implemented: all refused
    uint8_t  num_cs{0};
    uint8_t  modes{0};             // bit n set = SpiMode n supported
    uint8_t  lanes{0};             // OR of SpiLanes values supported
    bool     cs_active_high{false};
    bool     lsb_first{false};
    bool     cs_release_between_segments{false};
    bool     dma{false};           // informative: devices never choose DMA
};

constexpr uint8_t spi_mode_bit(SpiMode m) noexcept {
    return static_cast<uint8_t>(1U << static_cast<uint8_t>(m));
}
constexpr uint8_t spi_lane_bit(SpiLanes l) noexcept { return static_cast<uint8_t>(l); }

// Pure check of a transaction ([SPEC-HAL-08]): Ok, InvalidArgument (malformed) or NotSupported.
constexpr SpiStatus check_spi_transaction(const SpiCapabilities& caps, const SpiDevice& dev,
                                          std::span<const SpiSegment> segs) noexcept {
    if (segs.empty() || dev.max_hz == 0 || static_cast<uint8_t>(dev.mode) > 3U) {
        return SpiStatus::InvalidArgument;
    }
    for (const SpiSegment& sg : segs) {
        if (sg.lanes != SpiLanes::Single && sg.lanes != SpiLanes::Dual && sg.lanes != SpiLanes::Quad) {
            return SpiStatus::InvalidArgument;
        }
        if (sg.tx.empty() && sg.rx.empty()) {
            return SpiStatus::InvalidArgument;
        }
        if (!sg.tx.empty() && !sg.rx.empty() &&
            (sg.lanes != SpiLanes::Single || sg.tx.size() != sg.rx.size())) {
            return SpiStatus::InvalidArgument;
        }
    }
    if (caps.max_segments == 0 || segs.size() > caps.max_segments) return SpiStatus::NotSupported;
    if (dev.cs >= caps.num_cs) return SpiStatus::NotSupported;
    if ((caps.modes & spi_mode_bit(dev.mode)) == 0) return SpiStatus::NotSupported;
    if (dev.cs_active_high && !caps.cs_active_high) return SpiStatus::NotSupported;
    if (dev.lsb_first && !caps.lsb_first) return SpiStatus::NotSupported;
    for (size_t i = 0; i < segs.size(); ++i) {
        if ((caps.lanes & spi_lane_bit(segs[i].lanes)) == 0) return SpiStatus::NotSupported;
        if (segs[i].release_cs_after && i + 1 < segs.size() && !caps.cs_release_between_segments) {
            return SpiStatus::NotSupported;
        }
    }
    return SpiStatus::Ok;
}

struct SpiConfig {
    uint32_t frequency_hz{10'000'000};
    SpiMode  mode{SpiMode::Mode0};
    bool     lsb_first{false};
    bool     use_dma{true};    // deprecated [SPEC-HAL-11]: the bus driver's DMA rule decides
    bool     dual_spi{false};  // deprecated [SPEC-HAL-11]: SpiSegment::lanes
};

/*
 * SPI Transfer Result Descriptor (Output Queue element)
 */
struct SpiResult {
    SpiStatus status{SpiStatus::Ok};
    size_t    transferred_bytes{0};
    uint64_t  timestamp_us{0};
    uint32_t  queue_latency_us{0};
    uint32_t  bus_duration_us{0};
};

/*
 * SPI Transfer Request Descriptor (Input Queue element)
 */
struct SpiRequest : public AsyncTransactionMeta<SpiResult> {
    std::span<const uint8_t> tx_data{};
    std::span<uint8_t>       rx_data{};
    std::span<const uint8_t> tx_data_link_b{};   // deprecated [SPEC-HAL-11]: second link
    std::span<uint8_t>       rx_data_link_b{};   // deprecated [SPEC-HAL-11]: second link
    uint32_t                 cs_pin{0};          // deprecated [SPEC-HAL-11]: SpiDevice::cs
    bool                     auto_cs{true};      // deprecated [SPEC-HAL-11]
    bool                     is_dual_spi{false}; // deprecated [SPEC-HAL-11]: SpiLanes::Dual
    // [SPEC-HAL-08] Device transaction: when device != nullptr the driver runs `segments`
    // (both owned by the awaiting frame until completion) and ignores the legacy fields.
    const SpiDevice*            device{nullptr};
    std::span<const SpiSegment> segments{};

    // Dedicated RX/TX callbacks when hardware operation finishes
    etl::delegate<void(const SpiResult&)> on_tx_complete{};
    etl::delegate<void(const SpiResult&)> on_rx_complete{};

    void notify_tx_completion(const SpiResult& result) const noexcept {
        if (on_tx_complete.is_valid()) {
            on_tx_complete(result);
        }
    }

    void notify_rx_completion(const SpiResult& result) const noexcept {
        if (on_rx_complete.is_valid()) {
            on_rx_complete(result);
        }
    }
};

// @impl [SPEC-HAL-02] docs/DESIGN_SPECIFICATION.md#spec-hal-02
// @status Complete
template <size_t QueueDepth = 16>
class AsyncSpiDriver : public AsyncDriverBase<SpiRequest, SpiResult, QueueDepth> {
public:
    virtual bool init(const SpiConfig& config) = 0;
    virtual void select(bool active) = 0;

    virtual bool set_frequency(uint32_t frequency_hz) { (void)frequency_hz; return true; }

    // [SPEC-HAL-08] What this bus driver supports; the default refuses every device transaction.
    virtual SpiCapabilities capabilities() const noexcept { return {}; }

    /*
     * [SPEC-HAL-08] Device transaction awaiter: checked against capabilities() in await_ready
     * (a refusal never suspends or reaches the hardware), submitted in await_suspend.
     * The segment array is caller-owned (awaiting frame) or, for the helpers, owned here.
     */
    // @impl [SPEC-HAL-08] docs/DESIGN_SPECIFICATION.md
    struct AsyncTransactionAwaiter {
        AsyncSpiDriver&             driver;
        const SpiDevice&            device;
        std::span<const SpiSegment> caller_segments{};
        // Helpers: segment 0 is a command byte (own storage) or a caller tx span; segment 1 caller's.
        uint8_t                     helper{0};   // 0 = caller segments, 1 = cmd + seg1, 2 = tx0 + seg1
        uint8_t                     cmd{0};
        std::span<const uint8_t>    tx0{};
        SpiSegment                  seg1{};
        std::array<SpiSegment, 2>   own_segments{};
        SpiRequest                  request{};
        SpiResult                   result{};

        std::span<const SpiSegment> segments() const noexcept {
            return helper == 0 ? caller_segments : std::span<const SpiSegment>(own_segments);
        }

        // Runs on the awaiter's final address, so &cmd stays valid until resume.
        bool await_ready() noexcept {
            if (helper != 0) {
                own_segments[0] = SpiSegment::write(helper == 1 ? std::span<const uint8_t>(&cmd, 1) : tx0);
                own_segments[1] = seg1;
            }
            result.status = check_spi_transaction(driver.capabilities(), device, segments());
            return result.status != SpiStatus::Ok;   // refused: resume at once, no hardware
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            request.device = &device;
            request.segments = segments();
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.submit_request(request)) {
                result.status = SpiStatus::QueueFull;
                return false; // resume immediately; never resume inline
            }
            return true;
        }

        SpiResult await_resume() noexcept { return result; }
    };

    // One transaction of segments on one device; CS held between segments.
    AsyncTransactionAwaiter transfer_async(const SpiDevice& dev,
                                           std::span<const SpiSegment> segs) noexcept {
        return AsyncTransactionAwaiter{*this, dev, segs};
    }

    // tx on one lane, then rx on rx_lanes, CS held (command/address, then data).
    AsyncTransactionAwaiter write_then_read_async(const SpiDevice& dev, std::span<const uint8_t> tx,
                                                  std::span<uint8_t> rx,
                                                  SpiLanes rx_lanes = SpiLanes::Single) noexcept {
        AsyncTransactionAwaiter a{*this, dev};
        a.helper = 2;
        a.tx0 = tx;
        a.seg1 = SpiSegment::read(rx, rx_lanes);
        return a;
    }

    // Register burst read: (reg | read_flag), then out.size() bytes; full status returned.
    AsyncTransactionAwaiter read_regs_async(const SpiDevice& dev, uint8_t reg, std::span<uint8_t> out,
                                            uint8_t read_flag = 0x80) noexcept {
        AsyncTransactionAwaiter a{*this, dev};
        a.helper = 1;
        a.cmd = static_cast<uint8_t>(reg | read_flag);
        a.seg1 = SpiSegment::read(out);
        return a;
    }

    // Register burst write: (reg & write_mask), then in; full status returned.
    AsyncTransactionAwaiter write_regs_async(const SpiDevice& dev, uint8_t reg,
                                             std::span<const uint8_t> in,
                                             uint8_t write_mask = 0x7F) noexcept {
        AsyncTransactionAwaiter a{*this, dev};
        a.helper = 1;
        a.cmd = static_cast<uint8_t>(reg & write_mask);
        a.seg1 = SpiSegment::write(in);
        return a;
    }

    /*
     * C++20 Coroutine Async Transfer Awaiter
     */
    struct AsyncTransferAwaiter {
        AsyncSpiDriver& driver;
        SpiRequest      request;
        SpiResult       result{};

        AsyncTransferAwaiter(AsyncSpiDriver& drv,
                             std::span<const uint8_t> tx,
                             std::span<uint8_t> rx,
                             bool dual = false)
            : driver(drv) {
            request.tx_data = tx;
            request.rx_data = rx;
            request.is_dual_spi = dual;
        }

        bool await_ready() const noexcept {
            return request.tx_data.empty() && request.rx_data.empty();
        }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.submit_request(request)) {
                result.status = SpiStatus::QueueFull;
                return false; // resume immediately; never resume inline
            }
            return true;
        }

        SpiResult await_resume() noexcept {
            return result;
        }
    };

    AsyncTransferAwaiter transfer_async(std::span<const uint8_t> tx, std::span<uint8_t> rx) noexcept {
        return AsyncTransferAwaiter(*this, tx, rx, false);
    }

    /*
     * C++20 Coroutine Async Register Transfer Awaiter (Zero-Heap, Frame-Embedded Buffers)
     */
    struct AsyncRegTransferAwaiter {
        AsyncSpiDriver&        driver;
        std::array<uint8_t, 2> tx_buf{};
        std::array<uint8_t, 2> rx_buf{};
        SpiRequest             request{};
        SpiResult              result{};

        AsyncRegTransferAwaiter(AsyncSpiDriver& drv, uint8_t cmd, uint8_t val = 0)
            : driver(drv), tx_buf{cmd, val} {}

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> handle) noexcept {
            request.tx_data = tx_buf;
            request.rx_data = rx_buf;
            request.coro_handle = handle;
            request.result_slot = &result;
            if (!driver.submit_request(request)) {
                result.status = SpiStatus::QueueFull;
                return false; // resume immediately; never resume inline
            }
            return true;
        }

        uint8_t await_resume() noexcept {
            return rx_buf[1];
        }
    };

    // Deprecated [SPEC-HAL-11]: these three byte helpers return only the data byte; use
    // read_regs_async / write_regs_async, which return the status as well.
    AsyncRegTransferAwaiter transfer_reg_async(uint8_t cmd, uint8_t val = 0) noexcept {
        return AsyncRegTransferAwaiter(*this, cmd, val);
    }

    AsyncRegTransferAwaiter read_reg_async(uint8_t reg, uint8_t read_mask = 0x80) noexcept {
        return AsyncRegTransferAwaiter(*this, static_cast<uint8_t>(reg | read_mask), 0x00);
    }

    AsyncRegTransferAwaiter write_reg_async(uint8_t reg, uint8_t val, uint8_t write_mask = 0x7F) noexcept {
        return AsyncRegTransferAwaiter(*this, static_cast<uint8_t>(reg & write_mask), val);
    }

    // @impl [SPEC-HAL-11] docs/DESIGN_SPECIFICATION.md
    // Deprecated [SPEC-HAL-11]: two parallel links, implemented by no target. Use
    // transfer_async(dev, segments) with SpiLanes::Dual for a 2-lane link.
    AsyncTransferAwaiter transfer_dual_async(std::span<const uint8_t> tx_a,
                                             std::span<const uint8_t> tx_b,
                                             std::span<uint8_t> rx_a,
                                             std::span<uint8_t> rx_b) noexcept {
        AsyncTransferAwaiter awaiter(*this, tx_a, rx_a, true);
        awaiter.request.tx_data_link_b = tx_b;
        awaiter.request.rx_data_link_b = rx_b;
        return awaiter;
    }
};

using ISpi = AsyncSpiDriver<16>;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_SPI_HPP
