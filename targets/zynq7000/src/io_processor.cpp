/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Zynq-7000 Target: I/O Processor Implementation
 * ---------------------------------------------------------
 * Bridges Linux user-space flight loops to the Artix-7 PL switch fabric via /dev/uio0.
 * Implements IIoProcessor with zero heap allocations and non-blocking TLP drainage.
 *
 * @impl [SPEC-ZYNQ-01] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
 * @impl [SPEC-ZYNQ-02] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
 * @impl [SPEC-ZYNQ-03] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-03
 * @impl [SPEC-ZYNQ-06] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-06
 */

#include "abstractx/hal/io_processor.hpp"
#include "abstractx/target/zynq_uio.hpp"

#include <atomic>
#include <array>
#include <poll.h>
#include <unistd.h>

namespace abstractx::target {

class Zynq7000IoProcessor final : public hal::IIoProcessor {
public:
    Zynq7000IoProcessor() noexcept = default;

    ~Zynq7000IoProcessor() override {
        stop();
    }

    bool configure(const hal::IoProcessorSetup& setup) override {
        setup_ = setup;
        return true;
    }

    bool start() override {
        if (running_.load(std::memory_order_acquire)) {
            return true;
        }

        if (!bridge_.open_uio("/dev/uio0")) {
            return false;
        }

        running_.store(true, std::memory_order_release);
        return true;
    }

    void stop() override {
        running_.store(false, std::memory_order_release);
        bridge_.close_uio();
    }

    int step(int timeout_ms = 0) override {
        if (!running_.load(std::memory_order_acquire) || !bridge_.is_open()) {
            return 0;
        }

        int processed = 0;

        // 1. Flush outgoing commands/TLPs from Application -> FPGA PL Ingress FIFO
        if (setup_.egress_tx_ring) {
            Tlp64 tx_tlp{};
            while (bridge_.ingress_free_slots() > 0 && setup_.egress_tx_ring->pop(tx_tlp)) {
                if (bridge_.write_tlp(tx_tlp)) {
                    ++processed;
                }
            }
        }

        // 2. Drain incoming telemetry/TLPs from FPGA PL Egress FIFO -> Ingress RX Ring
        if (setup_.ingress_rx_ring) {
            Tlp64 rx_tlp{};
            while (bridge_.egress_count() > 0) {
                if (bridge_.read_tlp(rx_tlp)) {
                    if (setup_.ingress_rx_ring->push(rx_tlp)) {
                        ++processed;
                        if (setup_.on_rx_pushed) {
                            setup_.on_rx_pushed();
                        }
                    } else {
                        // Ring buffer full
                        break;
                    }
                } else {
                    break;
                }
            }
        }

        // 3. If timeout requested and nothing was immediately processed, wait on UIO eventfd
        if (processed == 0 && timeout_ms > 0 && bridge_.fd() >= 0) {
            struct pollfd pfd{};
            pfd.fd = bridge_.fd();
            pfd.events = POLLIN;

            const int ret = ::poll(&pfd, 1, timeout_ms);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                uint32_t irq_count = 0;
                [[maybe_unused]] auto bytes = ::read(bridge_.fd(), &irq_count, sizeof(irq_count));
                bridge_.ack_interrupt();

                // Re-drain now that interrupt asserted
                if (setup_.ingress_rx_ring) {
                    Tlp64 rx_tlp{};
                    while (bridge_.egress_count() > 0 && bridge_.read_tlp(rx_tlp)) {
                        if (setup_.ingress_rx_ring->push(rx_tlp)) {
                            ++processed;
                            if (setup_.on_rx_pushed) {
                                setup_.on_rx_pushed();
                            }
                        } else {
                            break;
                        }
                    }
                }
            }
        }

        return processed;
    }

    void run() override {
        while (running_.load(std::memory_order_relaxed)) {
            step(10);
        }
    }

    [[nodiscard]] bool is_running() const noexcept override {
        return running_.load(std::memory_order_acquire);
    }

    bool set_auto_mode(uint8_t /*channel_id*/, bool /*enable*/) override {
        // Hardware auto-DMA is configured in bitstream / sys_regs
        return true;
    }

private:
    ZynqUioBridge bridge_{};
    hal::IoProcessorSetup setup_{};
    std::atomic<bool> running_{false};
};

// Global singleton instance for target factory
static Zynq7000IoProcessor s_zynq_io_processor;

} // namespace abstractx::target

namespace abstractx::hal {

IIoProcessor& get_target_io_processor() noexcept {
    return target::s_zynq_io_processor;
}

} // namespace abstractx::hal
