/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "hal/rpmsg_endpoints.hpp"
#include "abstractx/trace/runtime_stats.hpp"


namespace hal {

void RpmsgEndpoint::attach() noexcept {
    (void)rpmsg_.register_endpoint(addr_, &RpmsgEndpoint::dispatch, this);
}

void RpmsgEndpoint::service() noexcept {
    if (!announced_ && rpmsg_.is_driver_ready()) {
        announced_ = rpmsg_.announce_service(name_, addr_);
    }
}

void RpmsgEndpoint::dispatch(const RpmsgMessage& msg, void* self) noexcept {
    auto* ep = static_cast<RpmsgEndpoint*>(self);
    ep->host_addr_ = msg.src; // first message tells us where Linux listens
    ep->on_message(msg);
}

// A host that reads the trace endpoint renews this lease once a second ([SPEC-E906-IPC-05]).
// @impl [SPEC-E906-IPC-05] targets/allwinner_e906/SPECIFICATION.md
// Lease on the runtime's cycle clock (mcycle on the E906); without a clock, a hello is enough.
void RpmsgTraceTransport::on_message(const RpmsgMessage& msg) noexcept {
    (void)msg;
    last_hello_ticks_ = abstractx::stats::now_cycles();
    heard_ = true;
}

bool RpmsgTraceTransport::listening() const noexcept {
    const uint64_t lease = 3ULL * abstractx::stats::g_state.hz;
    return heard_ && host_connected() &&
           (lease == 0 || abstractx::stats::now_cycles() - last_hello_ticks_ < lease);
}

std::span<uint8_t> RpmsgTraceTransport::acquire(size_t max_bytes) noexcept {
    if (!listening() || cur_) {
        return {};
    }
    cur_ = rpmsg_.tx_acquire(kTlpReserve);
    return cur_.payload.size() > max_bytes ? cur_.payload.first(max_bytes) : cur_.payload;
}

void RpmsgTraceTransport::commit(std::span<uint8_t> buffer, size_t used_bytes) noexcept {
    (void)buffer; // always cur_.payload (or a prefix of it)
    (void)rpmsg_.tx_commit(cur_, static_cast<uint16_t>(used_bytes), address(), host_addr_);
    cur_ = {};
}

asp_tlp64_unaligned_t* RpmsgTlpChannel::emplace() noexcept {
    if (cur_ && (count_ + 1) * kTlpBytes > cur_.payload.size()) {
        flush();
    }
    if (!cur_) {
        if (!host_connected()) {
            ++tx_dropped_;
            return nullptr;
        }
        cur_ = rpmsg_.tx_acquire(0);
        count_ = 0;
        if (cur_.payload.size() < kTlpBytes) {
            if (cur_) {
                (void)rpmsg_.tx_commit(cur_, 0, address(), host_addr_);
                cur_ = {};
            }
            ++tx_dropped_;
            return nullptr;
        }
    }
    uint8_t* slot = cur_.payload.data() + count_ * kTlpBytes;
    ++count_;
    for (size_t i = 0; i < kTlpBytes; ++i) {
        slot[i] = 0;
    }
    return reinterpret_cast<asp_tlp64_unaligned_t*>(slot);
}

bool RpmsgTlpChannel::send(const abstractx::Tlp64& tlp) noexcept {
    asp_tlp64_unaligned_t* slot = emplace();
    if (!slot) {
        return false;
    }
    tlp_store(slot, tlp.wire);
    return true;
}

void RpmsgTlpChannel::flush() noexcept {
    if (cur_ && count_ > 0) {
        (void)rpmsg_.tx_commit(cur_, static_cast<uint16_t>(count_ * kTlpBytes), address(), host_addr_);
        cur_ = {};
        count_ = 0;
    }
}

void RpmsgTlpChannel::on_message(const RpmsgMessage& msg) noexcept {
    if (msg.len % kTlpBytes != 0) {
        if (msg.len >= kTlpBytes) {
            ++rx_rejected_; // not a whole number of TLPs
        }
        return; // short messages (the reader's hello) carry no TLPs
    }
    for (size_t off = 0; off + kTlpBytes <= msg.len; off += kTlpBytes) {
        abstractx::Tlp64 tlp{};
        tlp_load(&tlp.wire, reinterpret_cast<const asp_tlp64_unaligned_t*>(msg.data + off));
        if (!ingress_ || !ingress_->push(tlp)) {
            ++rx_dropped_;
        }
    }
}

} // namespace hal
