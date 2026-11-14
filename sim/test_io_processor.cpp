/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: Target IIoProcessor Sensor HW Fusion API & Bus Lockout Verification
 * @verifies [SPEC-ARCH-01] [SPEC-TLP-03] docs/DESIGN_SPECIFICATION.md
 * @verifies [SPEC-LINUX-SPI-01] targets/linux/SPECIFICATION.md#spec-linux-spi-01
 */

#include "abstractx/hal/io_processor.hpp"
#include "tlp_ring.hpp"
#include "asp_tlp_msg.hpp"

#include <iostream>
#include <cassert>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <chrono>

using namespace abstractx;
using namespace abstractx::hal;

namespace {

// A bridge request runs on the SpiWorker thread; its completion is pushed into the ingress ring
// from that thread and rings the back end's wake eventfd (notify_host()). pump() is step(20),
// which blocks in epoll on that eventfd, so this waits for the completion itself, not for a
// guessed time. The deadline only turns a lost completion into a failure instead of a hang.
// @verifies [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md (8: tests wait for an event)
constexpr auto kCplDeadline = std::chrono::seconds(5);

template <typename Pump>
bool wait_for_cpl(MpscTlpRing<>& ring, Tlp64& out, Pump&& pump) {
    const auto deadline = std::chrono::steady_clock::now() + kCplDeadline;
    while (!ring.pop(out)) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        pump();
    }
    return true;
}

} // namespace

int main() {
    std::cout << "[TEST] Starting Sensor HW Fusion API & Bus Lockout Verification..." << std::endl;

    // 1. Configure Sensor HW Fusion Channel in Setup Mode (auto_mode = false initially)
    AutoChannelConfig channels[1]{};
    channels[0].channel_id = 0;
    channels[0].auto_mode = false; // Initial Setup Mode: Auto-DMA is OFF
    channels[0].trigger_mode = TriggerMode::GpioEdge;
    channels[0].trigger_pin = 20; // Pin 3 / GP20
    channels[0].trigger_rising = true; // Positive / Rising Edge
    channels[0].bus_type = BusType::Spi;
    channels[0].bus_index = 0;
    channels[0].bus_speed_hz = 20'000'000;
    channels[0].tx_cmd[0] = 0x1F | 0x80; // Read command byte
    channels[0].tx_len = 1;
    channels[0].rx_len = 15;             // Read 15 bytes payload
    channels[0].tlp_channel = 0x02;      // DMA_Stream
    channels[0].tlp_tag = 1;

    // 2. Instantiate MPSC TLP Rings
    MpscTlpRing<> app_to_io_tx_ring{};
    MpscTlpRing<> io_to_app_rx_ring{};

    IoProcessorSetup setup{};
    setup.channels = std::span<const AutoChannelConfig>(channels, 1);
    setup.egress_tx_ring = &app_to_io_tx_ring;
    setup.ingress_rx_ring = &io_to_app_rx_ring;

    // 3. Obtain target IIoProcessor and configure
    IIoProcessor& io_proc = get_target_io_processor();
    bool cfg_ok = io_proc.configure(setup);
    assert(cfg_ok && "IIoProcessor configure failed");
    std::cout << "  -> Sensor HW Fusion configured in Setup Mode (Auto-DMA disarmed)." << std::endl;

    bool start_ok = io_proc.start();
    assert(start_ok && "IIoProcessor start failed");
    assert(io_proc.is_running() && "Expected is_running() to be true");

    // 4. In Setup Mode: Manual reads and writes MUST succeed!
    uint8_t tx_bytes[2] = { static_cast<uint8_t>(0x75 | 0x80), 0x00 }; // Read WHO_AM_I
    Tlp64 spi_req = Tlp64::make_spi_transfer(0, 0, tx_bytes, 2, ASP_SPI_FLAG_AUTO_CS, 42, Channel::Control);
    assert(app_to_io_tx_ring.push(spi_req) && "Failed to push SPI request to TX ring");


    Tlp64 cpl{};
    assert(wait_for_cpl(io_to_app_rx_ring, cpl, [&] { io_proc.step(20); }) && "Failed to receive completion TLP");
    assert(cpl.type() == TlpType::Completion && "Expected Completion TLP");
    assert(cpl.wire.payload[0] == ASP_STATUS_OK && "Expected ASP_STATUS_OK in Setup Mode");
    assert(cpl.tag() == 42 && "Tag mismatch");
    std::cout << "  -> In Setup Mode: Manual register read succeeded (Status=OK, Tag=42)." << std::endl;

    // 5. Turn ON Sensor HW Fusion (Auto-DMA Mode Enabled) via TLP Control Packet!
    Tlp64 enable_tlp = Tlp64::make_hw_fusion_control(0, true, 99);
    assert(app_to_io_tx_ring.push(enable_tlp) && "Failed to push enable control TLP");
    io_proc.step(20);

    Tlp64 ctrl_cpl{};
    assert(io_to_app_rx_ring.pop(ctrl_cpl) && "Failed to receive control completion TLP");
    assert(ctrl_cpl.tag() == 99 && "Control Tag mismatch");
    assert(ctrl_cpl.wire.payload[0] == ASP_STATUS_OK && "Expected OK for enable command");
    std::cout << "  -> Sensor HW Fusion Mode ENABLED via TLP control packet." << std::endl;

    // 6. CRITICAL HARDWARE INVARIANT: Once in Auto/DMA mode, manual reads/writes CANNOT happen!
    Tlp64 rejected_req = Tlp64::make_spi_transfer(0, 0, tx_bytes, 2, ASP_SPI_FLAG_AUTO_CS, 43, Channel::Control);
    assert(app_to_io_tx_ring.push(rejected_req) && "Failed to push SPI request");
    io_proc.step(20);

    Tlp64 locked_cpl{};
    assert(io_to_app_rx_ring.pop(locked_cpl) && "Failed to receive rejection completion");
    assert(locked_cpl.tag() == 43 && "Tag mismatch");
    assert(locked_cpl.wire.payload[0] == ASP_STATUS_BUS_LOCKED &&
           "Expected ASP_STATUS_BUS_LOCKED when manual read is attempted during Auto-DMA mode!");
    std::cout << "  -> Bus Lockout Verified: Manual read correctly REJECTED with ASP_STATUS_BUS_LOCKED!" << std::endl;

    // 7. Turn OFF Sensor HW Fusion (Disarm Auto-DMA) via TLP Control Packet!
    Tlp64 disable_tlp = Tlp64::make_hw_fusion_control(0, false, 100);
    assert(app_to_io_tx_ring.push(disable_tlp) && "Failed to push disable control TLP");
    io_proc.step(20);

    Tlp64 dis_cpl{};
    assert(io_to_app_rx_ring.pop(dis_cpl) && "Failed to receive disable completion");
    assert(dis_cpl.tag() == 100 && "Tag mismatch");
    assert(dis_cpl.wire.payload[0] == ASP_STATUS_OK && "Expected OK for disable command");
    std::cout << "  -> Sensor HW Fusion Mode DISABLED via TLP control packet (Bus Unlocked)." << std::endl;

    // 8. With Auto-DMA turned OFF: Manual reads and writes succeed once again!
    Tlp64 allowed_req = Tlp64::make_spi_transfer(0, 0, tx_bytes, 2, ASP_SPI_FLAG_AUTO_CS, 44, Channel::Control);
    assert(app_to_io_tx_ring.push(allowed_req) && "Failed to push SPI request");

    Tlp64 ok_cpl{};
    assert(wait_for_cpl(io_to_app_rx_ring, ok_cpl, [&] { io_proc.step(20); }) && "Failed to receive completion");
    assert(ok_cpl.tag() == 44 && "Tag mismatch");
    assert(ok_cpl.wire.payload[0] == ASP_STATUS_OK && "Expected ASP_STATUS_OK after unlocking bus");
    std::cout << "  -> Manual read succeeded again once Auto-DMA was turned off." << std::endl;

    // 9. Clean Stop
    io_proc.stop();
    assert(!io_proc.is_running() && "Expected is_running() to be false after stop()");
    std::cout << "  -> IIoProcessor stopped cleanly." << std::endl;

    // 10. Verify IIoProcessor::init(setup) and run_coroutine()
    bool reinit_ok = io_proc.init(setup);
    assert(reinit_ok && "IIoProcessor init(setup) failed");
    assert(io_proc.is_running() && "Expected is_running() to be true after init()");
    std::cout << "  -> IIoProcessor::init(setup) verified successfully." << std::endl;

    // Launch run_coroutine()
    coro::Task<void> io_coro = io_proc.run_coroutine();
    io_coro.resume();
    assert(!io_coro.done() && "Expected io_coro to remain active");

    // Queue a manual SPI request and process via Dispatcher
    Tlp64 coro_req = Tlp64::make_spi_transfer(0, 0, tx_bytes, 2, ASP_SPI_FLAG_AUTO_CS, 55, Channel::Control);
    assert(app_to_io_tx_ring.push(coro_req) && "Failed to push SPI request for coroutine test");

    // One Dispatcher pass runs run_coroutine(), which must take the request off the egress ring.
    Dispatcher::process();
    assert(app_to_io_tx_ring.empty() && "run_coroutine() did not take the request");

    // Then wait for the worker's completion (the coroutine keeps polling on each pass).
    Tlp64 coro_cpl{};
    assert(wait_for_cpl(io_to_app_rx_ring, coro_cpl, [&] { Dispatcher::process(); io_proc.step(20); }) &&
           "Failed to receive completion via coroutine");
    assert(coro_cpl.tag() == 55 && "Tag mismatch");
    assert(coro_cpl.wire.payload[0] == ASP_STATUS_OK && "Expected OK from coroutine execution");
    std::cout << "  -> IIoProcessor::run_coroutine() pumped and serviced TLP request cleanly." << std::endl;

    // 11. Verify pipelined concurrent bridge SPI requests [SPEC-LINUX-SPI-01]
    std::cout << "  -> Testing pipelined concurrent bridge SPI requests [SPEC-LINUX-SPI-01]..." << std::endl;
    constexpr size_t NUM_PIPELINED_REQS = 8;
    for (size_t i = 0; i < NUM_PIPELINED_REQS; ++i) {
        uint8_t tag = static_cast<uint8_t>(200 + i);
        Tlp64 pipe_req = Tlp64::make_spi_transfer(0, 0, tx_bytes, 2, ASP_SPI_FLAG_AUTO_CS, tag, Channel::Control);
        assert(app_to_io_tx_ring.push(pipe_req) && "Failed to push pipelined SPI request");
    }

    for (size_t i = 0; i < NUM_PIPELINED_REQS; ++i) {
        Tlp64 pipe_cpl{};
        assert(wait_for_cpl(io_to_app_rx_ring, pipe_cpl, [&] { io_proc.step(20); }) &&
               "Missing completion for pipelined SPI request");
        assert(pipe_cpl.type() == TlpType::Completion && "Expected Completion TLP");
        uint8_t expected_tag = static_cast<uint8_t>(200 + i);
        if (pipe_cpl.tag() != expected_tag || pipe_cpl.wire.payload[0] != ASP_STATUS_OK) {
            std::fprintf(stderr, "pipelined completion %zu: tag %u status 0x%02x (expected tag %u, OK)\n", i,
                         static_cast<unsigned>(pipe_cpl.tag()), static_cast<unsigned>(pipe_cpl.wire.payload[0]),
                         static_cast<unsigned>(expected_tag));
        }
        assert(pipe_cpl.tag() == expected_tag && "Pipelined tag mismatch");
        assert(pipe_cpl.wire.payload[0] == ASP_STATUS_OK && "Expected ASP_STATUS_OK for pipelined request");
        assert(pipe_cpl.wire.payload[sizeof(asp_tlp_spi_cpl_header_t) + 1] == 0x47 && "Expected ICM WHO_AM_I byte 0x47 in payload");
    }
    std::cout << "  -> Pipelined concurrent requests verified: all 8 completions correlated with matching tags." << std::endl;

    io_proc.stop();
    assert(!io_proc.is_running() && "Expected is_running() to be false after final stop()");

    // 12. stop() from another thread ends run() wherever it lands: before run() starts, or while
    // run() waits in poll_events() ([SPEC-ARCH-13] 7). No sleep: both orders happen over the rounds,
    // and a lost stop is a failure after the deadline, not a hang.
    // @verifies [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md (7: a stop() before run() is kept)
    constexpr int kStopRounds = 200;
    for (int round = 0; round < kStopRounds; ++round) {
        assert(io_proc.start() && "Expected restart to succeed");
        std::atomic<bool> returned{false};
        std::thread runner([&]() { io_proc.run(); returned.store(true, std::memory_order_release); });
        if (round % 2 == 0) {
            std::this_thread::yield();   // odd rounds stop at once, even rounds give run() a chance to start
        }
        io_proc.stop();
        const auto deadline = std::chrono::steady_clock::now() + kCplDeadline;
        while (!returned.load(std::memory_order_acquire)) {
            if (std::chrono::steady_clock::now() >= deadline) {
                std::fprintf(stderr, "run() did not return after stop() (round %d)\n", round);
                std::abort();
            }
            std::this_thread::yield();
        }
        runner.join();
        assert(!io_proc.is_running() && "Expected is_running() to be false after concurrent stop()");
    }
    std::cout << "  -> stop() ended run() in all " << kStopRounds << " rounds (before and during run())." << std::endl;

    std::cout << "[SUCCESS] Sensor HW Fusion API & Bus Lockout verified 100% cleanly!" << std::endl;
    return 0;
}
