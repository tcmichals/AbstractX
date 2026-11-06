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
#include <atomic>
#include <coroutine>
#include <etl/queue_spsc_isr.h>
#include <etl/delegate.h>
#include <etl/memory_model.h>

#include "abstractx/interrupt_lock.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/executor.hpp"
#include "io_status.hpp"
#include "abstractx/cold.hpp"

namespace abstractx::hal {

// @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (8: request state, abort else wait)
// Cancellation runs only when a task is destroyed during a transfer: ABSTRACTX_COLD (cold.hpp).

// Request state, owned by the awaiter (in the coroutine frame): Pending -> Done, or
// Pending -> Abandoned -> Done. A completion that finds Abandoned writes and posts nothing.
inline constexpr uint8_t kReqIdle = 0, kReqPending = 1, kReqAbandoned = 2, kReqDone = 3;
using RequestStateWord = std::atomic<uint8_t>;

// A driver that can withdraw a request: removes it if not started, aborts it if it can. True when
// the request is now Done; false when the caller must wait for the driver to finish it.
class ICancellable {
public:
    virtual bool cancel_request(RequestStateWord* state) noexcept = 0;

protected:
    ~ICancellable() = default;
};

// Mark the request Abandoned, ask the driver to withdraw it, and wait (bounded) until it is Done.
// src/executor.cpp. Traps if the driver never finishes it (a hung transfer is a firmware fault).
ABSTRACTX_COLD void abandon_request(ICancellable& driver, RequestStateWord& state) noexcept;

// An awaiter member, declared LAST so it is destroyed first: while it waits, the awaiter's buffers
// (which the hardware may still be writing) are alive. Copies start idle.
struct RequestState {
    RequestStateWord word{kReqIdle};
    ICancellable*    driver{nullptr};

    RequestState() = default;
    RequestState(const RequestState&) noexcept {}
    // Copies never carry the in-flight state: a request is armed in place (no-op, self included).
    RequestState& operator=(const RequestState&) noexcept { return *this; }  // NOLINT(bugprone-unhandled-self-assignment)
    ~RequestState() { if (driver) abandon_request(*driver, word); }

    void arm(ICancellable& d) noexcept {
        driver = &d;
        word.store(kReqPending, std::memory_order_release);
    }
    void disarm() noexcept {
        driver = nullptr;
        word.store(kReqIdle, std::memory_order_release);
    }
};

/*
 * Generic Request Header with Callback & Coroutine Handle
 */
template <typename TResult>
struct AsyncTransactionMeta {
    using Callback = etl::delegate<void(const TResult&)>;

    // [SPEC-ARCH-16] 3: the driver's own hardware work (an I/O processor back end turning a bus
    // completion into a TLP); never application code. Coroutines complete through complete().
    Callback               callback{};
    std::coroutine_handle<> coro_handle{nullptr};
    // Awaiter-owned result storage. When set, the completion is written here
    // instead of the shared output queue, so concurrent awaiters on one driver
    // each receive their own result.
    TResult*               result_slot{nullptr};
    RequestStateWord*      state{nullptr};      // [SPEC-ARCH-16] 8: the awaiter's request state
    void*                  user_context{nullptr};
    uint64_t               submit_timestamp_us{0};
    uint64_t               start_timestamp_us{0};

    bool abandoned() const noexcept {
        return state != nullptr && state->load(std::memory_order_acquire) == kReqAbandoned;
    }

    // @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (8: the one completion path)
    // Write the result, post the coroutine, mark Done: all under one guard, so an abandon cannot
    // interleave. An abandoned request only becomes Done (its frame is going away).
    void complete(const TResult& result) const noexcept {
        if (!abandoned() && callback.is_valid()) {
            callback(result);
        }
        InterruptGuard guard;
        if (abandoned()) {
            state->store(kReqDone, std::memory_order_release);
            return;
        }
        if (result_slot) *result_slot = result;
        if (coro_handle && !coro_handle.done()) {
            (void)abstractx::post_from_isr(coro_handle);  // [SPEC-ARCH-16] completion context
        }
        if (state) state->store(kReqDone, std::memory_order_release);
    }

    void notify_completion(const TResult& result) const noexcept { complete(result); }
};

/*
 * Split-Queue Driver Base
 * Template parameters:
 * - TRequest   : Driver-specific transfer request descriptor
 * - TResult    : Driver-specific completion/result descriptor
 * - QueueDepth : Capacity of Request and Completion rings (must be power of 2)
 */
// @impl [SPEC-ARCH-01] [SPEC-HAL-01] docs/DESIGN_SPECIFICATION.md#spec-hal-01
template <typename TRequest, typename TResult, size_t QueueDepth = 16>
class AsyncDriverBase : public ICancellable {
public:
    static constexpr size_t CAPACITY = QueueDepth;

    using RequestQueue = etl::queue_spsc_isr<TRequest, QueueDepth, InterruptLock, etl::memory_model::MEMORY_MODEL_SMALL>;

    virtual ~AsyncDriverBase() = default;

    // @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (8: withdraw one request)
    ABSTRACTX_COLD bool cancel_request(RequestStateWord* st) noexcept override {
        InterruptGuard guard;
        // Not started: take it out of the queue; nothing was written, nothing is posted.
        bool removed = false;
        const size_t n = input_queue_.size();
        TRequest r;
        for (size_t i = 0; i < n && input_queue_.pop_from_isr(r); ++i) {
            if (!removed && r.state == st) { removed = true; continue; }
            (void)input_queue_.push_from_isr(r);
        }
        if (removed) {
            st->store(kReqDone, std::memory_order_release);
            return true;
        }
        // Running: abort it if the driver can (its completion then finds Abandoned).
        if (hardware_busy_ && active_state_ == st && abort_active_from_isr()) {
            start_next_from_isr();
            return st->load(std::memory_order_acquire) == kReqDone;
        }
        return false;  // the caller waits for the driver to finish it
    }

    /*
     * Main / Coroutine Domain: Submit a request to the input queue.
     * If the hardware engine is idle and the queue is empty, starts the transfer directly
     * without copying the request into and out of input_queue_ ([SPEC-HAL-01]).
     */
    bool submit_request(const TRequest& request) noexcept {
        {
            InterruptGuard guard;
            if (!hardware_busy_ && input_queue_.empty()) {
                hardware_busy_ = true;
                active_state_ = request.state;
                start_hardware_transfer_from_isr(request);
                return true;
            }
        }
        bool pushed = input_queue_.push(request);
        if (pushed) {
            check_and_start_hardware_if_idle();
        }
        return pushed;
    }

    /*
     * Main Domain Status Queries
     */
    bool has_pending_requests() const noexcept {
        return !input_queue_.empty();
    }

    // [SPEC-HAL-07] Complete every queued (not yet started) request with its bus's Cancelled
    // status: result_slot written, coroutine posted to the Dispatcher. Returns how many.
    // @impl [SPEC-HAL-07] docs/DESIGN_SPECIFICATION.md
    size_t cancel_pending() noexcept {
        size_t cancelled = 0;
        for (;;) {
            TRequest req;
            bool got = false;
            {
                InterruptGuard guard;
                got = input_queue_.pop_from_isr(req);
            }
            if (!got) {
                break;
            }
            complete_request(req, make_cancelled_result<TResult>());
            ++cancelled;
        }
        return cancelled;
    }

    // [SPEC-HAL-07] Ask the driver to abort the in-flight request. True when the driver stopped
    // it and completed it with Cancelled; false when nothing was in flight or the driver cannot
    // abort (the request then completes normally).
    bool cancel_active() noexcept {
        InterruptGuard guard;
        if (!hardware_busy_) {
            return false;
        }
        if (abort_active_from_isr()) {
            start_next_from_isr();  // [SPEC-HAL-07] queued requests do not stall (review Q-9)
            return true;
        }
        return false;
    }

    // [SPEC-HAL-07] Cancel queued requests and try to abort the active one.
    // No request is discarded without its coroutine being resumed.
    void reset_queues() noexcept {
        (void)cancel_pending();
        (void)cancel_active();
    }

protected:
    /*
     * DMA / ISR Domain: Pull the next pending request to feed hardware.
     */
    bool pop_next_request_from_isr(TRequest& request) noexcept {
#if ABSTRACTX_LOCKED_ISR_PATHS  // hosted threads or SMP cores: take the lock ([SPEC-ARCH-01])
        return input_queue_.pop(request); // hosted: "ISR" is a worker thread
#else
        return input_queue_.pop_from_isr(request);
#endif
    }

    /*
     * DMA / ISR Domain: Push a finished transaction result to the output queue
     * and trigger callbacks / coroutine resumes.
     */
    // @impl [SPEC-HAL-01] docs/DESIGN_SPECIFICATION.md#spec-hal-01
    // @impl [SPEC-ARCH-16] docs/DESIGN_SPECIFICATION.md (3: one completion path, no shared result queue)
    bool push_completion_from_isr(const TRequest& completed_req, const TResult& result) noexcept {
        completed_req.complete(result);  // result slot, post and Done in one place
        return true;
    }

    /*
     * Pure virtual hooks to be implemented by concrete hardware drivers
     */
    virtual void start_hardware_transfer_from_isr(const TRequest& request) noexcept = 0;

    // [SPEC-HAL-07] Stop the in-flight transfer and complete it with
    // make_cancelled_result<TResult>() through push_completion_from_isr(); return true.
    // Called with interrupts masked. The default cannot abort: the transfer runs to completion.
    virtual bool abort_active_from_isr() noexcept { return false; }

    // Write the result to the request's slot (or the shared queue) and post its coroutine.
    bool complete_request(const TRequest& req, const TResult& result) noexcept {
        return push_completion_from_isr(req, result);
    }

    void set_hardware_idle_from_isr() noexcept {
        InterruptGuard guard;
        start_next_from_isr();
    }

    // Hardware is free: start the next queued request, if any (called with interrupts masked).
    void start_next_from_isr() noexcept {
        hardware_busy_ = false;
        active_state_ = nullptr;
        TRequest next_req;
        if (pop_next_request_from_isr(next_req)) {
            hardware_busy_ = true;
            active_state_ = next_req.state;
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
                active_state_ = next_req.state;
                start_hardware_transfer_from_isr(next_req);
            }
        }
    }

    RequestQueue input_queue_{};
    volatile bool hardware_busy_{false};
    RequestStateWord* active_state_{nullptr};  // shared: under InterruptGuard; the running request
};

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_ASYNC_DRIVER_HPP
