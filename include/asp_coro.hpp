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

namespace abstractx {
namespace coro {

// ============================================================================
// 1. Lightweight C++20 Coroutine Task<T>
// ============================================================================

template <typename T = void>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T> result_{};
        std::coroutine_handle<> continuation_{nullptr};
        std::exception_ptr exception_{nullptr};

        Task get_return_object() noexcept {
            return Task{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(handle_type h) noexcept {
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

    handle_type handle() const noexcept { return handle_; }

    void resume() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    bool done() const noexcept {
        return !handle_ || handle_.done();
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

        Task get_return_object() noexcept {
            return Task{handle_type::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(handle_type h) noexcept {
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

    void resume() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    bool done() const noexcept {
        return !handle_ || handle_.done();
    }

private:
    handle_type handle_{nullptr};
};

// ============================================================================
// 2. Coroutine I/O Engine & TLP Split-Transaction Dispatcher
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

// ============================================================================
// 3. Concurrency Combinators: when_all (&&) and when_any (||)
// ============================================================================

// --- when_all (&&) Implementation ---
template <typename... Tasks>
class WhenAllAwaiter {
public:
    explicit WhenAllAwaiter(Tasks&&... tasks)
        : tasks_(std::forward<Tasks>(tasks)...), remaining_(sizeof...(Tasks)) {}

    bool await_ready() const noexcept {
        return sizeof...(Tasks) == 0;
    }

    void await_suspend(std::coroutine_handle<> caller) {
        caller_ = caller;
        start_all(std::index_sequence_for<Tasks...>{});
    }

    auto await_resume() {
        return get_results(std::index_sequence_for<Tasks...>{});
    }

private:
    template <size_t... Is>
    void start_all(std::index_sequence<Is...>) {
        (start_one<Is>(), ...);
    }

    template <size_t Index>
    void start_one() {
        auto& task = std::get<Index>(tasks_);
        task.handle().promise().continuation_ = std::coroutine_handle<>::from_address(this);
        task.resume();
        if (task.done()) {
            notify_done();
        }
    }

    void notify_done() {
        if (--remaining_ == 0 && caller_) {
            caller_.resume();
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
template <typename T1, typename T2>
class WhenAnyAwaiter {
public:
    WhenAnyAwaiter(Task<T1>&& t1, Task<T2>&& t2)
        : t1_(std::move(t1)), t2_(std::move(t2)), ready_index_(-1) {}

    bool await_ready() const noexcept {
        return t1_.done() || t2_.done();
    }

    void await_suspend(std::coroutine_handle<> caller) {
        caller_ = caller;
        t1_.resume();
        if (t1_.done()) {
            ready_index_ = 0;
            caller_.resume();
            return;
        }
        t2_.resume();
        if (t2_.done()) {
            ready_index_ = 1;
            caller_.resume();
            return;
        }
    }

    std::variant<T1, T2> await_resume() {
        if (ready_index_ == 0 || t1_.done()) {
            return std::variant<T1, T2>{std::in_place_index<0>, t1_.await_resume()};
        } else {
            return std::variant<T1, T2>{std::in_place_index<1>, t2_.await_resume()};
        }
    }

    void notify_winner(int idx) {
        if (ready_index_ == -1) {
            ready_index_ = idx;
            if (caller_) caller_.resume();
        }
    }

private:
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
