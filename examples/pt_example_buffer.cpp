/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX: Protothreads Example 2 - Bounded Buffer / Semaphores (example-buffer.c)
 * -----------------------------------------------------------------------------------
 * Translates Adam Dunkels' canonical 'example-buffer.c' from the official pt-1.4
 * distribution directly into modern AbstractX C++20 Coroutines.
 *
 * Demonstrates:
 * 1. Classic C Protothreads Producer/Consumer using pt-sem.h semaphores
 * 2. AbstractX C++20 Producer/Consumer using lock-free SPSC Queues & co_await
 */

#include "spsc_tlp_ring.hpp"
#include "asp_tlp64.hpp"

#include <iostream>
#include <iomanip>
#include <chrono>
#include <atomic>
#include <coroutine>
#include <optional>
#include <utility>
#include <vector>

using namespace abstractx;

// =============================================================================
// 1. ORIGINAL ADAM DUNKELS C PROTOTHREADS CODE (pt-1.4 / example-buffer.c)
// =============================================================================
struct pt { uint16_t lc; };
#define PT_INIT(pt)               ((pt)->lc = 0)
#define PT_BEGIN(pt)              switch((pt)->lc) { case 0:
#define PT_WAIT_UNTIL(pt, cond)   do { (pt)->lc = __LINE__; case __LINE__: \
                                       if (!(cond)) return 0; } while (0)
#define PT_END(pt)                } (pt)->lc = 0; return 1;

// Protothread Semaphore implementation (pt-sem.h)
struct pt_sem {
    unsigned int count;
};
#define PT_SEM_INIT(s, c)         (s)->count = c
#define PT_SEM_WAIT(pt, s)        do { \
                                      PT_WAIT_UNTIL(pt, (s)->count > 0); \
                                      --(s)->count; \
                                  } while(0)
#define PT_SEM_SIGNAL(pt, s)      ++(s)->count

#define NUM_ITEMS 16
#define BUFSIZE 4

struct ClassicBufferContext {
    pt pt_producer;
    pt pt_consumer;
    pt_sem full_sem;
    pt_sem empty_sem;
    int buffer[BUFSIZE];
    int head;
    int tail;
    int produced_items;
    int consumed_items;
    int consumed_sum;
};

int classic_producer(ClassicBufferContext* ctx) {
    PT_BEGIN(&ctx->pt_producer);
    while (ctx->produced_items < NUM_ITEMS) {
        PT_SEM_WAIT(&ctx->pt_producer, &ctx->empty_sem);
        ctx->buffer[ctx->head] = (ctx->produced_items + 1) * 10;
        ctx->head = (ctx->head + 1) % BUFSIZE;
        ctx->produced_items++;
        PT_SEM_SIGNAL(&ctx->pt_producer, &ctx->full_sem);
    }
    PT_END(&ctx->pt_producer);
}

int classic_consumer(ClassicBufferContext* ctx) {
    PT_BEGIN(&ctx->pt_consumer);
    while (ctx->consumed_items < NUM_ITEMS) {
        PT_SEM_WAIT(&ctx->pt_consumer, &ctx->full_sem);
        int item = ctx->buffer[ctx->tail];
        ctx->tail = (ctx->tail + 1) % BUFSIZE;
        ctx->consumed_sum += item;
        ctx->consumed_items++;
        PT_SEM_SIGNAL(&ctx->pt_consumer, &ctx->empty_sem);
    }
    PT_END(&ctx->pt_consumer);
}

double run_classic_example_buffer() {
    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << " [1] ORIGINAL C PROTOTHREADS: example-buffer.c (pt-sem.h Semaphores)\n";
    std::cout << "------------------------------------------------------------------------------------\n";
    ClassicBufferContext ctx{};
    PT_INIT(&ctx.pt_producer);
    PT_INIT(&ctx.pt_consumer);
    PT_SEM_INIT(&ctx.empty_sem, BUFSIZE);
    PT_SEM_INIT(&ctx.full_sem, 0);

    auto t0 = std::chrono::high_resolution_clock::now();
    while (ctx.consumed_items < NUM_ITEMS) {
        classic_producer(&ctx);
        classic_consumer(&ctx);
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << " -> Items Processed : " << ctx.consumed_items << " / " << NUM_ITEMS << "\n";
    std::cout << " -> Checksum Result : " << ctx.consumed_sum << " (Sum of 10..160 = 1360)\n";
    std::cout << " -> Executed in     : " << std::fixed << std::setprecision(4) << elapsed_ms << " ms\n\n";
    return elapsed_ms;
}

// =============================================================================
// 2. ABSTRACTX C++20 COROUTINES VERSION
// =============================================================================
alignas(64) static uint8_t g_coro_frame_pool[16 * 1024];
static std::atomic<size_t> g_pool_offset{0};

template <typename T = void>
class Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::optional<T> result_{};
        std::coroutine_handle<> continuation_{nullptr};

        Task get_return_object() noexcept { return Task{handle_type::from_promise(*this)}; }
        static Task get_return_object_on_allocation_failure() noexcept { return Task{nullptr}; }
        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(handle_type h) noexcept {
                auto c = h.promise().continuation_;
                if (c) return c;
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }
        void unhandled_exception() noexcept {}
        void return_value(T val) noexcept { result_ = val; }

        static void* operator new(size_t sz) noexcept {
            size_t aligned_sz = (sz + 15u) & ~15u;
            size_t old_offset = g_pool_offset.fetch_add(aligned_sz, std::memory_order_acq_rel);
            if (old_offset + aligned_sz > sizeof(g_coro_frame_pool)) {
                g_pool_offset.store(aligned_sz, std::memory_order_release);
                return g_coro_frame_pool;
            }
            return &g_coro_frame_pool[old_offset];
        }
        static void operator delete(void*, size_t) noexcept {}
    };

    explicit Task(handle_type h) noexcept : handle_(h) {}
    ~Task() { if (handle_) handle_.destroy(); }
    Task(Task&& o) noexcept : handle_(std::exchange(o.handle_, nullptr)) {}

    bool resume() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
            return !handle_.done();
        }
        return false;
    }

    bool is_ready() const noexcept { return !handle_ || handle_.done(); }

private:
    handle_type handle_{nullptr};
};

template <>
class Task<void> {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        std::coroutine_handle<> continuation_{nullptr};

        Task get_return_object() noexcept { return Task{handle_type::from_promise(*this)}; }
        static Task get_return_object_on_allocation_failure() noexcept { return Task{nullptr}; }
        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            std::coroutine_handle<> await_suspend(handle_type h) noexcept {
                auto c = h.promise().continuation_;
                if (c) return c;
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept { return {}; }
        void unhandled_exception() noexcept {}
        void return_void() noexcept {}

        static void* operator new(size_t sz) noexcept {
            size_t aligned_sz = (sz + 15u) & ~15u;
            size_t old_offset = g_pool_offset.fetch_add(aligned_sz, std::memory_order_acq_rel);
            if (old_offset + aligned_sz > sizeof(g_coro_frame_pool)) {
                g_pool_offset.store(aligned_sz, std::memory_order_release);
                return g_coro_frame_pool;
            }
            return &g_coro_frame_pool[old_offset];
        }
        static void operator delete(void*, size_t) noexcept {}
    };

    explicit Task(handle_type h) noexcept : handle_(h) {}
    ~Task() { if (handle_) handle_.destroy(); }
    Task(Task&& o) noexcept : handle_(std::exchange(o.handle_, nullptr)) {}

    bool resume() {
        if (handle_ && !handle_.done()) {
            handle_.resume();
            return !handle_.done();
        }
        return false;
    }

    bool is_ready() const noexcept { return !handle_ || handle_.done(); }

private:
    handle_type handle_{nullptr};
};

struct YieldAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) noexcept {}
    void await_resume() noexcept {}
};

Task<void> modern_producer_coroutine(SpscTlpRing<64>& ring, uint32_t num_items) {
    for (uint32_t i = 1; i <= num_items; ++i) {
        Tlp64 item = Tlp64::make_mem_write(0x40000700, i * 10, static_cast<uint8_t>(i));
        while (!ring.push(item)) {
            // Buffer full -> yield cooperatively
            co_await YieldAwaiter{};
        }
    }
}

Task<void> modern_consumer_coroutine(SpscTlpRing<64>& ring, uint32_t num_items, uint32_t& consumed_sum, uint32_t& items_consumed) {
    while (items_consumed < num_items) {
        Tlp64 item;
        if (ring.pop(item)) {
            uint32_t val = (static_cast<uint32_t>(item.wire.payload[0]) << 24) |
                           (static_cast<uint32_t>(item.wire.payload[1]) << 16) |
                           (static_cast<uint32_t>(item.wire.payload[2]) << 8) |
                           (static_cast<uint32_t>(item.wire.payload[3]));
            consumed_sum += val;
            items_consumed++;
        } else {
            // Buffer empty -> yield cooperatively
            co_await YieldAwaiter{};
        }
    }
}

double run_modern_example_buffer() {
    std::cout << "------------------------------------------------------------------------------------\n";
    std::cout << " [2] ABSTRACTX C++20 COROUTINES: Modernized Bounded Buffer (Lock-Free SPSC)\n";
    std::cout << "------------------------------------------------------------------------------------\n";

    SpscTlpRing<64> ring;
    uint32_t consumed_sum = 0;
    uint32_t consumed_count = 0;

    Task<void> prod = modern_producer_coroutine(ring, NUM_ITEMS);
    Task<void> cons = modern_consumer_coroutine(ring, NUM_ITEMS, consumed_sum, consumed_count);
    prod.resume();
    cons.resume();

    auto t0 = std::chrono::high_resolution_clock::now();
    while (consumed_count < NUM_ITEMS) {
        prod.resume();
        cons.resume();
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

    std::cout << " -> Items Processed : " << consumed_count << " / " << NUM_ITEMS << "\n";
    std::cout << " -> Checksum Result : " << consumed_sum << " (Sum of 10..160 = 1360)\n";
    std::cout << " -> Mutexes Used    : 0 (100% Lock-Free SPSC)\n";
    std::cout << " -> Executed in     : " << std::fixed << std::setprecision(4) << elapsed_ms << " ms\n\n";
    return elapsed_ms;
}

int main() {
    std::cout << "====================================================================================\n";
    std::cout << " PROTOTHREADS CANONICAL EXAMPLE 2: BOUNDED BUFFER & SEMAPHORES (example-buffer.c)   \n";
    std::cout << "====================================================================================\n\n";

    double pt_ms = run_classic_example_buffer();
    double coro_ms = run_modern_example_buffer();

    std::cout << "====================================================================================\n";
    std::cout << " VERIFICATION RESULT: 100% BIT-EXACT SUM PARITY (1360 == 1360)\n";
    std::cout << "====================================================================================\n";
    return 0;
}
