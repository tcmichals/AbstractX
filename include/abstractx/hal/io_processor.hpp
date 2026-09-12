/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX HAL: Universal Target I/O Processor Interface & Auto-DMA Engine
 * -------------------------------------------------------------------------
 * Authoritative interface for the target-level I/O Processor (ioProcessor).
 * Unifies FPGA, Real-Time Coprocessor, and Linux architectures.
 * Provides setup, start, stop, and autonomous Auto-Mode polling & forwarding.
 */

#ifndef ABSTRACTX_HAL_IO_PROCESSOR_HPP
#define ABSTRACTX_HAL_IO_PROCESSOR_HPP

#include <cstdint>
#include <cstddef>
#include <span>
#include <etl/delegate.h>

#include "asp_tlp64.hpp"
#include "spsc_tlp_ring.hpp"
#include "asp_coro.hpp"
#include "abstractx/domain_dispatcher.hpp"

namespace abstractx::hal {

// =============================================================================
// AUTONOMOUS CHANNEL CONFIGURATION (FPGA / MCU / LINUX REGISTER MAP)
// =============================================================================

enum class TriggerMode : uint8_t {
    None     = 0,
    GpioEdge = 1, // Physical hardware interrupt pin (e.g. DRDY, strobe, sync pulse)
    Timer    = 2  // Periodic hardware timer interval
};

enum class BusType : uint8_t {
    Spi  = 0,
    I2c  = 1,
    Uart = 2
};

/*
 * Generic Auto-DMA Channel Configuration:
 * Maps 1:1 to FPGA hardware trigger registers or target-level I/O processor setup.
 * When an ISR/trigger fires, the engine executes the polling transfer ASAP
 * and forwards the resulting data as a timestamped TLP into the completion ring.
 */
struct AutoChannelConfig {
    uint8_t     channel_id{0};           // Hardware channel index (0, 1, 2...)
    bool        auto_mode{true};         // Auto Mode: true = read ASAP on trigger & forward TLP

    // 1. Trigger Specification
    TriggerMode trigger_mode{TriggerMode::GpioEdge};
    uint32_t    trigger_pin{0};          // Physical ISR pin number (e.g. Pin 3 / GP20)
    bool        trigger_rising{true};    // Rising vs Falling edge
    uint32_t    timer_period_us{0};      // Periodic timer interval in microseconds (if Timer mode)

    // 2. Bus Polling / Transfer Specification
    BusType     bus_type{BusType::Spi};
    uint8_t     bus_index{0};            // Physical bus index (SPI0, SPI1, /dev/spidev0.0)
    uint32_t    bus_speed_hz{20'000'000};
    uint32_t    cs_pin{0};               // Chip select pin (if applicable)

    uint8_t     tx_cmd[8]{};             // Outgoing command bytes to initiate read (e.g. register read addr)
    uint8_t     tx_len{1};               // Length of command bytes
    uint16_t    rx_len{15};              // Number of data bytes to clock in / read ASAP

    // 3. Egress TLP Wire Specification
    uint8_t     tlp_channel{0x02};       // TLP Channel (0x02 = DMA_Stream)
    uint8_t     tlp_tag{1};              // Correlation tag
};

/*
 * Complete I/O Setup passed from Application to Target ioProcessor
 */
struct IoProcessorSetup {
    std::span<const AutoChannelConfig> channels{};
    SpscTlpRing<64>* egress_tx_ring{nullptr};  // Application -> ioProcessor (Manual requests / IOCTL)
    SpscTlpRing<64>* ingress_rx_ring{nullptr}; // ioProcessor -> Application (Completions & Auto-Forwarded Stream TLPs)

    // Optional notification hooks
    etl::delegate<void()> on_tx_pushed{};
    etl::delegate<void()> on_rx_pushed{};
};

// =============================================================================
// ABSTRACT I/O PROCESSOR INTERFACE
// =============================================================================

class IIoProcessor {
public:
    virtual ~IIoProcessor() = default;

    /*
     * Configure: Sets up the hardware channels, triggers, and SPSC rings.
     */
    virtual bool configure(const IoProcessorSetup& setup) = 0;

    /*
     * Start: Arms the hardware triggers / ISRs and begins autonomous execution.
     */
    virtual bool start() = 0;

    /*
     * Init: Convenience initialization that configures and starts the I/O processor.
     */
    virtual bool init(const IoProcessorSetup& setup) {
        return configure(setup) && start();
    }

    /*
     * Stop: Disarms triggers and shuts down cleanly.
     */
    virtual void stop() = 0;

    /*
     * Step: Non-blocking single step of the event/reactor loop.
     * Essential for unit tests, SITL simulation, and deterministic verification.
     */
    virtual int step(int timeout_ms = 0) = 0;

    /*
     * Run: Blocking execution loop for dedicated worker thread or Core 0.
     */
    virtual void run() = 0;

    /*
     * Run Coroutine: Cooperative I/O reactor execution task.
     * Allows the I/O processor reactor to execute cooperatively inside the main coroutine loop.
     */
    virtual coro::Task<void> run_coroutine() {
        while (is_running()) {
            step(0);
            co_await yield_to_dispatcher();
        }
    }

    /*
     * Operational status.
     */
    virtual bool is_running() const noexcept = 0;

    /*
     * Dynamic Auto-Mode Control: Enables or disables autonomous read & forward per channel.
     */
    virtual bool set_auto_mode(uint8_t channel_id, bool enable) = 0;
};

/*
 * Target Factory: Returns the concrete target-specific IIoProcessor instance.
 */
IIoProcessor& get_target_io_processor() noexcept;

} // namespace abstractx::hal

#endif // ABSTRACTX_HAL_IO_PROCESSOR_HPP
