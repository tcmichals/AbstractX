#include <coroutine>
#include <span>
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <atomic>
#include "resource_table.h"

namespace hal {

/*
 * RPMsg Packet View & Callback Definition
 */
struct RpmsgMessage {
    uint32_t src;
    uint32_t dst;
    uint16_t len;
    const uint8_t *data;
    uint16_t desc_idx;
};

using EndpointCallback = void (*)(const RpmsgMessage &msg, void *user_data);

// A host buffer lent by Rpmsg::tx_acquire(): write the payload in place, then
// pass the same token to tx_commit(). Empty payload = no buffer.
struct RpmsgTxBuffer {
    std::span<uint8_t> payload{};
    uint16_t           desc_idx{0};
    explicit operator bool() const noexcept { return !payload.empty(); }
};

/*
 * ============================================================================
 * IRpmsg: Pure Virtual Interface for Inter-Processor Communication (IPC)
 * ============================================================================
 * Allows upper-layer tasks and coroutines to communicate without knowing
 * whether the underlying transport is standard Linux VirtIO or Lite-Metal.
 */
// @impl [SPEC-E906-IPC-01] targets/allwinner_e906/SPECIFICATION.md
class IRpmsg {
public:
    virtual ~IRpmsg() = default;

    virtual void init(const struct rpmsg_resource_table *rsc) noexcept = 0;
    virtual bool is_driver_ready() noexcept = 0;
    virtual bool register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data = nullptr) noexcept = 0;
    virtual bool announce_service(const char *name, uint32_t addr) noexcept = 0;
    virtual bool poll() noexcept = 0;
    virtual bool reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept = 0;
    virtual void enable_rx_kick(bool enable) noexcept = 0;
    virtual uint32_t get_rx_count() noexcept = 0;
    virtual uint32_t get_tx_count() noexcept = 0;
    virtual bool is_rx_pending() noexcept = 0;
    virtual uint32_t get_ring_errors() noexcept = 0;
    virtual uint32_t get_tx_full_waits() noexcept = 0;
    virtual uint32_t get_mbox_full_waits() noexcept = 0;
    virtual uint32_t get_host_kicks() noexcept = 0;

    /* C++20 Asynchronous Coroutine Awaiter: Suspends until Linux kicks MSGBOX */
    // One waiter at a time ([SPEC-E906-CORO-01]): await_suspend() returns false
    // (no suspend) if another coroutine already holds the wake slot.
    struct AsyncRxAwaiter {
        IRpmsg *driver;
        void   *registered{nullptr};
        bool await_ready() const noexcept;
        bool await_suspend(std::coroutine_handle<> handle) noexcept;
        bool await_resume() noexcept;
    };

    inline AsyncRxAwaiter async_receive() noexcept {
        return AsyncRxAwaiter{this};
    }
};

/*
 * ============================================================================
 * Rpmsg: Standard Linux Kernel VirtIO RemoteProc Driver
 * ============================================================================
 * Linux virtio_rpmsg_bus over the SRAM RPMsg pool ([SPEC-E906-IPC-02]). The
 * D-cache is off ([SPEC-E906-CACHE-02]), so there is no cache maintenance.
 * Every wait is bounded; host addresses and lengths are validated.
 */
// @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
class Rpmsg : public IRpmsg {
public:
    static constexpr size_t MAX_ENDPOINTS = 8;

    Rpmsg() noexcept = default;
    ~Rpmsg() override = default;

    void init(const struct rpmsg_resource_table *rsc) noexcept override;
    bool is_driver_ready() noexcept override;
    bool register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data = nullptr) noexcept override;
    bool announce_service(const char *name, uint32_t addr) noexcept override;
    bool poll() noexcept override;
    bool reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept override;
    void enable_rx_kick(bool enable) noexcept override;
    uint32_t get_rx_count() noexcept override;
    uint32_t get_tx_count() noexcept override;
    bool is_rx_pending() noexcept override;
    uint32_t get_ring_errors() noexcept override;
    uint32_t get_tx_full_waits() noexcept override;
    uint32_t get_mbox_full_waits() noexcept override;
    uint32_t get_host_kicks() noexcept override;
    // A kick the full message-box FIFO refused, still to be sent ([SPEC-E906-IPC-02]).
    bool kick_owed() noexcept;

    // Zero-copy transmit ([SPEC-E906-IPC-04]): borrow the next free host
    // buffer if at least `min_free_after` would remain free (budget), write it
    // in place, then commit. Several buffers may be outstanding.
    RpmsgTxBuffer tx_acquire(uint16_t min_free_after = 0) noexcept;
    bool tx_commit(const RpmsgTxBuffer &buf, uint16_t used_len, uint32_t src, uint32_t dst) noexcept;
    // Hand back the most recently acquired, unused buffer (never commit 0 bytes).
    bool tx_cancel(const RpmsgTxBuffer &buf) noexcept;
    // Retry a kick the full message box FIFO refused ([SPEC-E906-IPC-02]); call every loop.
    void flush_kick() noexcept;
    // Host buffers posted in vring[0] and not yet taken.
    uint16_t tx_free_buffers() noexcept;

    // Static convenience facade delegating to the globally active driver
    static void set_active_driver(IRpmsg *driver) noexcept;
    static IRpmsg* get_active_driver() noexcept;

    static void init_active(const struct rpmsg_resource_table *rsc) noexcept {
        get_active_driver()->init(rsc);
    }
    static bool reply_active(const RpmsgMessage &msg, const void *payload, uint16_t len) noexcept {
        return get_active_driver()->reply(msg, payload, len);
    }
};

/*
 * ============================================================================
 * RpmsgLiteMetal: Zero-Overhead Bare-Metal / SRAM Driver
 * ============================================================================
 * Designed for ultra-low latency direct SRAM buffers or peer-to-peer messaging
 * without Linux kernel VirtIO overhead and ZERO D-Cache maintenance operations.
 */
// @impl [SPEC-E906-IPC-03] targets/allwinner_e906/SPECIFICATION.md
class RpmsgLiteMetal : public IRpmsg {
public:
    static constexpr size_t MAX_ENDPOINTS = 8;

    RpmsgLiteMetal() noexcept = default;
    ~RpmsgLiteMetal() override = default;

    void init(const struct rpmsg_resource_table *rsc) noexcept override;
    bool is_driver_ready() noexcept override;
    bool register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data = nullptr) noexcept override;
    bool announce_service(const char *name, uint32_t addr) noexcept override;
    bool poll() noexcept override;
    bool reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept override;
    void enable_rx_kick(bool enable) noexcept override;
    uint32_t get_rx_count() noexcept override;
    uint32_t get_tx_count() noexcept override;
    bool is_rx_pending() noexcept override;
    uint32_t get_ring_errors() noexcept override;
    uint32_t get_tx_full_waits() noexcept override;
    uint32_t get_mbox_full_waits() noexcept override;
    uint32_t get_host_kicks() noexcept override;
};

// Message box doorbell ([SPEC-E906-IRQ-04]): the ISR masks the level-triggered read
// interrupt and sets a flag. take() returns and clears it; arm() unmasks after draining.
bool rpmsg_take_doorbell() noexcept;
void rpmsg_arm_doorbell() noexcept;
bool rpmsg_doorbell_pending() noexcept;
// Before the core sleeps: kicks on, then true (and the doorbell flag set) if Linux posted a
// descriptor while kicks were suppressed ([SPEC-E906-IRQ-04] rule 2). Interrupts masked.
bool rpmsg_pending_before_sleep() noexcept;
uint32_t rpmsg_doorbell_irqs() noexcept;

#if !defined(__riscv)
// Host tests only: the host-physical == local window that vrings and buffers may use
// (on the E906 it is the fixed RPMsg pool, [SPEC-E906-IPC-02]).
void rpmsg_set_host_window(uintptr_t base, size_t size) noexcept;
#endif

} // namespace hal
