/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * bench.cpp - E906 runtime benchmark shared by bare metal and FreeRTOS builds.
 * @impl [SPEC-E906-BENCH-01] [SPEC-E906-BENCH-02] [SPEC-E906-BENCH-03] [SPEC-E906-BENCH-04] apps/e906_bench/SPECIFICATION.md
 */
#include "bench.hpp"

#include <atomic>
#include <cstddef>
#include "abstractx/domain_dispatcher.hpp"

extern "C" {

__attribute__((weak)) const char *abx_bench_platform_name(void) { return "baremetal"; }
__attribute__((weak)) bool abx_bench_soft_irq_enable(void) { return false; }
__attribute__((weak)) bool abx_bench_idle_stats(uint32_t *, uint64_t *) { return false; }

uint64_t abx_bench_mcycle(void) {
#if defined(__riscv)
    uint32_t hi0, hi1, lo;
    do {
        asm volatile("csrr %0, mcycleh" : "=r"(hi0));
        asm volatile("csrr %0, mcycle" : "=r"(lo));
        asm volatile("csrr %0, mcycleh" : "=r"(hi1));
    } while (hi0 != hi1);
    return (static_cast<uint64_t>(hi0) << 32) | lo;
#else
    return 0;
#endif
}

} // extern "C"

namespace e906_bench {
namespace {

using abstractx::Dispatcher;

constexpr size_t kPayload = 496;   // RPMsg buffer 512 B minus 16 B header
constexpr size_t kSlots = 4;
constexpr uint32_t kYieldRuns = 10000;
constexpr uint32_t kSoftIrqRuns = 1000;
constexpr uint32_t kProbeYields = 1000;

// CLINT machine software interrupt pending (CLIC line 3) [SPEC-E906-BENCH-03]
volatile uint32_t *const kMsip = reinterpret_cast<volatile uint32_t *>(0xE0000000u);

struct Slot {
    uint32_t src;
    uint16_t len;
    uint8_t data[kPayload];
};

// Coroutine domain only: written by the rx callback (inside RPMsg processing on the
// AbstractX context) and read by the worker coroutine.
Slot s_slots[kSlots];
uint32_t s_head = 0;
uint32_t s_tail = 0;
std::coroutine_handle<> s_worker{};
uint32_t s_rx_msgs = 0;
uint32_t s_rx_dropped = 0;
uint32_t s_tx_fail = 0;

// Shared with abx_bench_soft_isr() (ISR domain).
std::atomic<uint32_t> s_soft_flag{0};
std::coroutine_handle<> s_soft_waiter{};
volatile uint32_t s_soft_t0 = 0;
volatile uint32_t s_soft_tisr = 0;

inline uint32_t now32() noexcept { return static_cast<uint32_t>(abx_bench_mcycle()); }

void rx_callback(const ::hal::RpmsgMessage &msg, void *) noexcept {
    ++s_rx_msgs;
    if (s_head - s_tail >= kSlots || !msg.data) {
        ++s_rx_dropped;
        return;
    }
    Slot &s = s_slots[s_head % kSlots];
    s.src = msg.src;
    s.len = static_cast<uint16_t>(msg.len > kPayload ? kPayload : msg.len);
    for (uint16_t i = 0; i < s.len; ++i) {
        s.data[i] = msg.data[i];
    }
    ++s_head;
    if (s_worker) {
        auto h = s_worker;
        s_worker = {};
        Dispatcher::push(h);  // resumed by the runtime loop, not inline
    }
}

struct SlotAwaiter {
    bool await_ready() const noexcept { return s_head != s_tail; }
    void await_suspend(std::coroutine_handle<> h) noexcept { s_worker = h; }
    void await_resume() const noexcept {}
};

struct SoftIrqAwaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) noexcept {
        s_soft_waiter = h;
        s_soft_t0 = now32();
        *kMsip = 1;
    }
    uint32_t await_resume() const noexcept { return now32(); }
};

struct Stat {
    uint32_t min = 0xFFFFFFFFu;
    uint32_t max = 0;
    uint64_t sum = 0;
    uint32_t n = 0;
    void add(uint32_t v) noexcept {
        if (v < min) min = v;
        if (v > max) max = v;
        sum += v;
        ++n;
    }
    uint32_t avg() const noexcept { return n ? static_cast<uint32_t>(sum / n) : 0; }
};

// Text report, one RPMsg message per kPayload bytes. One report at a time, so the
// buffer is static and stays out of the coroutine frame pool.
uint8_t s_report_buf[kPayload];

class Report {
public:
    Report(::hal::IRpmsg &rpmsg, uint32_t dst) noexcept : rpmsg_(rpmsg), dst_(dst) {}

    void kv(const char *key, uint64_t value) noexcept {
        char digits[21];
        size_t n = 0;
        do {
            digits[n++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value);
        line_begin(key);
        while (n) put(digits[--n]);
        put('\n');
    }
    void kv(const char *key, const char *value) noexcept {
        line_begin(key);
        while (*value) put(*value++);
        put('\n');
    }
    void stat(const char *prefix, const Stat &s) noexcept {
        char key[40];
        static const char *suffix[3] = {"_min", "_avg", "_max"};
        const uint32_t vals[3] = {s.n ? s.min : 0, s.avg(), s.max};
        for (int i = 0; i < 3; ++i) {
            size_t k = 0;
            for (const char *p = prefix; *p && k < sizeof(key) - 6; ++p) key[k++] = *p;
            for (const char *p = suffix[i]; *p; ++p) key[k++] = *p;
            key[k] = '\0';
            kv(key, vals[i]);
        }
    }
    void flush() noexcept {
        if (len_ == 0) return;
        ::hal::RpmsgMessage to{};
        to.src = dst_;
        to.dst = ENDPOINT_ADDR;
        if (!rpmsg_.reply(to, s_report_buf, static_cast<uint16_t>(len_))) ++s_tx_fail;
        len_ = 0;
    }

private:
    void line_begin(const char *key) noexcept {
        size_t need = 0;
        for (const char *p = key; *p; ++p) ++need;
        if (len_ + need + 24 > kPayload) flush();
        while (*key) put(*key++);
        put('=');
    }
    void put(char c) noexcept {
        if (len_ >= kPayload) flush();
        s_report_buf[len_++] = static_cast<uint8_t>(c);
    }
    ::hal::IRpmsg &rpmsg_;
    uint32_t dst_;
    size_t len_ = 0;
};

abstractx::coro::Task<void> run_suite(::hal::IRpmsg &rpmsg, uint32_t host) noexcept {
    Report r(rpmsg, host);
    r.kv("platform", abx_bench_platform_name());

    // 1. One runtime loop pass per yield [SPEC-E906-BENCH-02]
    Stat yield;
    for (uint32_t i = 0; i < kYieldRuns; ++i) {
        const uint32_t t0 = now32();
        co_await abstractx::yield_to_dispatcher();
        yield.add(now32() - t0);
    }
    r.kv("yield_runs", kYieldRuns);
    r.stat("yield_cycles", yield);

    // 2. Software interrupt -> ISR -> Dispatcher -> coroutine [SPEC-E906-BENCH-03]
    if (!abx_bench_soft_irq_enable()) {
        r.kv("softirq", "unsupported");
    } else {
        s_soft_flag.store(0, std::memory_order_relaxed);
        s_soft_waiter = {};
        *kMsip = 1;
        for (uint32_t i = 0; i < kProbeYields && !s_soft_flag.load(std::memory_order_acquire); ++i) {
            co_await abstractx::yield_to_dispatcher();
        }
        if (!s_soft_flag.load(std::memory_order_acquire)) {
            *kMsip = 0;
            r.kv("softirq", "no-interrupt");
        } else {
            Stat isr, wake;
            for (uint32_t i = 0; i < kSoftIrqRuns; ++i) {
                const uint32_t t1 = co_await SoftIrqAwaiter{};
                isr.add(s_soft_tisr - s_soft_t0);
                wake.add(t1 - s_soft_t0);
            }
            r.kv("softirq_runs", kSoftIrqRuns);
            r.stat("softirq_isr_cycles", isr);
            r.stat("softirq_wake_cycles", wake);
        }
    }

    // 3. Idle accounting and health counters
    uint32_t wakes = 0;
    uint64_t idle = 0;
    if (abx_bench_idle_stats(&wakes, &idle)) {
        r.kv("idle_wakes", wakes);
        r.kv("idle_cycles", idle);
        r.kv("window_cycles", abx_bench_mcycle());
    } else {
        r.kv("idle", "not-instrumented");
    }
    r.kv("dropped_resumes", Dispatcher::dropped_resumes());
    r.kv("coro_pool_high_water", abstractx::coro::coro_pool_high_water());
    r.kv("rx_msgs", s_rx_msgs);
    r.kv("rx_dropped", s_rx_dropped);
    r.kv("tx_fail", s_tx_fail);
    r.kv("end", static_cast<uint64_t>(1));
    r.flush();
}

} // namespace

void init(::hal::IRpmsg &rpmsg) noexcept {
    rpmsg.register_endpoint(ENDPOINT_ADDR, rx_callback, nullptr);
}

abstractx::coro::Task<void> rx_task(::hal::IRpmsg &rpmsg) noexcept {
    bool announced = false;
    for (;;) {
        if (!announced && rpmsg.is_driver_ready()) {
            announced = rpmsg.announce_service(SERVICE_NAME, ENDPOINT_ADDR);
        }
        // Wakes on the message box interrupt; processing runs in await_resume().
        co_await rpmsg.async_receive();
    }
}

abstractx::coro::Task<void> worker_task(::hal::IRpmsg &rpmsg) noexcept {
    for (;;) {
        co_await SlotAwaiter{};
        Slot &s = s_slots[s_tail % kSlots];
        const uint8_t cmd = s.len ? s.data[0] : 0;
        ::hal::RpmsgMessage to{};
        to.src = s.src;
        to.dst = ENDPOINT_ADDR;
        if (cmd == 'E') {
            if (!rpmsg.reply(to, s.data, s.len)) ++s_tx_fail;
            ++s_tail;
        } else {
            const uint32_t host = s.src;
            ++s_tail;  // free the slot before a long run
            if (cmd == 'R') {
                co_await run_suite(rpmsg, host);
            } else if (cmd == 'I') {
                Report r(rpmsg, host);
                r.kv("platform", abx_bench_platform_name());
                r.flush();
            }
        }
    }
}

} // namespace e906_bench

// [SPEC-E906-BENCH-03] integer only, never resumes ([SPEC-E906-RTOS-06])
extern "C" void abx_bench_soft_isr(void) {
    using namespace e906_bench;
    *kMsip = 0;
    s_soft_tisr = now32();
    s_soft_flag.store(1, std::memory_order_release);
    if (s_soft_waiter) {
        auto h = s_soft_waiter;
        s_soft_waiter = {};
        abstractx::IsrDispatcher::post(h);
    }
}
