/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/rpmsg_trace_transport.hpp"

namespace hal {

void RpmsgTraceTransport::attach() noexcept {
    (void)rpmsg_.register_endpoint(kEndpoint, &RpmsgTraceTransport::on_host_message, this);
}

void RpmsgTraceTransport::service() noexcept {
    if (!announced_ && rpmsg_.is_driver_ready()) {
        announced_ = rpmsg_.announce_service(kServiceName, kEndpoint);
    }
    (void)rpmsg_.poll();
}

void RpmsgTraceTransport::on_host_message(const RpmsgMessage& msg, void* self) noexcept {
    static_cast<RpmsgTraceTransport*>(self)->host_addr_ = msg.src;
}

std::span<uint8_t> RpmsgTraceTransport::acquire(size_t max_bytes) noexcept {
    if (!host_connected()) {
        return {};
    }
    std::span<uint8_t> buf = rpmsg_.tx_acquire();
    return buf.size() > max_bytes ? buf.first(max_bytes) : buf;
}

void RpmsgTraceTransport::commit(std::span<uint8_t> buffer, size_t used_bytes) noexcept {
    (void)buffer; // the buffer lent by tx_acquire(); Rpmsg tracks its descriptor
    (void)rpmsg_.tx_commit(static_cast<uint16_t>(used_bytes), kEndpoint, host_addr_);
}

} // namespace hal
