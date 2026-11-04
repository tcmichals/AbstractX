/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * POSIX (Linux) host for AbstractX: the FreeRTOS host's shape with Linux calls. The calling
 * thread runs AbstractX; it waits in ppoll() on an eventfd with the timeout set to the deadline
 * step() returned, and wake() writes the eventfd. No polling interval ([SPEC-ARCH-15],
 * [SPEC-ARCH-16] executor table: "Linux, POSIX host").
 * @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (POSIX host)
 */
#ifndef ABSTRACTX_HOST_POSIX_HOST_HPP
#define ABSTRACTX_HOST_POSIX_HOST_HPP

#include <cstdint>

namespace abstractx::posix_host {

using DoneFn = bool (*)(void* ctx) noexcept;

// Run AbstractX on the calling thread until done(ctx) is true (checked after every step()) or
// give_up_us of wall time has passed. Returns done(ctx). Registers the wake hooks while running and
// restores the previous ones on return.
bool run_until(DoneFn done, void* ctx, uint64_t give_up_us) noexcept;

// Run until stop() is called from any thread.
void run() noexcept;
void stop() noexcept;

// Times the host blocked in ppoll() since the last reset (diagnostics and the conformance test).
uint32_t waits(bool reset = false) noexcept;

} // namespace abstractx::posix_host

#endif // ABSTRACTX_HOST_POSIX_HOST_HPP
