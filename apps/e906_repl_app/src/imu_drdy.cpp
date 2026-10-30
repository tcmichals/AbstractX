/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * imu_drdy.cpp - MicroPython glue for the IMU DRDY auto-DMA channel (abstractx.imu_drdy_*).
 * The read path is the target's IoEngine channel; this file only starts it and hands samples over.
 *
 * @impl [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
 */

#include "abstractx/abstractx.hpp"
#include "abstractx/hal/io_engine.hpp"
#include "abstractx/hal/platform.hpp"
#include "applib/drivers/imu/icm42688p.hpp"
#include "hal/auto_dma.hpp"
#include "hal/gpio_irq.hpp"
#include "hal/spi.hpp"
#include "hal/timer.hpp"
#include "repl_wait.hpp"
#include "tlp_ring.hpp"

#include <cstddef>
#include <cstdint>

namespace {

namespace ax = abstractx::hal;
namespace imu = applib::drivers::imu;

constexpr uint64_t kInitWaitMs = 200;
constexpr uint64_t kStopWaitMs = 5;      // one 15-byte DMA at 10 MHz is ~15 us
constexpr uint64_t kImuPeriodNs = 1'000'000;   // 1 kHz ODR
constexpr size_t kSampleBytes = 14;              // temp, accel xyz, gyro xyz

// Static so the driver, channel and ring stay out of the coroutine frame pool and in SRAM.
imu::Icm42688p s_imu{ax::get_spi_driver(), {10'000'000, imu::IcmRegs::ODR_1KHZ}};
ax::AutoChannelConfig s_channel{};
abstractx::MpscTlpRing<> s_ring;   // shared: DMA ISR pushes (MPSC, InterruptLock), the VM pops

volatile bool s_init_done = false;  // shared: init coroutine sets, the VM polls with a deadline; single hart
volatile bool s_init_ok = false;    // shared: init coroutine writes before s_init_done; single hart
bool s_running = false;             // VM only

struct Stat {
    uint32_t min{0xFFFFFFFFU}, max{0};
    uint64_t sum{0};
    uint32_t n{0};
    void add(uint32_t v) noexcept {
        if (v < min) min = v;
        if (v > max) max = v;
        sum += v;
        ++n;
    }
};

// VM only (the run since the last start).
uint32_t s_samples = 0, s_gaps = 0, s_overruns0 = 0, s_ingress0 = 0, s_edges0 = 0;
uint64_t s_prev_ts = 0;
Stat s_period, s_wake;

void on_pushed() noexcept {}   // the DMA completion interrupt itself ends repl_wait_step

abstractx::coro::Task<void> init_imu() noexcept {
    s_init_ok = co_await s_imu.init_async();
    s_init_done = true;
}

ax::IoEngine& engine() noexcept { return static_cast<ax::IoEngine&>(ax::get_target_io_processor()); }

} // namespace

extern "C" {

bool e906_imu_drdy_running(void) { return s_running; }

// @impl [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
bool e906_imu_drdy_start(void) {
    if (s_running) return false;
    s_init_done = false;
    s_init_ok = false;
    if (!abstractx::spawn(init_imu())) return false;
    const uint64_t deadline = hal::Timer::get_ticks() + kInitWaitMs * hal::Timer::TICKS_PER_MS;
    while (!s_init_done && hal::Timer::get_ticks() < deadline) {
        repl_wait_step(deadline);
    }
    if (!s_init_done || !s_init_ok) return false;   // a late init finishes on its own and is ignored

    s_channel = {};
    s_channel.channel_id = 0;
    s_channel.auto_mode = true;
    s_channel.trigger_mode = ax::TriggerMode::GpioEdge;
    s_channel.trigger_pin = ::hal::gpio_irq::kImuDrdy;
    s_channel.trigger_rising = true;
    s_channel.bus_type = ax::BusType::Spi;
    s_channel.bus_index = 1;
    s_channel.bus_speed_hz = 10'000'000;
    s_channel.cs_pin = 0;
    s_channel.tx_cmd[0] = static_cast<uint8_t>(imu::IcmRegs::TEMP_DATA1 | 0x80);
    s_channel.tx_len = 1;
    s_channel.rx_len = kSampleBytes;
    s_channel.tlp_channel = static_cast<uint8_t>(abstractx::Channel::Telemetry);
    s_channel.tlp_tag = imu::TLP_TAG_IMU;

    ax::IoProcessorSetup setup{};
    setup.channels = std::span<const ax::AutoChannelConfig>(&s_channel, 1);
    setup.ingress_rx_ring = &s_ring;
    setup.on_rx_pushed = etl::delegate<void()>::create<on_pushed>();

    auto& io = engine();
    s_ring.clear();
    s_samples = s_gaps = 0;
    s_prev_ts = 0;
    s_period = {};
    s_wake = {};
    s_overruns0 = io.auto_overruns();
    s_ingress0 = io.ingress_dropped();
    s_edges0 = ::hal::gpio_irq::edges(::hal::gpio_irq::kImuDrdy);
    ::hal::auto_dma::reset();
    io.configure(setup);
    io.start();
    s_running = true;
    return true;
}

// buf: [0:8] edge timestamp ns (LE), [8:22] raw sample. Caller checked len >= 22.
// @impl [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
bool e906_imu_drdy_read_into(uint8_t* buf, uint32_t timeout_ms) {
    if (!s_running) return false;
    abstractx::Tlp64 tlp{};
    bool got = s_ring.pop(tlp);
    if (!got && timeout_ms) {
        const uint64_t deadline = hal::Timer::get_ticks() + timeout_ms * hal::Timer::TICKS_PER_MS;
        while (!(got = s_ring.pop(tlp)) && hal::Timer::get_ticks() < deadline) {
            repl_wait_step(deadline);
        }
    }
    if (!got) return false;
    const uint64_t now = hal::Timer::get_time_ns();
    const uint64_t ts = tlp.wire.timestamp_ns;
    s_wake.add(static_cast<uint32_t>(now > ts ? now - ts : 0));
    if (s_prev_ts) {
        const uint64_t d = ts - s_prev_ts;
        s_period.add(static_cast<uint32_t>(d));
        if (d > kImuPeriodNs + kImuPeriodNs / 2) ++s_gaps;
    }
    s_prev_ts = ts;
    ++s_samples;
    for (size_t i = 0; i < 8; ++i) buf[i] = static_cast<uint8_t>(ts >> (8 * i));
    // The one copy on this path: the Python heap is in DSP RAM, out of the DMA's reach.
    const auto raw = tlp.payload();
    for (size_t i = 0; i < kSampleBytes; ++i) buf[8 + i] = i < raw.size() ? raw[i] : 0;
    return true;
}

// @impl [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
void e906_imu_drdy_stop(void) {
    if (!s_running) return;
    engine().stop();
    // A read started by the last edge may still be on SPI1: spi_xfer stays refused until it ends.
    const uint64_t deadline = hal::Timer::get_ticks() + kStopWaitMs * hal::Timer::TICKS_PER_MS;
    while (fc::hal::Spi1::is_busy() && hal::Timer::get_ticks() < deadline) {
        repl_wait_step(deadline);
    }
    s_running = fc::hal::Spi1::is_busy();
}

// Bench 'A' keys, in order; stats are min, avg, max. Returns the number of entries written.
// @impl [SPEC-E906-REPL-21] apps/e906_repl_app/SPECIFICATION.md
size_t e906_imu_drdy_stats(const char** keys, uint32_t* values, size_t max) {
    auto& io = engine();
    const auto st = ::hal::auto_dma::stats();
    size_t n = 0;
    auto put = [&](const char* k, uint32_t v) {
        if (n < max) { keys[n] = k; values[n] = v; ++n; }
    };
    auto put_stat = [&](const char* k_min, const char* k_avg, const char* k_max,
                        uint32_t mn, uint32_t mx, uint64_t sum, uint32_t cnt) {
        put(k_min, cnt ? mn : 0);
        put(k_avg, cnt ? static_cast<uint32_t>(sum / cnt) : 0);
        put(k_max, mx);
    };
    put("samples", s_samples);
    put("edges", ::hal::gpio_irq::edges(::hal::gpio_irq::kImuDrdy) - s_edges0);
    put("overruns", io.auto_overruns() - s_overruns0);
    put("busy_drops", st.busy_drops);
    put("dma_ok", st.completed);
    put("dma_failed", st.failed);
    put("ingress_dropped", io.ingress_dropped() - s_ingress0);
    put("seq_gaps", s_gaps);
    put_stat("period_ns_min", "period_ns_avg", "period_ns_max",
             s_period.min, s_period.max, s_period.sum, s_period.n);
    put_stat("edge_to_dma_cycles_min", "edge_to_dma_cycles_avg", "edge_to_dma_cycles_max",
             st.edge_to_start.min, st.edge_to_start.max, st.edge_to_start.sum, st.edge_to_start.n);
    put_stat("dma_cycles_min", "dma_cycles_avg", "dma_cycles_max",
             st.dma.min, st.dma.max, st.dma.sum, st.dma.n);
    put_stat("edge_to_wake_ns_min", "edge_to_wake_ns_avg", "edge_to_wake_ns_max",
             s_wake.min, s_wake.max, s_wake.sum, s_wake.n);
    return n;
}

} // extern "C"
