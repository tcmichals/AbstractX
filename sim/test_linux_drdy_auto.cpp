/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: Linux GPIO-edge auto channel, edge -> SPI read -> TLP, run like bench 'A' on SITL.
 * @verifies [SPEC-LINUX-GPIO-01] targets/linux/SPECIFICATION.md
 */

#include "abstractx/hal/io_engine.hpp"
#include "abstractx/hal/io_processor.hpp"
#include "linux_io.hpp"
#include "tlp_ring.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>

using namespace abstractx;
using namespace abstractx::hal;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

namespace {

constexpr uint32_t kDrdyPin = 264;   // PI8 on the Cubie A5E main PIO chip
constexpr int kSamples = 1000;

uint64_t monotonic_ns() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

// Pump the engine like abstractx::step() until one sample is in the ring (or 1 s passes).
bool wait_sample(IoEngine& io, MpscTlpRing<>& ring, Tlp64& out) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        if (ring.pop(out)) return true;
        io.step(2);
    }
    return ring.pop(out);
}

} // namespace

int main() {
    AutoChannelConfig ch{};
    ch.channel_id = 0;
    ch.auto_mode = true;
    ch.trigger_mode = TriggerMode::GpioEdge;
    ch.trigger_pin = kDrdyPin;
    ch.trigger_rising = true;
    ch.bus_type = BusType::Spi;
    ch.bus_index = 1;
    ch.bus_speed_hz = 10'000'000;
    ch.cs_pin = 0;
    ch.tx_cmd[0] = 0x1D | 0x80;   // TEMP_DATA1 burst, as bench 'A'
    ch.tx_len = 1;
    ch.rx_len = 14;
    ch.tlp_channel = static_cast<uint8_t>(Channel::Telemetry);
    ch.tlp_tag = 1;

    MpscTlpRing<> ring{};
    IoProcessorSetup setup{};
    setup.channels = std::span<const AutoChannelConfig>(&ch, 1);
    setup.ingress_rx_ring = &ring;

    target::set_linux_gpiochip("/dev/abstractx-no-such-gpiochip");   // force SITL on any machine
    auto& io = static_cast<IoEngine&>(get_target_io_processor());
    CHECK(io.configure(setup));
    CHECK(io.start());
    CHECK(target::linux_trigger_simulated(kDrdyPin));
    CHECK(!target::linux_simulate_edge(kDrdyPin + 1));   // no trigger on another pin

    const uint32_t overruns0 = io.auto_overruns();
    const uint32_t dropped0 = io.ingress_dropped();
    uint64_t prev_ts = 0;
    for (int i = 0; i < kSamples; ++i) {
        const uint64_t before = monotonic_ns();
        CHECK(target::linux_simulate_edge(kDrdyPin));
        Tlp64 tlp{};
        CHECK(wait_sample(io, ring, tlp));
        const uint64_t ts = tlp.wire.timestamp_ns;
        // The edge time, on CLOCK_MONOTONIC: after the edge was raised, before it was consumed.
        CHECK(ts >= before);
        CHECK(ts <= monotonic_ns());
        CHECK(ts > prev_ts);
        prev_ts = ts;
        CHECK(tlp.tag() == 1);
    }
    // Steady state: one sample per edge, nothing dropped, nothing left over.
    CHECK(io.auto_overruns() == overruns0);
    CHECK(io.ingress_dropped() == dropped0);
    Tlp64 extra{};
    io.step(5);
    CHECK(!ring.pop(extra));

    // Two edges before one pump: each is its own trigger (the second may overrun, never merge).
    CHECK(target::linux_simulate_edge(kDrdyPin));
    CHECK(target::linux_simulate_edge(kDrdyPin));
    int got = 0;
    Tlp64 t{};
    for (int k = 0; k < 2 && wait_sample(io, ring, t); ++k) ++got;
    CHECK(got + static_cast<int>(io.auto_overruns() - overruns0) == 2);

    io.stop();
    std::printf("[PASS] test_linux_drdy_auto: %d samples, overruns %u\n", kSamples,
                io.auto_overruns() - overruns0);
    return 0;
}
