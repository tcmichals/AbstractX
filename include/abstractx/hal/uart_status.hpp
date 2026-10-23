/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: UART status codes and their IoStatus mapping ([SPEC-HAL-06], [SPEC-HAL-07]).
 * Separate header so the receive ring (uart_rx.hpp) and the driver (uart.hpp) share it.
 */

#ifndef ABSTRACTX_HAL_UART_STATUS_HPP
#define ABSTRACTX_HAL_UART_STATUS_HPP

#include <cstdint>

#include "io_status.hpp"

namespace abstractx::hal {

enum class UartStatus : uint8_t {
    Ok = 0,
    Timeout = 1,
    FramingError = 2,
    OverrunError = 3,
    QueueFull = 4,
    NotSupported = 5,   // [SPEC-HAL-06]: the request is outside the driver's capabilities
    Cancelled = 6,      // [SPEC-HAL-07]
    ParityError = 7,    // [SPEC-HAL-10]
    BreakError = 8,     // [SPEC-HAL-10]
    InvalidArgument = 9 // [SPEC-HAL-10]
};

// @impl [SPEC-HAL-07] docs/DESIGN_SPECIFICATION.md
constexpr IoStatus to_io_status(UartStatus s) noexcept {
    switch (s) {
    case UartStatus::Ok: return IoStatus::Ok;
    case UartStatus::Timeout: return IoStatus::Timeout;
    case UartStatus::FramingError: return IoStatus::Framing;
    case UartStatus::OverrunError: return IoStatus::Overrun;
    case UartStatus::QueueFull: return IoStatus::QueueFull;
    case UartStatus::NotSupported: return IoStatus::NotSupported;
    case UartStatus::Cancelled: return IoStatus::Cancelled;
    case UartStatus::ParityError: return IoStatus::Parity;
    case UartStatus::BreakError: return IoStatus::Break;
    case UartStatus::InvalidArgument: return IoStatus::InvalidArgument;
    }
    return IoStatus::BusError;
}

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_UART_STATUS_HPP
