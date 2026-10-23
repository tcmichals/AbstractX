/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: shared status vocabulary for every bus ([SPEC-HAL-07]).
 * Each bus keeps its own *Status enum and maps onto this one with to_io_status().
 */

#ifndef ABSTRACTX_HAL_IO_STATUS_HPP
#define ABSTRACTX_HAL_IO_STATUS_HPP

#include <cstdint>

namespace abstractx::hal {

// @impl [SPEC-HAL-07] docs/DESIGN_SPECIFICATION.md
enum class IoStatus : uint8_t {
    Ok = 0,
    QueueFull,
    NotSupported,
    InvalidArgument,
    Cancelled,
    Timeout,
    NackAddress,
    NackData,
    ArbitrationLost,
    BusError,
    DmaError,
    Crc,
    Overrun,
    Framing,
    Parity,
    Break,
};

// The completion a cancelled request receives: its bus's Cancelled status ([SPEC-HAL-07]).
template <typename TResult>
constexpr TResult make_cancelled_result() noexcept {
    TResult r{};
    using StatusEnum = decltype(r.status);
    static_assert(requires { StatusEnum::Cancelled; },
                  "[SPEC-HAL-07]: a driver result status needs a Cancelled value");
    r.status = StatusEnum::Cancelled;
    return r;
}

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_IO_STATUS_HPP
