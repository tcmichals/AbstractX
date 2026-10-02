#include <coroutine>
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

/*
 * ============================================================================
 * IRpmsg: Pure Virtual Interface for Inter-Processor Communication (IPC)
 * ============================================================================
 * Allows upper-layer tasks and coroutines to communicate without knowing
 * whether the underlying transport is standard Linux VirtIO or Lite-Metal.
 */
class IRpmsg {
public:
    virtual ~IRpmsg() = default;

    virtual void init(const struct rpmsg_resource_table *rsc) noexcept = 0;
    virtual bool is_driver_ready() noexcept = 0;
    virtual bool register_endpoint(uint32_t addr, EndpointCallback cb, void *user_data = nullptr) noexcept = 0;
    virtual bool announce_service(const char *name, uint32_t addr) noexcept = 0;
    virtual bool poll() noexcept = 0;
    virtual bool reply(const RpmsgMessage &incoming, const void *payload, uint16_t len) noexcept = 0;
    virtual uint32_t get_rx_count() noexcept = 0;
    virtual uint32_t get_tx_count() noexcept = 0;
    virtual bool is_rx_pending() noexcept = 0;

    /* C++20 Asynchronous Coroutine Awaiter: Suspends until Linux kicks MSGBOX */
    struct AsyncRxAwaiter {
        IRpmsg *driver;
        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) noexcept;
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
 * Full compliance with Linux virtio_rpmsg_bus (/dev/rpmsg0).
 * Handles DDR DRAM payload buffers with full XuanTie L1 D-Cache maintenance
 * (dcache.cpa write-back on TX, dcache.iva invalidate on RX).
 */
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
    uint32_t get_rx_count() noexcept override;
    uint32_t get_tx_count() noexcept override;
    bool is_rx_pending() noexcept override;

    // Static convenience facade delegating to the globally active driver
    static void set_active_driver(IRpmsg *driver) noexcept;
    static IRpmsg* get_active_driver() noexcept;

    static void init_active(const struct rpmsg_resource_table *rsc) noexcept {
        get_active_driver()->init(rsc);
    }
    static bool poll_active() noexcept {
        return get_active_driver()->poll();
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
    uint32_t get_rx_count() noexcept override;
    uint32_t get_tx_count() noexcept override;
    bool is_rx_pending() noexcept override;
};

} // namespace hal
