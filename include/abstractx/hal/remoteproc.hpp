/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: Linux RemoteProc & Diagnostics Interface
 * -------------------------------------------------------
 * RemoteProc trace buffer and resource management contracts.
 */

#ifndef ABSTRACTX_HAL_REMOTEPROC_HPP
#define ABSTRACTX_HAL_REMOTEPROC_HPP

#include <cstdint>
#include <cstddef>
#include <span>

namespace abstractx::hal {

class IRemoteprocTrace {
public:
    virtual ~IRemoteprocTrace() = default;

    virtual void init() = 0;
    virtual void puts(const char* str) = 0;
    virtual void write(std::span<const uint8_t> data) = 0;
    virtual void flush() = 0;
};

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_REMOTEPROC_HPP
