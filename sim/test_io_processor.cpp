/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: Target IIoProcessor Sensor HW Fusion API & Bus Lockout Verification
 */

#include "abstractx/hal/io_processor.hpp"
#include "spsc_tlp_ring.hpp"
#include "asp_tlp_msg.hpp"

#include <iostream>
#include <cassert>
#include <array>
#include <thread>
#include <chrono>

using namespace abstractx;
using namespace abstractx::hal;

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

    // 2. Instantiate SPSC TLP Rings
    SpscTlpRing<64> app_to_io_tx_ring{};
    SpscTlpRing<64> io_to_app_rx_ring{};

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
    Tlp64 spi_req = Tlp64::make_spi_transfer(0, 0, tx_bytes, 2, 42, 0, Channel::Control);
    assert(app_to_io_tx_ring.push(spi_req) && "Failed to push SPI request to TX ring");

    io_proc.step(20);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    io_proc.step(20);

    Tlp64 cpl{};
    assert(io_to_app_rx_ring.pop(cpl) && "Failed to receive completion TLP");
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
    Tlp64 rejected_req = Tlp64::make_spi_transfer(0, 0, tx_bytes, 2, 43, 0, Channel::Control);
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
    Tlp64 allowed_req = Tlp64::make_spi_transfer(0, 0, tx_bytes, 2, 44, 0, Channel::Control);
    assert(app_to_io_tx_ring.push(allowed_req) && "Failed to push SPI request");
    io_proc.step(20);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    io_proc.step(20);

    Tlp64 ok_cpl{};
    assert(io_to_app_rx_ring.pop(ok_cpl) && "Failed to receive completion");
    assert(ok_cpl.tag() == 44 && "Tag mismatch");
    assert(ok_cpl.wire.payload[0] == ASP_STATUS_OK && "Expected ASP_STATUS_OK after unlocking bus");
    std::cout << "  -> Manual read succeeded again once Auto-DMA was turned off." << std::endl;

    // 9. Clean Stop
    io_proc.stop();
    assert(!io_proc.is_running() && "Expected is_running() to be false after stop()");
    std::cout << "  -> IIoProcessor stopped cleanly." << std::endl;

    std::cout << "[SUCCESS] Sensor HW Fusion API & Bus Lockout verified 100% cleanly!" << std::endl;
    return 0;
}
