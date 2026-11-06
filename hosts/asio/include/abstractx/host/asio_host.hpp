/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Boost.Asio host for AbstractX (Linux): step() runs in a completion handler of an asio::io_context;
 * a deadline arms one steady_timer, a wake posts the handler. Handlers only: no Asio coroutines
 * ([SPEC-ARCH-15] no mixing). Same API as the POSIX host, so the host conformance checks run on it.
 * @impl [SPEC-ARCH-15] docs/DESIGN_SPECIFICATION.md (Boost.Asio host)
 */
#ifndef ABSTRACTX_HOST_ASIO_HOST_HPP
#define ABSTRACTX_HOST_ASIO_HOST_HPP

#include <cstdint>

namespace boost::asio { class io_context; }

namespace abstractx::asio_host {

using DoneFn = bool (*)(void* ctx) noexcept;

// Drive AbstractX from an io_context the application already runs (shares its loop). attach()
// registers the wake hooks and posts the first pump; detach() removes them. One attached context.
void attach(boost::asio::io_context& ioc) noexcept;
void detach() noexcept;

// Own io_context on the calling thread, until done(ctx) (checked after every step()) or give_up_us.
bool run_until(DoneFn done, void* ctx, uint64_t give_up_us) noexcept;
void run() noexcept;     // until stop()
void stop() noexcept;    // from any thread

// Times the host went idle (a pump that did not post itself again) since the last reset.
uint32_t waits(bool reset = false) noexcept;

} // namespace abstractx::asio_host

#endif // ABSTRACTX_HOST_ASIO_HOST_HPP
