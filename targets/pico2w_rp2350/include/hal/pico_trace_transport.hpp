/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Pico 2 W Zero-Copy lwIP UDP Trace Transport
 * --------------------------------------------
 * Borrows lwIP PBUF_POOL pbufs directly from the static memp pool and transmits
 * them over UDP without copying between the CTF trace engine and network driver.
 */

#ifndef ABSTRACTX_TARGETS_PICO2W_TRACE_TRANSPORT_HPP
#define ABSTRACTX_TARGETS_PICO2W_TRACE_TRANSPORT_HPP

#include "abstractx/trace/tracer.hpp"
#include <cstdint>
#include <cstddef>
#include <span>

#ifdef PICO_ON_DEVICE
#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#else
// Mock lwIP types and declarations for host simulation verification [SPEC-PICO-TRACE-01]
struct pbuf {
    struct pbuf* next{nullptr};
    void* payload{nullptr};
    uint16_t tot_len{0};
    uint16_t len{0};
    uint8_t type_internal{0};
    uint8_t flags{0};
    uint16_t ref{1};
};

struct ip_addr_t {
    uint32_t addr{0};
};

struct udp_pcb {
    uint8_t dummy{0};
};

typedef enum {
    PBUF_TRANSPORT,
    PBUF_IP,
    PBUF_LINK,
    PBUF_RAW_TX,
    PBUF_RAW
} pbuf_layer;

typedef enum {
    PBUF_RAM,
    PBUF_ROM,
    PBUF_REF,
    PBUF_POOL
} pbuf_type;

extern "C" {
    struct pbuf* pbuf_alloc(pbuf_layer layer, uint16_t length, pbuf_type type);
    void pbuf_realloc(struct pbuf* p, uint16_t size);
    uint8_t pbuf_free(struct pbuf* p);
    uint8_t udp_sendto(struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* dst_ip, uint16_t dst_port);
}
#endif

namespace abstractx::hal {

// Zero-copy UDP trace transport using lwIP PBUF_POOL pbufs.
// @impl [SPEC-PICO-TRACE-01] [SPEC-TRACE-07] [SPEC-ARCH-08] targets/pico2w_rp2350/SPECIFICATION.md#37-zero-copy-trace--telemetry-over-udp-spec-pico-trace-01
class PicoUdpTraceTransport final : public abstractx::trace::ITraceTransport {
public:
    static constexpr uint16_t kDefaultPort = 9870;

    PicoUdpTraceTransport() noexcept = default;

    void init(udp_pcb* pcb, const ip_addr_t& dest_ip, uint16_t port = kDefaultPort) noexcept {
        pcb_ = pcb;
        dest_ip_ = dest_ip;
        port_ = port;
    }

    // Acquire a PBUF_POOL buffer directly from the lwIP static memp pool.
    // If the pool is exhausted or non-contiguous, returns an empty span (non-blocking).
    std::span<uint8_t> acquire(size_t max_bytes) noexcept override {
        if (!pcb_) {
            return {};
        }
        if (cur_pbuf_) {
            return {reinterpret_cast<uint8_t*>(cur_pbuf_->payload), cur_pbuf_->len};
        }
        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, static_cast<uint16_t>(max_bytes), PBUF_POOL);
        if (!p) {
            ++alloc_failed_;
            return {};
        }
        if (p->next != nullptr) {
            // Reject non-contiguous chained pbufs to guarantee 0-copy single-packet invariant
            pbuf_free(p);
            ++chain_rejected_;
            return {};
        }
        cur_pbuf_ = p;
        ++acquires_;
        return {reinterpret_cast<uint8_t*>(p->payload), p->len};
    }

    // Commit writes the packet in place: reallocates pbuf to used size, sends over UDP,
    // and releases our reference.
    void commit(std::span<uint8_t> buffer, size_t used_bytes) noexcept override {
        if (!cur_pbuf_ || buffer.data() != cur_pbuf_->payload) {
            return;
        }
        struct pbuf* p = cur_pbuf_;
        cur_pbuf_ = nullptr;

        if (used_bytes > 0 && pcb_) {
            pbuf_realloc(p, static_cast<uint16_t>(used_bytes));
            udp_sendto(pcb_, p, &dest_ip_, port_);
            ++commits_;
            bytes_sent_ += used_bytes;
        }
        pbuf_free(p);
    }

    struct pbuf* current_pbuf() const noexcept { return cur_pbuf_; }
    uint32_t acquires() const noexcept { return acquires_; }
    uint32_t commits() const noexcept { return commits_; }
    uint32_t alloc_failed() const noexcept { return alloc_failed_; }
    uint32_t chain_rejected() const noexcept { return chain_rejected_; }
    size_t   bytes_sent() const noexcept { return bytes_sent_; }

private:
    udp_pcb*     pcb_{nullptr};
    ip_addr_t    dest_ip_{};
    uint16_t     port_{kDefaultPort};
    struct pbuf* cur_pbuf_{nullptr};

    uint32_t     acquires_{0};
    uint32_t     commits_{0};
    uint32_t     alloc_failed_{0};
    uint32_t     chain_rejected_{0};
    size_t       bytes_sent_{0};
};

} // namespace abstractx::hal

#endif // ABSTRACTX_TARGETS_PICO2W_TRACE_TRANSPORT_HPP
