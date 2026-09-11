/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: Asynchronous DMA/ISR Split-Queue Driver Base
 * -----------------------------------------------------------
 * Standardized split-transaction hardware driver foundation:
 * - Input Queue  : Lock-free SPSC ISR-safe Request queue (Main/Coro -> DMA/ISR)
 * - Output Queue : Lock-free SPSC ISR-safe Completion queue (DMA/ISR -> Main/Coro)
 *
 * Enables zero-allocation async I/O pipelining, DMA hardware chaining,
 * and automatic C++20 coroutine handle resumption via IsrDispatcher.
 */

#ifndef ABSTRACTX_HAL_ASYNC_DRIVER_HPP
#define ABSTRACTX_HAL_ASYNC_DRIVER_HPP

#include <cstddef>
#include <cstdint>
#include <coroutine>
#include <etl/queue_spsc_isr.h>
#include <etl/delegate.h>
#include <etl/memory_model.h>

#include "abstractx/interrupt_lock.hpp"
#include "abstractx/domain_dispatcher.hpp"

namespace abstractx::hal {

/*
 * Generic Request Header with Callback & Coroutine Handle
 */
template <typename TResult>
struct AsyncTransactionMeta {
    using Callback = etl::delegate<void(const TResult&)>;

    Callback               callback{};
    std::coroutine_handle<> coro_handle{nullptr};
    void*                  user_context{nullptr};
    uint64_t               submit_timestamp_us{0};
    uint64_t               start_timestamp_us{0};

    void notify_completion(const TResult& result) const noexcept {
        if (callback.is_valid()) {
            callback(result);
        }
        if (coro_handle && !coro_handle.done()) {
            IsrDispatcher::post(coro_handle);
        }
    }
};

/*
 * Split-Queue Driver Base
 * Template parameters:
 * - TRequest   : Driver-specific transfer request descriptor
 * - TResult    : Driver-specific completion/result descriptor
 * - QueueDepth : Capacity of Request and Completion rings (must be power of 2)
 */
// @impl [SPEC-ARCH-01] [SPEC-HAL-01] docs/DESIGN_SPECIFICATION.md#spec-hal-01
// @status Complete
template <typename TRequest, typename TResult, size_t QueueDepth = 16>
class AsyncDriverBase {
public:
    static constexpr size_t CAPACITY = QueueDepth;

    using RequestQueue = etl::queue_spsc_isr<TRequest, QueueDepth, InterruptLock, etl::memory_model::MEMORY_MODEL_SMALL>;
    using ResultQueue  = etl::queue_spsc_isr<TResult,  QueueDepth, InterruptLock, etl::memory_model::MEMORY_MODEL_SMALL>;

    virtual ~AsyncDriverBase() = default;

    /*
     * Main / Coroutine Domain: Submit a request to the input queue.
     * If the hardware engine is idle, immediately starts hardware transfer.
     */
    bool submit_request(const TRequest& request) noexcept {
        bool pushed = input_queue_.push(request);
        if (pushed) {
            check_and_start_hardware_if_idle();
        }
        return pushed;
    }

    /*
     * Main / Coroutine Domain: Pop a finished result from the output queue.
     */
    bool pop_completion(TResult& result) noexcept {
        return output_queue_.pop(result);
    }

    /*
     * Main Domain Status Queries
     */
    bool has_pending_requests() const noexcept {
        return !input_queue_.empty();
    }

    bool has_completed_results() const noexcept {
        return !output_queue_.empty();
    }

    void reset_queues() noexcept {
        input_queue_.clear();
        output_queue_.clear();
        hardware_busy_ = false;
    }

protected:
    /*
     * DMA / ISR Domain: Pull the next pending request to feed hardware.
     */
    bool pop_next_request_from_isr(TRequest& request) noexcept {
        return input_queue_.pop_from_isr(request);
    }

    /*
     * DMA / ISR Domain: Push a finished transaction result to the output queue
     * and trigger callbacks / coroutine resumes.
     */
    bool push_completion_from_isr(const TRequest& completed_req, const TResult& result) noexcept {
        bool pushed = output_queue_.push_from_isr(result);
        completed_req.notify_completion(result);
        return pushed;
    }

    /*
     * Pure virtual hooks to be implemented by concrete hardware drivers
     */
    virtual void start_hardware_transfer_from_isr(const TRequest& request) noexcept = 0;

    void set_hardware_idle_from_isr() noexcept {
        hardware_busy_ = false;
        // Check if more requests arrived while we were finishing
        TRequest next_req;
        if (pop_next_request_from_isr(next_req)) {
            hardware_busy_ = true;
            start_hardware_transfer_from_isr(next_req);
        }
    }

private:
    void check_and_start_hardware_if_idle() noexcept {
        InterruptGuard guard;
        if (!hardware_busy_) {
            TRequest next_req;
            if (input_queue_.pop_from_isr(next_req)) {
                hardware_busy_ = true;
                start_hardware_transfer_from_isr(next_req);
            }
        }
    }

    RequestQueue input_queue_{};
    ResultQueue  output_queue_{};
    volatile bool hardware_busy_{false};
};

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_ASYNC_DRIVER_HPP
