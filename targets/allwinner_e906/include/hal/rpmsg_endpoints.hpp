/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * E906 RPMsg endpoints over one message box channel ([SPEC-E906-IPC-04]):
 *   abstractx-tlp   (1024): 64-byte TLPs both ways, batched per message
 *   abstractx-trace (1025): CTF packets E906 -> Linux, written in place
 * Both share the host buffers Linux posts; trace keeps kTlpReserve free for TLPs.
 */

#pragma once

#include "abstractx/trace/tracer.hpp"
#include "asp_tlp64.hpp"
#include "spsc_tlp_ring.hpp"
#include "hal/rpmsg.hpp"

namespace hal {

// Host buffers the trace endpoint must leave free for TLPs.
inline constexpr uint16_t kTlpReserve = 4;

// Name-service announcement and host-address learning shared by both endpoints.
class RpmsgEndpoint {
public:
    RpmsgEndpoint(Rpmsg& rpmsg, uint32_t addr, const char* name) noexcept
        : rpmsg_(rpmsg), addr_(addr), name_(name) {}
    virtual ~RpmsgEndpoint() = default;

    void attach() noexcept;
    // Announce once Linux's virtio driver is up. Rpmsg::poll() is called once
    // per loop by the platform, not per endpoint.
    void service() noexcept;
    bool host_connected() const noexcept { return host_addr_ != kNoHost; }
    uint32_t address() const noexcept { return addr_; }

protected:
    static constexpr uint32_t kNoHost = 0xFFFFFFFFu;
    virtual void on_message(const RpmsgMessage& msg) noexcept { (void)msg; }

    Rpmsg&            rpmsg_;
    volatile uint32_t host_addr_{kNoHost};

private:
    static void dispatch(const RpmsgMessage& msg, void* self) noexcept;

    uint32_t    addr_;
    const char* name_;
    bool        announced_{false};
};

// CTF trace packets written directly into host buffers.
// @impl [SPEC-E906-IPC-04] [SPEC-TRACE-07] targets/allwinner_e906/SPECIFICATION.md
class RpmsgTraceTransport final : public RpmsgEndpoint, public abstractx::trace::ITraceTransport {
public:
    static constexpr uint32_t kEndpoint = 1025;
    explicit RpmsgTraceTransport(Rpmsg& rpmsg) noexcept : RpmsgEndpoint(rpmsg, kEndpoint, "abstractx-trace") {}

    // Empty unless a host said hello in the last 3 s, or when only the TLP reserve is left.
    std::span<uint8_t> acquire(size_t max_bytes) noexcept override;
    void commit(std::span<uint8_t> buffer, size_t used_bytes) noexcept override;
    // @impl [SPEC-E906-IPC-05] targets/allwinner_e906/SPECIFICATION.md
    bool listening() const noexcept;

private:
    void on_message(const RpmsgMessage& msg) noexcept override;

    RpmsgTxBuffer     cur_{};
    volatile uint64_t last_hello_ticks_{0};
    bool              heard_{false};
};

// 64-byte TLPs, batched (payload / 64 per message, 7 for 496-byte payloads).
// @impl [SPEC-E906-IPC-04] [SPEC-ARCH-08] targets/allwinner_e906/SPECIFICATION.md
class RpmsgTlpChannel final : public RpmsgEndpoint {
public:
    static constexpr uint32_t kEndpoint = 1024;
    static constexpr size_t   kTlpBytes = sizeof(asp_tlp64_t);
    explicit RpmsgTlpChannel(Rpmsg& rpmsg) noexcept : RpmsgEndpoint(rpmsg, kEndpoint, "abstractx-tlp") {}

    // In-place, zeroed slot in the current batch (zero-copy), or nullptr if no
    // buffer. Slots are not 64-byte aligned: use the unaligned TLP type.
    asp_tlp64_unaligned_t* emplace() noexcept;
    // Copy an already-built TLP into a slot (one 64-byte copy).
    bool send(const abstractx::Tlp64& tlp) noexcept;
    // Commit a partial batch. Called every loop.
    void flush() noexcept;

    // Host -> E906 TLPs are pushed here (counted as dropped if unset or full).
    void set_ingress_ring(abstractx::SpscTlpRing<>* ring) noexcept { ingress_ = ring; }

    uint32_t tx_dropped() const noexcept { return tx_dropped_; }
    uint32_t rx_dropped() const noexcept { return rx_dropped_; }
    uint32_t rx_rejected() const noexcept { return rx_rejected_; }

protected:
    void on_message(const RpmsgMessage& msg) noexcept override;

private:
    RpmsgTxBuffer                cur_{};
    size_t                       count_{0};
    abstractx::SpscTlpRing<>*    ingress_{nullptr};
    uint32_t                     tx_dropped_{0};
    uint32_t                     rx_dropped_{0};
    uint32_t                     rx_rejected_{0};
};

} // namespace hal
