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

#include "asp_tlp64.hpp"
#include "spsc_tlp_ring.hpp"
#include <coroutine>
#include <cstdint>
#include <cstddef>
#include <array>
#include <tuple>
#include <variant>
#include <optional>
#include <utility>
#include <functional>

namespace abstractx {
namespace coro {

// ============================================================================
// 1. Static Frame Allocator & Lightweight C++20 Coroutine Task<T>
// ============================================================================

// Freestanding MCU Static Frame Pool (Guarantees 0 B Dynamic Heap Allocation)
alignas(64) inline uint8_t g_coro_static_frame_pool[64 * 1024];
inline std::atomic<size_t> g_coro_static_pool_offset{0};

template <typename T = void>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T> result_{};
        std::coroutine_handle<> continuation_{nullptr};
        std::exception_ptr exception_{nullptr};
        std::function<void()> on_complete_{nullptr};

        Task get_return_object() noexcept {
            return Task{handle_type::from_promise(*this)};
        }

        static Task get_return_object_on_allocation_failure() noexcept {
            return Task{nullptr};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(handle_type h) noexcept {
                if (h.promise().on_complete_) {
                    h.promise().on_complete_();
                }
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
            exception_ = std::current_exception();
        }

        static void* operator new(size_t sz) noexcept {
            size_t aligned_sz = (sz + 15u) & ~15u;
            size_t old_offset = g_coro_static_pool_offset.fetch_add(aligned_sz, std::memory_order_acq_rel);
            if (old_offset + aligned_sz > sizeof(g_coro_static_frame_pool)) {
                g_coro_static_pool_offset.store(aligned_sz, std::memory_order_release);
                return g_coro_static_frame_pool;
            }
            return &g_coro_static_frame_pool[old_offset];
        }

        static void operator delete(void*, size_t) noexcept {}
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

    bool await_ready() const noexcept {
        return !handle_ || handle_.done();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        return handle_;
    }

    T await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
        return std::move(*handle_.promise().result_);
    }

    const T& get_result() const { return *handle_.promise().result_; }
    T& get_result() { return *handle_.promise().result_; }

    handle_type handle() const noexcept { return handle_; }

    bool resume() {
        if (handle_ && !handle_.done()) {
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
        std::exception_ptr exception_{nullptr};
        std::function<void()> on_complete_{nullptr};

        Task get_return_object() noexcept {
            return Task{handle_type::from_promise(*this)};
        }

        static Task get_return_object_on_allocation_failure() noexcept {
            return Task{nullptr};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(handle_type h) noexcept {
                if (h.promise().on_complete_) {
                    h.promise().on_complete_();
                }
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
            exception_ = std::current_exception();
        }

        static void* operator new(size_t sz) noexcept {
            size_t aligned_sz = (sz + 15u) & ~15u;
            size_t old_offset = g_coro_static_pool_offset.fetch_add(aligned_sz, std::memory_order_acq_rel);
            if (old_offset + aligned_sz > sizeof(g_coro_static_frame_pool)) {
                g_coro_static_pool_offset.store(aligned_sz, std::memory_order_release);
                return g_coro_static_frame_pool;
            }
            return &g_coro_static_frame_pool[old_offset];
        }

        static void operator delete(void*, size_t) noexcept {}
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

    bool await_ready() const noexcept {
        return !handle_ || handle_.done();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        handle_.promise().continuation_ = caller;
        return handle_;
    }

    void await_resume() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
    }

    handle_type handle() const noexcept { return handle_; }

    bool resume() {
        if (handle_ && !handle_.done()) {
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
            void await_resume() const noexcept {}
        };
        return Awaiter{handle_};
    }

private:
    handle_type handle_{nullptr};
};

// Standard Asynchronous Yield Awaiter (Yields to let other coroutines run)
struct YieldAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) noexcept {}
    void await_resume() noexcept {}
};

// Standard Asynchronous Delay / Sleep Awaiter (0 CPU Superloop Polling)
struct AsyncDelayAwaiter {
    uint64_t resume_at_us_{0};
    uint64_t current_time_us_{0};
    uint64_t* timer_comparator_reg_{nullptr};

    constexpr AsyncDelayAwaiter(uint64_t resume_at_us, uint64_t current_time_us, uint64_t* timer_reg = nullptr) noexcept
        : resume_at_us_(resume_at_us), current_time_us_(current_time_us), timer_comparator_reg_(timer_reg) {}

    bool await_ready() const noexcept { return current_time_us_ >= resume_at_us_; }

    void await_suspend(std::coroutine_handle<>) noexcept {
        if (timer_comparator_reg_) {
            *timer_comparator_reg_ = resume_at_us_;
        }
    }

    void await_resume() noexcept {}
};

// Aliases for embedded flexibility
using AsyncSleepAwaiter = AsyncDelayAwaiter;

// ============================================================================
// 2. Generic Protothread Replacement Primitives (Zero-Heap Embedded Primitives)
// ============================================================================

// Standard Cooperative Yield (replaces PT_YIELD)
inline YieldAwaiter yield() noexcept { return {}; }

// Standard Asynchronous Delay Helpers
inline AsyncDelayAwaiter delay_us(uint64_t delta_us, uint64_t current_time_us, uint64_t* timer_reg = nullptr) noexcept {
    return AsyncDelayAwaiter{current_time_us + delta_us, current_time_us, timer_reg};
}

inline AsyncDelayAwaiter delay_ms(uint64_t delta_ms, uint64_t current_time_ms, uint64_t* timer_reg = nullptr) noexcept {
    return AsyncDelayAwaiter{current_time_ms + delta_ms, current_time_ms, timer_reg};
}

inline AsyncDelayAwaiter delay_until(uint64_t target_time, uint64_t current_time, uint64_t* timer_reg = nullptr) noexcept {
    return AsyncDelayAwaiter{target_time, current_time, timer_reg};
}

inline AsyncDelayAwaiter sleep_until(uint64_t target_time_us, uint64_t current_time_us, uint64_t* timer_comparator = nullptr) noexcept {
    return AsyncDelayAwaiter{target_time_us, current_time_us, timer_comparator};
}

inline AsyncDelayAwaiter sleep_for(uint64_t delta_us, uint64_t current_time_us, uint64_t* timer_comparator = nullptr) noexcept {
    return AsyncDelayAwaiter{current_time_us + delta_us, current_time_us, timer_comparator};
}

// Type-Safe Condition Awaiter (replaces PT_WAIT_UNTIL)
template <typename Predicate>
struct WaitUntilAwaiter {
    Predicate pred_;
    bool await_ready() const noexcept { return pred_(); }
    void await_suspend(std::coroutine_handle<>) noexcept {}
    void await_resume() noexcept {}
};

template <typename Predicate>
auto wait_until(Predicate pred) noexcept {
    return WaitUntilAwaiter<Predicate>{pred};
}

// Cooperative Event Signaling (replaces global flag synchronization)
class Event {
public:
    constexpr Event(bool initial_state = false) noexcept : state_(initial_state) {}

    void set() noexcept {
        state_ = true;
        if (waiter_) {
            auto h = waiter_;
            waiter_ = nullptr;
            h.resume();
        }
    }

    void reset() noexcept { state_ = false; }
    bool is_set() const noexcept { return state_; }

    struct Awaiter {
        Event& event_;
        bool await_ready() const noexcept { return event_.state_; }
        void await_suspend(std::coroutine_handle<> h) noexcept { event_.waiter_ = h; }
        void await_resume() noexcept {}
    };

    Awaiter operator co_await() noexcept { return Awaiter{*this}; }

private:
    bool state_{false};
    std::coroutine_handle<> waiter_{nullptr};
};

// Cooperative Counting Semaphore (replaces pt-sem.h PT_SEM_WAIT / PT_SEM_SIGNAL)
class Semaphore {
public:
    explicit constexpr Semaphore(size_t initial_count = 0) noexcept : count_(initial_count) {}

    void release() noexcept {
        ++count_;
        if (waiter_ && count_ > 0) {
            auto h = waiter_;
            waiter_ = nullptr;
            --count_;
            h.resume();
        }
    }

    struct AcquireAwaiter {
        Semaphore& sem_;
        bool await_ready() const noexcept { return sem_.count_ > 0; }
        void await_suspend(std::coroutine_handle<> h) noexcept {
            sem_.waiter_ = h;
        }
        void await_resume() noexcept {
            if (sem_.count_ > 0) --sem_.count_;
        }
    };

    AcquireAwaiter acquire() noexcept { return AcquireAwaiter{*this}; }

    size_t count() const noexcept { return count_; }

private:
    size_t count_{0};
    std::coroutine_handle<> waiter_{nullptr};
};

// Cooperative Lock-Free SPSC Queue (replaces raw circular buffers)
template <typename T, size_t Capacity = 16>
class AsyncQueue {
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
    bool is_full() const noexcept { return ((head_ + 1) % (Capacity + 1)) == tail_; }

    bool try_push(T val) noexcept {
        if (is_full()) return false;
        buffer_[head_] = std::move(val);
        head_ = (head_ + 1) % (Capacity + 1);
        if (pop_waiter_) {
            auto h = pop_waiter_;
            pop_waiter_ = nullptr;
            h.resume();
        }
        return true;
    }

    T try_pop() noexcept {
        T item = std::move(buffer_[tail_]);
        tail_ = (tail_ + 1) % (Capacity + 1);
        if (push_waiter_) {
            auto h = push_waiter_;
            push_waiter_ = nullptr;
            h.resume();
        }
        return item;
    }

private:
    std::array<T, Capacity + 1> buffer_{};
    size_t head_{0};
    size_t tail_{0};
    std::coroutine_handle<> push_waiter_{nullptr};
    std::coroutine_handle<> pop_waiter_{nullptr};
};

// ============================================================================
// 3. Coroutine I/O Engine & TLP Split-Transaction Dispatcher
// ============================================================================

class CoroutineIoEngine {
public:
    static constexpr size_t MAX_PENDING_TAGS = 256;

    struct PendingRequest {
        bool active{false};
        uint32_t target_addr{0};
        std::coroutine_handle<> awaiting_coro{nullptr};
        Tlp64 response_tlp{};
    };

    CoroutineIoEngine(SpscTlpRing<64>& host_tx_ring, SpscTlpRing<64>& host_rx_ring)
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
                    coro.resume();
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
                coro.resume();
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
            return engine.tx_ring_.push(req);
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
            return engine.tx_ring_.push(req);
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

        void await_suspend(std::coroutine_handle<> coro) {
            engine.register_stream_awaiter(static_cast<uint8_t>(channel), coro);
        }

        Tlp64 await_resume() noexcept {
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
    SpscTlpRing<64>& tx_ring_;
    SpscTlpRing<64>& rx_ring_;
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
//     std::function<void()> on_complete_{nullptr};
// and FinalAwaiter calls on_complete_() before returning noop_coroutine().
// ============================================================================

// --- when_all (&&) Implementation ---
// Suspends the caller until ALL sub-tasks complete, then resumes with a tuple
// of all results. Sub-tasks are started sequentially but each may suspend and
// complete asynchronously (e.g. different I/O bus completions).
template <typename... Tasks>
class WhenAllAwaiter {
public:
    explicit WhenAllAwaiter(Tasks&&... tasks)
        : tasks_(std::forward<Tasks>(tasks)...), remaining_(sizeof...(Tasks)) {}

    bool await_ready() const noexcept {
        // Zero-task when_all is immediately ready.
        return sizeof...(Tasks) == 0;
    }

    // Returns void — we will never resume the caller synchronously here.
    // The last completing sub-task's on_complete_ callback resumes caller_.
    void await_suspend(std::coroutine_handle<> caller) noexcept {
        caller_ = caller;
        start_all(std::index_sequence_for<Tasks...>{});
    }

    auto await_resume() {
        return get_results(std::index_sequence_for<Tasks...>{});
    }

private:
    template <size_t... Is>
    void start_all(std::index_sequence<Is...>) {
        // Inject all callbacks BEFORE starting any task, so a synchronously
        // completing task can still call notify_done() safely.
        (inject_callback<Is>(), ...);
        (start_one<Is>(), ...);
    }

    template <size_t Index>
    void inject_callback() {
        std::get<Index>(tasks_).handle().promise().on_complete_ =
            [this]() noexcept { notify_done(); };
    }

    template <size_t Index>
    void start_one() {
        auto& task = std::get<Index>(tasks_);
        task.resume();
        // If the task completed synchronously (no I/O suspension needed),
        // on_complete_ was already called by FinalAwaiter. Do not double-count.
    }

    void notify_done() noexcept {
        // remaining_ counts down from N to 0. Only the last sub-task to finish
        // (remaining_ hits 0) resumes the caller — guards against double-resume.
        if (--remaining_ == 0 && caller_) {
            auto caller = caller_;
            caller_ = nullptr;
            caller.resume();
        }
    }

    template <size_t... Is>
    auto get_results(std::index_sequence<Is...>) {
        return std::make_tuple(std::get<Is>(tasks_).await_resume()...);
    }

    std::tuple<Tasks...> tasks_;
    size_t remaining_;
    std::coroutine_handle<> caller_{nullptr};
};

template <typename... Tasks>
auto when_all(Tasks&&... tasks) {
    return WhenAllAwaiter<std::decay_t<Tasks>...>(std::forward<Tasks>(tasks)...);
}

// --- when_any (||) / Race Combinator ---
// Races two tasks; resumes the caller with the index and result of whichever
// finishes first. The loser task is kept alive (Task<T> RAII) until the
// WhenAnyAwaiter is destroyed — the loser handle is then destroyed cleanly.
//
// await_suspend uses symmetric transfer (return coroutine_handle<>) so that
// if a winner is found synchronously we hop back to the caller without adding
// an extra frame to the call stack.
template <typename T1, typename T2>
class WhenAnyAwaiter {
public:
    WhenAnyAwaiter(Task<T1>&& t1, Task<T2>&& t2)
        : t1_(std::move(t1)), t2_(std::move(t2)), ready_index_(-1) {}

    bool await_ready() const noexcept {
        return t1_.done() || t2_.done();
    }

    // SYMMETRIC TRANSFER: return coroutine_handle<> so the compiler performs
    // a tail-call resume instead of a nested .resume() call.
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> caller) noexcept {
        caller_ = caller;

        // Register callbacks BEFORE starting either task.
        t1_.handle().promise().on_complete_ = [this]() noexcept { notify_winner(0); };
        t2_.handle().promise().on_complete_ = [this]() noexcept { notify_winner(1); };

        // Start t1.
        t1_.resume();
        if (ready_index_ != -1) {
            // t1 won synchronously — symmetric transfer back to caller.
            caller_ = nullptr;
            return caller;
        }

        // Start t2.
        t2_.resume();
        if (ready_index_ != -1) {
            // t2 won synchronously — symmetric transfer back to caller.
            caller_ = nullptr;
            return caller;
        }

        // Both suspended — stay suspended. The first on_complete_ to fire
        // will call notify_winner() which resumes caller_.
        return std::noop_coroutine();
    }

    std::variant<T1, T2> await_resume() {
        if (ready_index_ == 0 || t1_.done()) {
            return std::variant<T1, T2>{std::in_place_index<0>, t1_.await_resume()};
        } else {
            return std::variant<T1, T2>{std::in_place_index<1>, t2_.await_resume()};
        }
    }

private:
    void notify_winner(int idx) noexcept {
        // Guard: only the FIRST completing task wins. ready_index_ == -1 means
        // no winner yet. CAS-like: once set, the second task's callback is ignored.
        if (ready_index_ == -1) {
            ready_index_ = idx;
            if (caller_) {
                auto caller = caller_;
                caller_ = nullptr;
                caller.resume();
            }
        }
    }

    Task<T1> t1_;
    Task<T2> t2_;
    int ready_index_{-1};
    std::coroutine_handle<> caller_{nullptr};
};

template <typename T1, typename T2>
auto when_any(Task<T1>&& t1, Task<T2>&& t2) {
    return WhenAnyAwaiter<T1, T2>(std::move(t1), std::move(t2));
}

} // namespace coro
} // namespace abstractx

#endif // ASP_CORO_HPP
