/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX C++20 Coroutine Framework for Flight Controllers & Multi-Rate I/O
 * ---------------------------------------------------------------------------
 * Provides zero-allocation / embedded-friendly C++20 coroutine primitives,
 * TLP split-transaction awaiters, and concurrency combinators (when_all / when_any)
 * for high-rate flight loops (e.g. 8kHz IMU) and slow buses (I2C, SPI, UART).
 */

#ifndef ASP_CORO_HPP
#define ASP_CORO_HPP

// @impl [SPEC-ARCH-02] docs/DESIGN_SPECIFICATION.md#spec-arch-02
// @status Complete

#include "asp_tlp64.hpp"
#include "tlp_ring.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include <coroutine>
#include <cstdint>
#include <cstddef>
#include <array>
#include <tuple>
#include <variant>
#include <optional>
#include <utility>
#include <atomic>

namespace abstractx {
namespace coro {

// ============================================================================
// 1. Static Frame Allocator & Lightweight C++20 Coroutine Task<T>
// ============================================================================

// Freestanding MCU Static Frame Pool (Guarantees 0 B Dynamic Heap Allocation)
#ifndef ABSTRACTX_CORO_POOL_SIZE
#define ABSTRACTX_CORO_POOL_SIZE (60 * 1024)
#endif

// Define ABSTRACTX_CORO_POOL_SECTION to place the pool outside the default BSS
// region, e.g. "-DABSTRACTX_CORO_POOL_SECTION=\".coro_frame_pool\"" to keep
// coroutine frames out of a small tightly-coupled RAM that also holds the stack.
#ifdef ABSTRACTX_CORO_POOL_SECTION
#define ABSTRACTX_CORO_POOL_ATTR __attribute__((section(ABSTRACTX_CORO_POOL_SECTION)))
#else
#define ABSTRACTX_CORO_POOL_ATTR
#endif

alignas(64) ABSTRACTX_CORO_POOL_ATTR inline uint8_t g_coro_static_frame_pool[ABSTRACTX_CORO_POOL_SIZE];
inline std::atomic<size_t> g_coro_static_pool_offset{0};

// @impl [SPEC-ARCH-09] docs/DESIGN_SPECIFICATION.md#spec-arch-09
// An exception escaping a coroutine is a bug: report it, then trap. No
// exception_ptr/rethrow, which would pull the C++ unwinder into firmware.
using UnhandledExceptionHook = void (*)() noexcept;

namespace detail {
inline UnhandledExceptionHook g_unhandled_exception_hook = nullptr;

[[noreturn]] inline void on_unhandled_exception() noexcept {
    if (g_unhandled_exception_hook) g_unhandled_exception_hook();
    __builtin_trap();
}

// Completion callback for when_all/when_any: function pointer + context
// instead of std::function (no type erasure, no heap, no RTTI).
struct CompletionHook {
    void (*fn)(void*) noexcept = nullptr;
    void* ctx = nullptr;
    void operator()() const noexcept { if (fn) fn(ctx); }
};
} // namespace detail

inline void set_unhandled_exception_hook(UnhandledExceptionHook hook) noexcept {
    detail::g_unhandled_exception_hook = hook;
}

// Awaiting an empty Task (frame pool exhausted) reports here, then traps.
namespace detail {
inline UnhandledExceptionHook g_pool_exhausted_hook = nullptr;

[[noreturn]] inline void on_pool_exhausted() noexcept {
    if (g_pool_exhausted_hook) g_pool_exhausted_hook();
    __builtin_trap();
}

template <typename Handle>
inline void require_frame(Handle h) noexcept {
    if (!h) on_pool_exhausted();
}
} // namespace detail

// @impl [SPEC-ARCH-09] docs/DESIGN_SPECIFICATION.md#spec-arch-09
inline void set_pool_exhausted_hook(UnhandledExceptionHook hook) noexcept {
    detail::g_pool_exhausted_hook = hook;
}

namespace detail {

// Freed frames go on an intrusive free list (the link lives inside the dead
// frame, so it costs no extra RAM) and are reused for the next frame of the
// exact same aligned size. Call sites have a small, fixed set of frame sizes, so
// a steady-state loop that repeatedly awaits sub-tasks stops growing the bump
// offset after its first iteration instead of exhausting the pool.
// @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md#spec-arch-01
struct FreeFrame {
    FreeFrame* next;
    size_t     size;
};

inline FreeFrame* g_coro_free_list{nullptr};
inline std::atomic<size_t> g_coro_live_bytes{0};
// Guards g_coro_free_list across cores. Frames must not be created or destroyed
// from ISR context (ISRs only post handles), so the spinlock cannot self-deadlock.
inline std::atomic_flag g_coro_free_list_lock = ATOMIC_FLAG_INIT;

// Promise operator new receives no alignment, and frames hold over-aligned
// locals (Tlp64 is alignas(64)), so every frame is 64-byte aligned: the pool
// is alignas(64) and every frame size is a multiple of 64.
inline constexpr size_t kCoroFrameAlign = 64;

inline size_t coro_frame_size(size_t sz) noexcept {
    const size_t aligned_sz = (sz + kCoroFrameAlign - 1U) & ~(kCoroFrameAlign - 1U);
    return aligned_sz < sizeof(FreeFrame) ? sizeof(FreeFrame) : aligned_sz;
}

struct FreeListGuard {
    FreeListGuard() noexcept {
        while (g_coro_free_list_lock.test_and_set(std::memory_order_acquire)) {}
    }
    ~FreeListGuard() { g_coro_free_list_lock.clear(std::memory_order_release); }
};

// Returns nullptr when exhausted so the promise's
// get_return_object_on_allocation_failure() path runs; it must never hand back
// storage that already belongs to a live coroutine.
inline void* coro_frame_alloc(size_t sz) noexcept {
    const size_t aligned_sz = coro_frame_size(sz);
    {
        FreeListGuard guard;
        for (FreeFrame** link = &g_coro_free_list; *link; link = &(*link)->next) {
            if ((*link)->size == aligned_sz) {
                FreeFrame* frame = *link;
                *link = frame->next;
                g_coro_live_bytes.fetch_add(aligned_sz, std::memory_order_relaxed);
                return frame;
            }
        }
    }
    size_t offset = g_coro_static_pool_offset.load(std::memory_order_relaxed);
    for (;;) {
        if (aligned_sz > sizeof(g_coro_static_frame_pool) - offset) {
            return nullptr;
        }
        if (g_coro_static_pool_offset.compare_exchange_weak(
                offset, offset + aligned_sz,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            g_coro_live_bytes.fetch_add(aligned_sz, std::memory_order_relaxed);
            return &g_coro_static_frame_pool[offset];
        }
    }
}

inline void coro_frame_free(void* ptr, size_t sz) noexcept {
    if (!ptr) {
        return;
    }
    const size_t aligned_sz = coro_frame_size(sz);
    g_coro_live_bytes.fetch_sub(aligned_sz, std::memory_order_relaxed);
    FreeListGuard guard;
    auto* frame = static_cast<FreeFrame*>(ptr);
    frame->size = aligned_sz;
    frame->next = g_coro_free_list;
    g_coro_free_list = frame;
}

} // namespace detail

// Bytes held by currently live coroutine frames.
inline size_t coro_pool_used() noexcept {
    return detail::g_coro_live_bytes.load(std::memory_order_relaxed);
}

// Bytes ever carved from the pool by the bump allocator (high-water mark).
inline size_t coro_pool_high_water() noexcept {
    return g_coro_static_pool_offset.load(std::memory_order_relaxed);
}

constexpr size_t coro_pool_capacity() noexcept {
    return sizeof(g_coro_static_frame_pool);
}

template <typename T = void>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T> result_{};
        std::coroutine_handle<> continuation_{nullptr};
        detail::CompletionHook on_complete_{};
        bool detached_{false};   // [SPEC-ARCH-11] owner let go: free the frame at final suspend
        bool started_{false};    // [SPEC-ARCH-11] ran past initial_suspend at least once

        Task get_return_object() noexcept {
            return Task{handle_type::from_promise(*this)};
        }

        static Task get_return_object_on_allocation_failure() noexcept {
            return Task{nullptr};
        }

        struct InitialAwaiter {
            promise_type& p;
            bool await_ready() const noexcept { return false; }
            // waker: the Task object owns the handle (lazy start)
            void await_suspend(std::coroutine_handle<>) const noexcept {}
            void await_resume() const noexcept { p.started_ = true; }
        };
        InitialAwaiter initial_suspend() noexcept { return {*this}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(handle_type h) noexcept {
                if (h.promise().detached_) {   // [SPEC-ARCH-11] nobody owns or awaits it any more
                    h.destroy();
                    return std::noop_coroutine();
                }
                h.promise().on_complete_();
                auto continuation = h.promise().continuation_;
                if (continuation) {
                    return continuation;
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_value(T value) noexcept {
            result_ = std::move(value);
        }

        void unhandled_exception() noexcept {
            detail::on_unhandled_exception();
        }

        static void* operator new(size_t sz) noexcept {
            return detail::coro_frame_alloc(sz);
        }

        static void operator delete(void* ptr, size_t sz) noexcept {
            detail::coro_frame_free(ptr, sz);
        }
    };

    explicit Task(handle_type handle) noexcept : handle_(handle) {}

    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
        if (handle_) handle_.destroy();
    }

    // [SPEC-ARCH-11] Give up ownership of a task that has not finished: it runs on to its end
    // and frees its own frame; nobody is resumed and its result is discarded. A finished task
    // is destroyed now.
    // @impl [SPEC-ARCH-11] docs/DESIGN_SPECIFICATION.md
    void detach() noexcept {
        if (!handle_) return;
        if (handle_.done() || !handle_.promise().started_) {   // nobody will ever resume it
            handle_.destroy();
        } else {
            handle_.promise().detached_ = true;
            handle_.promise().on_complete_ = {};
            handle_.promise().continuation_ = nullptr;
        }
        handle_ = nullptr;
    }

    bool await_ready() const noexcept {
        return !handle_ || handle_.done();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        return handle_;
    }

    T await_resume() noexcept {
        detail::require_frame(handle_);
        return std::move(*handle_.promise().result_);
    }

    const T& get_result() const { detail::require_frame(handle_); return *handle_.promise().result_; }
    T& get_result() { detail::require_frame(handle_); return *handle_.promise().result_; }

    handle_type handle() const noexcept { return handle_; }

    bool resume() {
        if (handle_ && !handle_.done()) {
            // resume: Task<T>::resume(): the owner starts the task (examples, spawn)
            handle_.resume();
            return !handle_.done();
        }
        return false;
    }

    bool done() const noexcept {
        return !handle_ || handle_.done();
    }

    bool is_ready() const noexcept {
        return done();
    }

    auto operator co_await() const noexcept {
        struct Awaiter {
            handle_type handle_;
            bool await_ready() const noexcept { return !handle_ || handle_.done(); }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
                handle_.promise().continuation_ = caller;
                return handle_;
            }
            T await_resume() const {
                detail::require_frame(handle_);
                return *handle_.promise().result_;
            }
        };
        return Awaiter{handle_};
    }

private:
    handle_type handle_{nullptr};
};

// Specialization for void
template <>
class Task<void> {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::coroutine_handle<> continuation_{nullptr};
        detail::CompletionHook on_complete_{};
        bool detached_{false};   // [SPEC-ARCH-11] owner let go: free the frame at final suspend
        bool started_{false};    // [SPEC-ARCH-11] ran past initial_suspend at least once

        Task get_return_object() noexcept {
            return Task{handle_type::from_promise(*this)};
        }

        static Task get_return_object_on_allocation_failure() noexcept {
            return Task{nullptr};
        }

        struct InitialAwaiter {
            promise_type& p;
            bool await_ready() const noexcept { return false; }
            // waker: the Task object owns the handle (lazy start)
            void await_suspend(std::coroutine_handle<>) const noexcept {}
            void await_resume() const noexcept { p.started_ = true; }
        };
        InitialAwaiter initial_suspend() noexcept { return {*this}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(handle_type h) noexcept {
                if (h.promise().detached_) {   // [SPEC-ARCH-11] nobody owns or awaits it any more
                    h.destroy();
                    return std::noop_coroutine();
                }
                h.promise().on_complete_();
                auto continuation = h.promise().continuation_;
                if (continuation) {
                    return continuation;
                }
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_void() noexcept {}

        void unhandled_exception() noexcept {
            detail::on_unhandled_exception();
        }

        static void* operator new(size_t sz) noexcept {
            return detail::coro_frame_alloc(sz);
        }

        static void operator delete(void* ptr, size_t sz) noexcept {
            detail::coro_frame_free(ptr, sz);
        }
    };

    explicit Task(handle_type handle) noexcept : handle_(handle) {}

    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    ~Task() {
        if (handle_) handle_.destroy();
    }

    // [SPEC-ARCH-11] Give up ownership of a task that has not finished: it runs on to its end
    // and frees its own frame; nobody is resumed and its result is discarded. A finished task
    // is destroyed now.
    // @impl [SPEC-ARCH-11] docs/DESIGN_SPECIFICATION.md
    void detach() noexcept {
        if (!handle_) return;
        if (handle_.done() || !handle_.promise().started_) {   // nobody will ever resume it
            handle_.destroy();
        } else {
            handle_.promise().detached_ = true;
            handle_.promise().on_complete_ = {};
            handle_.promise().continuation_ = nullptr;
        }
        handle_ = nullptr;
    }

    bool await_ready() const noexcept {
        return !handle_ || handle_.done();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        return handle_;
    }

    void await_resume() noexcept { detail::require_frame(handle_); }

    handle_type handle() const noexcept { return handle_; }

    bool resume() {
        if (handle_ && !handle_.done()) {
            // resume: Task<void>::resume(): the owner starts the task (examples, spawn)
            handle_.resume();
            return !handle_.done();
        }
        return false;
    }

    bool done() const noexcept {
        return !handle_ || handle_.done();
    }

    bool is_ready() const noexcept {
        return done();
    }

    auto operator co_await() const noexcept {
        struct Awaiter {
            handle_type handle_;
            bool await_ready() const noexcept { return !handle_ || handle_.done(); }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
                handle_.promise().continuation_ = caller;
                return handle_;
            }
            void await_resume() const noexcept { detail::require_frame(handle_); }
        };
        return Awaiter{handle_};
    }

private:
    handle_type handle_{nullptr};
};

// Standard Asynchronous Yield Awaiter (Yields to let other coroutines run)
struct YieldAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        abstractx::Dispatcher::push(h);
    }
    void await_resume() noexcept {}
};

// Timed waits use the target timer driver: co_await hal::get_timer_driver().sleep_ms_async(ms)
// ([SPEC-ARCH-10] every suspension has a waker). Hand-clocked delay / wait_until helpers for the
// protothread examples live in examples/hand_clocked.hpp.

// ============================================================================
// 2. Generic Protothread Replacement Primitives (Zero-Heap Embedded Primitives)
// ============================================================================

// Standard Cooperative Yield (replaces PT_YIELD)
inline YieldAwaiter yield() noexcept { return {}; }

// Cooperative Event Signaling (replaces global flag synchronization)
// @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
class Event {
public:
    constexpr Event(bool initial_state = false) noexcept : state_(initial_state) {}

    // @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
    void set() noexcept {
        state_ = true;
        while (waiters_) {
            auto* next = waiters_->next_;
            auto h = waiters_->handle_;
            waiters_->handle_ = nullptr;
            waiters_->next_ = nullptr;
            waiters_ = next;
            if (h) {
                abstractx::Dispatcher::push(h);  // never inline: only step() resumes
            }
        }
    }

    void reset() noexcept { state_ = false; }
    bool is_set() const noexcept { return state_; }

    struct Awaiter {
        Event& event_;
        std::coroutine_handle<> handle_{nullptr};
        Awaiter* next_{nullptr};

        bool await_ready() const noexcept { return event_.state_; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            handle_ = h;
            next_ = event_.waiters_;
            event_.waiters_ = this;
        }
        void await_resume() noexcept {}

        ~Awaiter() {
            if (handle_) {
                Awaiter** curr = &event_.waiters_;
                while (*curr) {
                    if (*curr == this) {
                        *curr = next_;
                        break;
                    }
                    curr = &(*curr)->next_;
                }
            }
        }
    };

    Awaiter operator co_await() noexcept { return Awaiter{*this}; }

private:
    bool state_{false};
    Awaiter* waiters_{nullptr};
};

// Cooperative Counting Semaphore (replaces pt-sem.h PT_SEM_WAIT / PT_SEM_SIGNAL)
// @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
class Semaphore {
public:
    explicit constexpr Semaphore(size_t initial_count = 0) noexcept : count_(initial_count) {}

    // @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
    void release() noexcept {
        if (waiters_head_) {
            auto* w = waiters_head_;
            waiters_head_ = w->next_;
            if (!waiters_head_) {
                waiters_tail_ = nullptr;
            }
            auto h = w->handle_;
            w->handle_ = nullptr;
            w->next_ = nullptr;
            w->granted_ = true;              // [SPEC-ARCH-10] this permit is the waiter's
            abstractx::Dispatcher::push(h);
        } else {
            ++count_;
        }
    }

    struct AcquireAwaiter {
        Semaphore& sem_;
        std::coroutine_handle<> handle_{nullptr};
        AcquireAwaiter* next_{nullptr};
        bool granted_{false};

        bool await_ready() const noexcept { return sem_.count_ > 0; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            handle_ = h;
            next_ = nullptr;
            if (sem_.waiters_tail_) {
                sem_.waiters_tail_->next_ = this;
            } else {
                sem_.waiters_head_ = this;
            }
            sem_.waiters_tail_ = this;
        }
        // @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
        void await_resume() noexcept {
            if (!granted_ && sem_.count_ > 0) --sem_.count_;   // hand-off already owns its permit
        }

        ~AcquireAwaiter() {
            if (handle_) {
                AcquireAwaiter** curr = &sem_.waiters_head_;
                AcquireAwaiter* prev = nullptr;
                while (*curr) {
                    if (*curr == this) {
                        *curr = next_;
                        if (sem_.waiters_tail_ == this) {
                            sem_.waiters_tail_ = prev;
                        }
                        break;
                    }
                    prev = *curr;
                    curr = &(*curr)->next_;
                }
            }
        }
    };

    AcquireAwaiter acquire() noexcept { return AcquireAwaiter{*this}; }
    AcquireAwaiter operator co_await() noexcept { return acquire(); }

    size_t count() const noexcept { return count_; }

private:
    size_t count_{0};
    AcquireAwaiter* waiters_head_{nullptr};
    AcquireAwaiter* waiters_tail_{nullptr};
};

// Cooperative Lock-Free SPSC Queue (replaces raw circular buffers)
// @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
template <typename T, size_t Capacity = 16>
class AsyncQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
    static constexpr size_t kMask = Capacity - 1;

public:
    struct PushAwaiter {
        AsyncQueue& q_;
        T item_;
        bool await_ready() const noexcept { return !q_.is_full(); }
        void await_suspend(std::coroutine_handle<> h) noexcept { q_.push_waiter_ = h; }
        void await_resume() noexcept { q_.try_push(std::move(item_)); }
    };

    struct PopAwaiter {
        AsyncQueue& q_;
        bool await_ready() const noexcept { return !q_.is_empty(); }
        void await_suspend(std::coroutine_handle<> h) noexcept { q_.pop_waiter_ = h; }
        T await_resume() noexcept { return q_.try_pop(); }
    };

    PushAwaiter push(T val) noexcept { return PushAwaiter{*this, std::move(val)}; }
    PopAwaiter pop() noexcept { return PopAwaiter{*this}; }

    bool is_empty() const noexcept { return head_ == tail_; }
    bool is_full() const noexcept { return (head_ - tail_) >= Capacity; }
    bool empty() const noexcept { return is_empty(); }
    bool full() const noexcept { return is_full(); }
    size_t size() const noexcept { return head_ - tail_; }

    bool try_push(T val) noexcept {
        if (is_full()) return false;
        buffer_[head_ & kMask] = std::move(val);
        ++head_;
        if (pop_waiter_) {
            auto h = pop_waiter_;
            pop_waiter_ = nullptr;
            abstractx::Dispatcher::push(h);  // [SPEC-ARCH-10]: never inline
        }
        return true;
    }

    bool try_pop(T& out) noexcept {
        if (is_empty()) return false;
        out = std::move(buffer_[tail_ & kMask]);
        ++tail_;
        if (push_waiter_) {
            auto h = push_waiter_;
            push_waiter_ = nullptr;
            abstractx::Dispatcher::push(h);  // [SPEC-ARCH-10]: never inline
        }
        return true;
    }

    T try_pop() noexcept {
        if (is_empty()) return T{};
        T item = std::move(buffer_[tail_ & kMask]);
        ++tail_;
        if (push_waiter_) {
            auto h = push_waiter_;
            push_waiter_ = nullptr;
            abstractx::Dispatcher::push(h);  // [SPEC-ARCH-10]: never inline
        }
        return item;
    }

    bool try_peek(T& out) const noexcept {
        if (is_empty()) return false;
        out = buffer_[tail_ & kMask];
        return true;
    }

private:
    std::array<T, Capacity> buffer_{};
    size_t head_{0};
    size_t tail_{0};
    std::coroutine_handle<> push_waiter_{nullptr};
    std::coroutine_handle<> pop_waiter_{nullptr};
};


// ============================================================================
// 3. Coroutine I/O Engine & TLP Split-Transaction Dispatcher
// ============================================================================

// @impl [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md (completions are posted, never resumed inline)
class CoroutineIoEngine {
public:
    static constexpr size_t MAX_PENDING_TAGS = 256;

    struct PendingRequest {
        bool active{false};
        uint32_t target_addr{0};
        std::coroutine_handle<> awaiting_coro{nullptr};
        Tlp64 response_tlp{};
    };

    CoroutineIoEngine(SpscTlpRing<>& host_tx_ring, SpscTlpRing<>& host_rx_ring)
        : tx_ring_(host_tx_ring), rx_ring_(host_rx_ring) {}

    // Allocate a unique Tag for split-transaction matching
    uint8_t allocate_tag() {
        for (size_t i = 1; i < MAX_PENDING_TAGS; ++i) {
            uint8_t tag = static_cast<uint8_t>((next_tag_ + i) % 254 + 1);
            if (!pending_requests_[tag].active) {
                next_tag_ = tag;
                pending_requests_[tag].active = true;
                return tag;
            }
        }
        return 0; // Out of tags
    }

    void register_awaiter(uint8_t tag, uint32_t addr, std::coroutine_handle<> coro) {
        pending_requests_[tag].target_addr = addr;
        pending_requests_[tag].awaiting_coro = coro;
    }

    // Register an awaiter for autonomous telemetry stream (Channel 2 IMU stream)
    void register_stream_awaiter(uint8_t channel, std::coroutine_handle<> coro) {
        if (channel < 8) {
            stream_awaiters_[channel] = coro;
        }
    }

    // Poll the incoming SPSC RX Ring for completed TLPs and wake pending coroutines
    size_t poll_completions() {
        size_t completions_processed = 0;

        while (auto opt_tlp = rx_ring_.pop()) {
            Tlp64 tlp = *opt_tlp;
            completions_processed++;

            // Check if this is an autonomous DMA Stream TLP (e.g. IMU Telemetry)
            if (tlp.type() == TlpType::DmaStream) {
                uint8_t ch = static_cast<uint8_t>(tlp.channel());
                if (ch < 8 && stream_awaiters_[ch]) {
                    latest_stream_tlp_[ch] = tlp;
                    auto coro = stream_awaiters_[ch];
                    stream_awaiters_[ch] = nullptr;
                    (void)abstractx::Dispatcher::push(coro);  // [SPEC-ARCH-10]: never resume inline
                }
                continue;
            }

            // Otherwise, match based on PCIe Tag for split transaction (MemRd / MemWr completion)
            uint8_t tag = tlp.wire.tag;
            if (pending_requests_[tag].active && pending_requests_[tag].awaiting_coro) {
                pending_requests_[tag].response_tlp = tlp;
                pending_requests_[tag].active = false;
                auto coro = pending_requests_[tag].awaiting_coro;
                pending_requests_[tag].awaiting_coro = nullptr;
                (void)abstractx::Dispatcher::push(coro);  // [SPEC-ARCH-10]: never resume inline
            }
        }

        return completions_processed;
    }

    // Awaitable for Split-Transaction Register Read (MemRd)
    struct MemReadAwaiter {
        CoroutineIoEngine& engine;
        uint32_t address;
        uint8_t tag{0};
        bool sent{false};

        MemReadAwaiter(CoroutineIoEngine& eng, uint32_t addr) : engine(eng), address(addr) {}

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> coro) {
            tag = engine.allocate_tag();
            if (tag == 0) return false; // Tag allocation failure, don't suspend

            engine.register_awaiter(tag, address, coro);

            // Construct and enqueue MemRd 64-byte TLP into TX ring
            Tlp64 req = Tlp64::make_mem_read(address, tag);
            if (!engine.tx_ring_.push(req)) {
                engine.pending_requests_[tag].active = false;
                return false;
            }
            return true;
        }

        Tlp64 await_resume() noexcept {
            return engine.pending_requests_[tag].response_tlp;
        }
    };

    // Awaitable for Split-Transaction Register Write (MemWr)
    struct MemWriteAwaiter {
        CoroutineIoEngine& engine;
        uint32_t address;
        uint32_t value;
        uint8_t tag{0};

        MemWriteAwaiter(CoroutineIoEngine& eng, uint32_t addr, uint32_t val)
            : engine(eng), address(addr), value(val) {}

        bool await_ready() const noexcept { return false; }

        bool await_suspend(std::coroutine_handle<> coro) {
            tag = engine.allocate_tag();
            if (tag == 0) return false;

            engine.register_awaiter(tag, address, coro);
            Tlp64 req = Tlp64::make_mem_write(address, value, tag);
            if (!engine.tx_ring_.push(req)) {
                engine.pending_requests_[tag].active = false;
                return false;
            }
            return true;
        }

        Tlp64 await_resume() noexcept {
            return engine.pending_requests_[tag].response_tlp;
        }
    };

    // Awaitable for Next DMA Telemetry Stream Packet
    struct StreamAwaiter {
        CoroutineIoEngine& engine;
        Channel channel;

        StreamAwaiter(CoroutineIoEngine& eng, Channel ch) : engine(eng), channel(ch) {}

        bool await_ready() const noexcept { return false; }

        bool refused{false};

        // One waiter per channel: a second one is refused (empty TLP) instead of overwriting the first.
        bool await_suspend(std::coroutine_handle<> coro) {
            const auto ch = static_cast<uint8_t>(channel);
            if (ch >= 8 || engine.stream_awaiters_[ch]) {
                refused = true;
                return false;
            }
            engine.register_stream_awaiter(ch, coro);
            return true;
        }

        Tlp64 await_resume() noexcept {
            if (refused) return Tlp64{};
            return engine.latest_stream_tlp_[static_cast<uint8_t>(channel)];
        }
    };

    // Helper functions returning awaitables
    MemReadAwaiter async_read(uint32_t address) {
        return MemReadAwaiter{*this, address};
    }

    MemWriteAwaiter async_write(uint32_t address, uint32_t value) {
        return MemWriteAwaiter{*this, address, value};
    }

    StreamAwaiter async_await_stream(Channel channel = Channel::Telemetry) {
        return StreamAwaiter{*this, channel};
    }

private:
    SpscTlpRing<>& tx_ring_;
    SpscTlpRing<>& rx_ring_;
    uint8_t next_tag_{1};
    std::array<PendingRequest, MAX_PENDING_TAGS> pending_requests_{};
    std::array<std::coroutine_handle<>, 8> stream_awaiters_{};
    std::array<Tlp64, 8> latest_stream_tlp_{};
};

/// ============================================================================
// 3. Concurrency Combinators: when_all (&&) and when_any (||)
// ============================================================================
//
// Design rules (enforced by this implementation):
//
//  1. NO .resume() calls inside await_suspend(). Symmetric transfer is used
//     (returning a coroutine_handle<>) to avoid unbounded stack growth.
//
//  2. Sub-task completion is signalled via an on_complete_ callback stored in
//     the promise_type. The callback is set by the awaiter BEFORE the task is
//     resumed, so final_suspend can call it synchronously or asynchronously.
//
//  3. The caller is resumed exactly ONCE (guarded by ready_index_ / remaining_
//     atomic counters). Double-resume is a hard UB — the guards prevent it.
//
// Requires: Task<T>::promise_type has a field:
//     detail::CompletionHook on_complete_{};  (fn + ctx, [SPEC-ARCH-09])
// and FinalAwaiter calls on_complete_() before returning noop_coroutine().
// ============================================================================

// --- when_all (&&) Implementation ---
template <typename... Tasks>
class WhenAllAwaiter {
public:
    explicit WhenAllAwaiter(Tasks&&... tasks)
        : tasks_(std::forward<Tasks>(tasks)...), remaining_(sizeof...(Tasks) + 1) {}

    bool await_ready() const noexcept {
        return sizeof...(Tasks) == 0;
    }

    bool await_suspend(std::coroutine_handle<> caller) noexcept {
        caller_ = caller;
        start_all(std::index_sequence_for<Tasks...>{});
        return remaining_.fetch_sub(1, std::memory_order_acq_rel) > 1;
    }

    auto await_resume() {
        return get_results(std::index_sequence_for<Tasks...>{});
    }

private:
    template <size_t... Is>
    void start_all(std::index_sequence<Is...>) {
        (inject_callback<Is>(), ...);
        (start_one<Is>(), ...);
    }

    template <size_t Index>
    void inject_callback() {
        detail::require_frame(std::get<Index>(tasks_).handle());   // [SPEC-ARCH-09]
        std::get<Index>(tasks_).handle().promise().on_complete_ = {
            [](void* self) noexcept { static_cast<WhenAllAwaiter*>(self)->notify_done(); }, this};
    }

    template <size_t Index>
    void start_one() {
        auto& task = std::get<Index>(tasks_);
        // resume: when_all starts a child ([SPEC-ARCH-11])
        task.resume();
    }

    void notify_done() noexcept {
        if (remaining_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            auto caller = caller_;
            caller_ = nullptr;
            if (caller) {
                abstractx::Dispatcher::push(caller);
            }
        }
    }

    template <size_t... Is>
    auto get_results(std::index_sequence<Is...>) {
        return std::make_tuple(std::get<Is>(tasks_).await_resume()...);
    }

    std::tuple<Tasks...> tasks_;
    std::atomic<size_t> remaining_;
    std::coroutine_handle<> caller_{nullptr};
};

template <typename... Tasks>
auto when_all(Tasks&&... tasks) {
    return WhenAllAwaiter<std::decay_t<Tasks>...>(std::forward<Tasks>(tasks)...);
}

// --- when_any (||) / Race Combinator ---
template <typename T1, typename T2>
class WhenAnyAwaiter {
public:
    WhenAnyAwaiter(Task<T1>&& t1, Task<T2>&& t2)
        : t1_(std::move(t1)), t2_(std::move(t2)), ready_index_(-1), stage_(0) {}

    bool await_ready() const noexcept {
        return t1_.done() || t2_.done();
    }

    bool await_suspend(std::coroutine_handle<> caller) noexcept {
        caller_ = caller;
        detail::require_frame(t1_.handle());   // [SPEC-ARCH-09]
        detail::require_frame(t2_.handle());

        // Register callbacks BEFORE starting either task.
        t1_.handle().promise().on_complete_ = {
            [](void* self) noexcept { static_cast<WhenAnyAwaiter*>(self)->notify_winner(0); }, this};
        t2_.handle().promise().on_complete_ = {
            [](void* self) noexcept { static_cast<WhenAnyAwaiter*>(self)->notify_winner(1); }, this};

        // Start t1.
        // resume: when_any starts its first child ([SPEC-ARCH-11])
        t1_.resume();
        if (ready_index_.load(std::memory_order_acquire) != -1) {
            return false;
        }

        // Start t2.
        // resume: when_any starts its second child ([SPEC-ARCH-11])
        t2_.resume();

        int prev = stage_.exchange(1, std::memory_order_acq_rel);
        if (prev == 2) {
            return false;
        }
        if (ready_index_.load(std::memory_order_acquire) != -1) {
            return false;
        }
        return true;
    }

    // @impl [SPEC-ARCH-11] docs/DESIGN_SPECIFICATION.md
    std::variant<T1, T2> await_resume() {
        int idx = ready_index_.load(std::memory_order_acquire);
        if (idx == 0 || (idx == -1 && t1_.done())) {
            std::variant<T1, T2> r{std::in_place_index<0>, t1_.await_resume()};
            t2_.detach();   // the loser runs on and frees itself (freed now if it never started)
            return r;
        }
        std::variant<T1, T2> r{std::in_place_index<1>, t2_.await_resume()};
        t1_.detach();
        return r;
    }

private:
    void notify_winner(int idx) noexcept {
        int expected = -1;
        if (ready_index_.compare_exchange_strong(expected, idx, std::memory_order_acq_rel)) {
            int prev = stage_.exchange(2, std::memory_order_acq_rel);
            if (prev == 1) {
                auto caller = caller_;
                caller_ = nullptr;
                if (caller) {
                    abstractx::Dispatcher::push(caller);
                }
            }
        }
    }

    Task<T1> t1_;
    Task<T2> t2_;
    std::atomic<int> ready_index_{-1};
    std::atomic<int> stage_{0};
    std::coroutine_handle<> caller_{nullptr};
};

template <typename T1, typename T2>
auto when_any(Task<T1>&& t1, Task<T2>&& t2) {
    return WhenAnyAwaiter<T1, T2>(std::move(t1), std::move(t2));
}

} // namespace coro
} // namespace abstractx

#endif // ASP_CORO_HPP
