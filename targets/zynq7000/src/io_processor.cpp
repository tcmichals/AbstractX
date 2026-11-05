/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Zynq-7000 Target: I/O Processor Implementation
 * ---------------------------------------------------------
 * Bridges Linux user-space flight loops to the Artix-7 PL switch fabric via /dev/uio0
 * and zero-copy SPSC DMA coherent ring buffers in PS DDR memory.
 *
 * @impl [SPEC-ZYNQ-01] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
 * @impl [SPEC-ZYNQ-02] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
 * @impl [SPEC-ZYNQ-03] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-03
 * @impl [SPEC-ZYNQ-06] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-06
 */

#include "abstractx/hal/io_processor.hpp"
#include "abstractx/interrupt_lock.hpp"
#include "abstractx/target/zynq_uio.hpp"

#include <atomic>
#include <array>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace abstractx::target {

class Zynq7000IoProcessor final : public hal::IIoProcessor {
public:
    static constexpr uint32_t DEFAULT_RING_CAPACITY = 256;

    Zynq7000IoProcessor() noexcept : wake_fd_(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC)) {}

    ~Zynq7000IoProcessor() override {
        stop();
        if (wake_fd_ >= 0) {
            ::close(wake_fd_);
            wake_fd_ = -1;
        }
    }

    bool configure(const hal::IoProcessorSetup& setup) override {
        InterruptGuard lock;
        setup_ = setup;
        return true;
    }

    bool start() override {
        InterruptGuard lock;
        if (running_.load(std::memory_order_acquire)) {
            return true;
        }

        const char* uio_dev = "/dev/uio0";
        if (const char* env_dev = ::getenv("ABSTRACTX_UIO_DEV")) {
            uio_dev = env_dev;
        }

        if (!bridge_.open_uio(uio_dev)) {
            return false;
        }

        running_.store(true, std::memory_order_release);
        return true;
    }

    void stop() override {
        running_.store(false, std::memory_order_release);
        if (wake_fd_ >= 0) {
            const uint64_t val = 1;
            [[maybe_unused]] auto n = ::write(wake_fd_, &val, sizeof(val));
        }
        InterruptGuard lock;
        bridge_.close_uio();
    }

    int step(int timeout_ms = 0) override {
        int processed = 0;
        int uio_fd = -1;
        {
            InterruptGuard lock;
            if (!running_.load(std::memory_order_acquire) || !bridge_.is_open()) {
                return 0;
            }

            // 1. Flush outgoing commands/TLPs from Application -> FPGA Ingress BRAM
            // @impl [SPEC-ZYNQ-01] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
            if (setup_.egress_tx_ring) {
                Tlp64 tx_tlp{};
                uint32_t free_slots = bridge_.ingress_free();
                while (free_slots > 0 && setup_.egress_tx_ring->pop(tx_tlp)) {
                    if (bridge_.write_tlp(tx_tlp)) {
                        ++processed;
                        --free_slots;
                    }
                }
            }

            // 2. Drain incoming telemetry/TLPs from FPGA Egress BRAM -> Ingress RX Ring
            // @impl [SPEC-ZYNQ-01] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
            processed += drain_egress();

            // Re-arm the interrupt once a burst taken earlier has been drained (polled or woken).
            // @impl [SPEC-ZYNQ-02] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
            bridge_.rearm_if_drained();
            uio_fd = bridge_.fd();
        }

        // 3. If timeout requested and nothing was immediately processed, wait on UIO + wake eventfd
        // @impl [SPEC-ZYNQ-02] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
        if (processed == 0 && timeout_ms > 0 && uio_fd >= 0) {
            struct pollfd pfds[2]{};
            pfds[0].fd = uio_fd;
            pfds[0].events = POLLIN;
            pfds[1].fd = wake_fd_;
            pfds[1].events = POLLIN;
            const nfds_t nfds = (wake_fd_ >= 0) ? 2 : 1;

            const int ret = ::poll(pfds, nfds, timeout_ms);
            if (ret > 0) {
                if (nfds == 2 && (pfds[1].revents & POLLIN)) {
                    uint64_t val = 0;
                    [[maybe_unused]] auto n = ::read(wake_fd_, &val, sizeof(val));
                }
                if (pfds[0].revents & POLLIN) {
                    InterruptGuard lock;
                    if (!running_.load(std::memory_order_acquire) || !bridge_.is_open()) {
                        return processed;
                    }
                    uint32_t irq_count = 0;
                    [[maybe_unused]] auto bytes = ::read(bridge_.fd(), &irq_count, sizeof(irq_count));
                    bridge_.note_interrupt_taken();

                    // Drain incoming packets on interrupt wake
                    processed += drain_egress();

                    // Clear and re-arm if drained; otherwise a later step() re-arms it
                    bridge_.rearm_if_drained();
                }
            }
        }

        return processed;
    }

    void run() override {
        while (running_.load(std::memory_order_acquire)) {
            step(10);
        }
    }

    [[nodiscard]] bool is_running() const noexcept override {
        return running_.load(std::memory_order_acquire);
    }

    bool set_auto_mode(uint8_t /*channel_id*/, bool /*enable*/) override {
        return true;
    }

    [[nodiscard]] uint32_t rx_ring_full() const noexcept { return rx_ring_full_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint32_t rx_dropped() const noexcept { return rx_dropped_.load(std::memory_order_relaxed); }

private:
    // FPGA egress -> ingress_rx_ring. Reads a packet only when the ring has room; a full ring
    // leaves the rest in the FPGA FIFO (backpressure). Caller holds the InterruptGuard.
    // @impl [SPEC-ZYNQ-02] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
    int drain_egress() noexcept {
        auto* ring = setup_.ingress_rx_ring;
        if (!ring) return 0;
        int moved = 0;
        Tlp64 rx_tlp{};
        for (uint32_t count = bridge_.egress_count(); count > 0; --count) {
            if (ring->size() >= ring->capacity()) {
                rx_ring_full_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            if (!bridge_.read_tlp(rx_tlp)) break;
            if (!ring->push(rx_tlp)) {  // another producer took the slot after the check
                rx_dropped_.fetch_add(1, std::memory_order_relaxed);
                break;
            }
            ++moved;
            if (setup_.on_rx_pushed) setup_.on_rx_pushed();
        }
        return moved;
    }

    std::atomic<uint32_t> rx_ring_full_{0};  // shared: step() writes, any thread reads; atomic
    std::atomic<uint32_t> rx_dropped_{0};    // shared: step() writes, any thread reads; atomic
    ZynqUioBridge bridge_{};
    hal::IoProcessorSetup setup_{};
    int wake_fd_{-1};
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
