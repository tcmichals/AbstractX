/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * bench.cpp - E906 runtime benchmark shared by bare metal and FreeRTOS builds.
 * @impl [SPEC-E906-BENCH-01] [SPEC-E906-BENCH-02] [SPEC-E906-BENCH-03] [SPEC-E906-BENCH-04] [SPEC-E906-BENCH-05] apps/e906_bench/SPECIFICATION.md
 */
#include "bench.hpp"

#include <atomic>
#include <cstddef>
#include "abstractx/abstractx.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/hal/io_engine.hpp"
#include "abstractx/hal/platform.hpp"
#include "applib/drivers/imu/icm42688p.hpp"
#include "hal/auto_dma.hpp"
#include "hal/gpio_irq.hpp"
#include "hal/timer.hpp"
#include "tlp_ring.hpp"

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
std::atomic<uint32_t> s_soft_flag{0};      // shared: soft ISR sets, probe coroutine reads; std::atomic release/acquire
std::coroutine_handle<> s_soft_waiter{};   // shared: SoftIrqAwaiter sets before raising MSIP, soft ISR takes it; same hart, IRQ raised after the store
volatile uint32_t s_soft_t0 = 0;           // shared: awaiter writes before MSIP, main reads after resume; same hart
volatile uint32_t s_soft_tisr = 0;         // shared: soft ISR writes, main reads after resume (ordered by the Dispatcher post); same hart

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

// ---------------------------------------------------------------------------
// [SPEC-E906-BENCH-05] IMU DRDY auto-DMA run
// ---------------------------------------------------------------------------
constexpr uint32_t kImuDefaultSamples = 2000;
constexpr uint32_t kImuMaxSamples = 100000;
constexpr uint32_t kImuFirstSampleMs = 500;
constexpr uint64_t kImuPeriodNs = 1'000'000;   // 1 kHz ODR

abstractx::MpscTlpRing<> s_imu_ring;                // shared: DMA ISR pushes (MPSC, InterruptLock), collector pops
std::coroutine_handle<> s_imu_waiter{};              // shared: collector sets, DMA ISR / timeout take it under InterruptGuard
volatile bool s_imu_timeout = false;                 // shared: timeout coroutine sets, collector reads; same hart
uint32_t s_imu_run = 0;                              // coroutine domain only: run id for the timeout coroutine

void imu_wake() noexcept {   // ISR or coroutine context; never resumes inline
    std::coroutine_handle<> h{};
    {
        abstractx::InterruptGuard guard;
        h = s_imu_waiter;
        s_imu_waiter = {};
    }
    if (h) abstractx::IsrDispatcher::post(h);
}

void on_imu_pushed() noexcept { imu_wake(); }

struct ImuSampleAwaiter {
    bool await_ready() const noexcept { return !s_imu_ring.empty() || s_imu_timeout; }
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        abstractx::InterruptGuard guard;
        if (!s_imu_ring.empty() || s_imu_timeout) return false;   // arrived after await_ready
        s_imu_waiter = h;
        return true;
    }
    void await_resume() const noexcept {}
};

// Sleeps in short steps and returns soon after its run ends, so a finished run leaves no
// frame behind for the next one ([SPEC-E906-BENCH-05], steady state).
abstractx::coro::Task<void> imu_timeout_task(uint32_t run, uint32_t first_ms, uint32_t total_ms) noexcept {
    constexpr uint32_t kStepMs = 50;
    for (uint32_t t = 0; t < total_ms; t += kStepMs) {
        co_await ::hal::Timer::sleep_ms_async(kStepMs);
        if (run != s_imu_run) co_return;
        if (t + kStepMs >= first_ms && t < first_ms && ::hal::auto_dma::stats().completed == 0) break;
    }
    if (run == s_imu_run) {
        s_imu_timeout = true;
        imu_wake();
    }
}

// Static so the driver and its config stay out of the coroutine frame pool.
applib::drivers::imu::Icm42688p s_imu{abstractx::hal::get_spi_driver(),
                                      {10'000'000, applib::drivers::imu::IcmRegs::ODR_1KHZ}};
abstractx::hal::AutoChannelConfig s_imu_channel{};

void hex_line(Report &r, const char *key, std::span<const uint8_t> bytes) noexcept;

abstractx::coro::Task<void> run_imu(::hal::IRpmsg &rpmsg, uint32_t host, uint32_t count) noexcept {
    namespace ax = abstractx::hal;
    Report r(rpmsg, host);
    r.kv("platform", abx_bench_platform_name());
    if (count == 0) count = kImuDefaultSamples;
    if (count > kImuMaxSamples) count = kImuMaxSamples;

    if (!co_await s_imu.init_async()) {
        r.kv("imu_init", "failed");
        r.kv("end", static_cast<uint64_t>(1));
        r.flush();
        co_return;
    }
    r.kv("imu_init", "ok");

    s_imu_channel = {};
    s_imu_channel.channel_id = 0;
    s_imu_channel.auto_mode = true;
    s_imu_channel.trigger_mode = ax::TriggerMode::GpioEdge;
    s_imu_channel.trigger_pin = ::hal::gpio_irq::kImuDrdy;
    s_imu_channel.trigger_rising = true;
    s_imu_channel.bus_type = ax::BusType::Spi;
    s_imu_channel.bus_index = 1;
    s_imu_channel.bus_speed_hz = 10'000'000;
    s_imu_channel.cs_pin = 0;
    s_imu_channel.tx_cmd[0] = static_cast<uint8_t>(applib::drivers::imu::IcmRegs::TEMP_DATA1 | 0x80);
    s_imu_channel.tx_len = 1;
    s_imu_channel.rx_len = 14;
    s_imu_channel.tlp_channel = static_cast<uint8_t>(abstractx::Channel::Telemetry);
    s_imu_channel.tlp_tag = applib::drivers::imu::TLP_TAG_IMU;

    auto &io = static_cast<ax::IoEngine &>(ax::get_target_io_processor());
    s_imu_ring.clear();
    s_imu_timeout = false;
    ++s_imu_run;
    ax::IoProcessorSetup setup{};
    setup.channels = std::span<const ax::AutoChannelConfig>(&s_imu_channel, 1);
    setup.ingress_rx_ring = &s_imu_ring;
    setup.on_rx_pushed = etl::delegate<void()>::create<on_imu_pushed>();

    const uint32_t overruns0 = io.auto_overruns();
    const uint32_t ingress0 = io.ingress_dropped();
    const uint32_t edges0 = ::hal::gpio_irq::edges(::hal::gpio_irq::kImuDrdy);
    ::hal::auto_dma::reset();
    io.configure(setup);
    io.start();
    if (!abstractx::spawn(imu_timeout_task(s_imu_run, kImuFirstSampleMs, count + 1000U))) s_imu_timeout = true;

    Stat period, wake;
    uint32_t samples = 0, gaps = 0;
    uint64_t prev_ts = 0;
    abstractx::Tlp64 tlp{};
    abstractx::Tlp64 last{};
    while (samples < count && !s_imu_timeout) {
        co_await ImuSampleAwaiter{};
        const uint64_t now = ::hal::Timer::get_time_ns();
        while (samples < count && s_imu_ring.pop(tlp)) {
            const uint64_t ts = tlp.wire.timestamp_ns;
            wake.add(static_cast<uint32_t>(now > ts ? now - ts : 0));
            if (prev_ts) {
                const uint64_t d = ts - prev_ts;
                period.add(static_cast<uint32_t>(d));
                if (d > kImuPeriodNs + kImuPeriodNs / 2) ++gaps;
            }
            prev_ts = ts;
            last = tlp;
            ++samples;
        }
    }
    io.stop();
    ++s_imu_run;   // a pending timeout coroutine now does nothing
    const auto st = ::hal::auto_dma::stats();

    r.kv("samples", samples);
    r.kv("imu_timeout", s_imu_timeout ? 1U : 0U);
    r.kv("edges", ::hal::gpio_irq::edges(::hal::gpio_irq::kImuDrdy) - edges0);
    r.kv("overruns", io.auto_overruns() - overruns0);
    r.kv("busy_drops", st.busy_drops);
    r.kv("dma_ok", st.completed);
    r.kv("dma_failed", st.failed);
    r.kv("ingress_dropped", io.ingress_dropped() - ingress0);
    r.kv("seq_gaps", gaps);
    r.stat("period_ns", period);
    Stat e2s{st.edge_to_start.min, st.edge_to_start.max, st.edge_to_start.sum, st.edge_to_start.n};
    Stat dma{st.dma.min, st.dma.max, st.dma.sum, st.dma.n};
    r.stat("edge_to_dma_cycles", e2s);
    r.stat("dma_cycles", dma);
    r.stat("edge_to_wake_ns", wake);
    const auto raw = last.payload();
    hex_line(r, "last_raw", samples && raw.size() >= 14 ? raw.first(14) : std::span<const uint8_t>{});
    r.kv("dropped_resumes", Dispatcher::dropped_resumes());
    r.kv("coro_pool_high_water", abstractx::coro::coro_pool_high_water());
    r.kv("end", static_cast<uint64_t>(1));
    r.flush();
}

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

void hex_line(Report &r, const char *key, std::span<const uint8_t> bytes) noexcept {
    static const char kHex[] = "0123456789abcdef";
    char buf[2 * 40 + 1];
    size_t n = 0;
    for (uint8_t b : bytes) {
        if (n + 2 >= sizeof(buf)) break;
        buf[n++] = kHex[b >> 4];
        buf[n++] = kHex[b & 0xF];
    }
    buf[n] = '\0';
    r.kv(key, buf);
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
            uint32_t count = 0;   // 'A' sample count, read before the slot is freed
            for (uint16_t i = 0; i < 4 && 1U + i < s.len; ++i) {
                count |= static_cast<uint32_t>(s.data[1 + i]) << (8U * i);
            }
            ++s_tail;  // free the slot before a long run
            if (cmd == 'R') {
                co_await run_suite(rpmsg, host);
            } else if (cmd == 'A') {
                co_await run_imu(rpmsg, host, count);
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
