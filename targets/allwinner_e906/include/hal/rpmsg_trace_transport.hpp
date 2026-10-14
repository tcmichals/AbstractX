/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * E906 zero-copy trace transport over RPMsg ([SPEC-E906-IPC-04]).
 * The trace engine writes each CTF packet directly into a host RPMsg buffer
 * (in SRAM or the DMA pool); commit publishes it and kicks the message box.
 */

#pragma once

#include "abstractx/trace/tracer.hpp"
#include "hal/rpmsg.hpp"

namespace hal {

// @impl [SPEC-E906-IPC-04] [SPEC-TRACE-07] targets/allwinner_e906/SPECIFICATION.md
class RpmsgTraceTransport final : public abstractx::trace::ITraceTransport {
public:
    static constexpr uint32_t kEndpoint = 1025;
    static constexpr const char* kServiceName = "abstractx-trace";

    explicit RpmsgTraceTransport(Rpmsg& rpmsg) noexcept : rpmsg_(rpmsg) {}

    // Register the local endpoint. Call after Rpmsg::init().
    void attach() noexcept;

    // Main loop: announce the service once Linux's virtio driver is up, then
    // poll for host messages (the first one tells us the host's address).
    void service() noexcept;

    // Empty until the host has sent its first message (its address is
    // unknown before that) or when Linux has no free buffer posted.
    std::span<uint8_t> acquire(size_t max_bytes) noexcept override;
    void commit(std::span<uint8_t> buffer, size_t used_bytes) noexcept override;

    bool host_connected() const noexcept { return host_addr_ != kNoHost; }

private:
    static constexpr uint32_t kNoHost = 0xFFFFFFFFu;
    static void on_host_message(const RpmsgMessage& msg, void* self) noexcept;

    Rpmsg&            rpmsg_;
    bool              announced_{false};
    volatile uint32_t host_addr_{kNoHost};
};

} // namespace hal
