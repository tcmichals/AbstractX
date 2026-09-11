/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: Asynchronous Mailbox & Doorbell Split-Queue Interface
 * -------------------------------------------------------------------
 * Inter-Core Doorbell and Message Box abstraction for heterogeneous SoCs
 * with request/completion queues, PLIC interrupt callbacks, and coroutine awaiters.
 */

#ifndef ABSTRACTX_HAL_MAILBOX_HPP
#define ABSTRACTX_HAL_MAILBOX_HPP

#include <cstdint>
#include <coroutine>

#include "async_driver.hpp"

namespace abstractx::hal {

enum class MailboxStatus : uint8_t {
    Ok = 0,
    Timeout = 1,
    ChannelBusy = 2,
    QueueFull = 3
};

struct MailboxResult {
    MailboxStatus status{MailboxStatus::Ok};
    uint32_t      channel{0};
    uint32_t      message{0};
};

struct MailboxTxRequest : public AsyncTransactionMeta<MailboxResult> {
    uint32_t channel{0};
    uint32_t message{0};
};

using MailboxCallback = void (*)(uint32_t channel, uint32_t message, void* context);

// @impl [SPEC-HAL-04] docs/DESIGN_SPECIFICATION.md#spec-hal-04
// @status Complete
template <size_t QueueDepth = 16>
class AsyncMailboxDriver : public AsyncDriverBase<MailboxTxRequest, MailboxResult, QueueDepth> {
public:
    virtual void init() = 0;
    virtual bool send_doorbell(uint32_t channel, uint32_t message) = 0;
    virtual void register_receiver(uint32_t channel, MailboxCallback callback, void* context) = 0;
    virtual void enable_channel(uint32_t channel, bool enable) = 0;

    /*
     * C++20 Coroutine Doorbell Awaiter
     */
    struct AsyncDoorbellAwaiter {
        AsyncMailboxDriver& driver;
        MailboxTxRequest    request;
        MailboxResult       result{};

        AsyncDoorbellAwaiter(AsyncMailboxDriver& drv, uint32_t ch, uint32_t msg)
            : driver(drv) {
            request.channel = ch;
            request.message = msg;
        }

        bool await_ready() const noexcept {
            return false;
        }

        void await_suspend(std::coroutine_handle<> handle) noexcept {
            request.coro_handle = handle;
            if (!driver.submit_request(request)) {
                result.status = MailboxStatus::QueueFull;
                if (handle && !handle.done()) {
                    handle.resume();
                }
            }
        }

        MailboxResult await_resume() noexcept {
            driver.pop_completion(result);
            return result;
        }
    };

    AsyncDoorbellAwaiter notify_async(uint32_t channel, uint32_t message) noexcept {
        return AsyncDoorbellAwaiter(*this, channel, message);
    }
};

using IMailbox = AsyncMailboxDriver<16>;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_MAILBOX_HPP
