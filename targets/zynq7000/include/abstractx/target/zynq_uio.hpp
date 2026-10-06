/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Zynq-7000 UIO Hardware Interface
 * -------------------------------------------
 * Direct memory-mapped access to the AbstractX FPGA fabric on M_AXI_GP0 (0x4000_0000).
 * Operates over /dev/uio0 with zero heap allocation and zero blocking spinloops.
 *
 * @impl [SPEC-ZYNQ-01] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
 * @impl [SPEC-ZYNQ-02] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
 * @impl [SPEC-ZYNQ-06] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-06
 */

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <poll.h>

#include "asp_tlp64.hpp"

namespace abstractx::target {

class ZynqUioBridge {
public:
    static constexpr size_t MAP_SIZE = 0x10000; // 64 KB AXI-Lite address window

    // Register Byte Offsets matching asp_axi_dma.sv [SPEC-ZYNQ-01]
    static constexpr uint32_t REG_CONTROL      = 0x00;
    static constexpr uint32_t REG_STATUS       = 0x04;
    static constexpr uint32_t REG_IRQ_STATUS   = 0x08;
    static constexpr uint32_t REG_IRQ_ENABLE   = 0x0C;
    static constexpr uint32_t REG_RX_BASE      = 0x10;
    static constexpr uint32_t REG_RX_CAPACITY  = 0x14;
    static constexpr uint32_t REG_RX_HEAD      = 0x18;
    static constexpr uint32_t REG_RX_TAIL      = 0x1C;
    static constexpr uint32_t REG_TX_BASE      = 0x20;
    static constexpr uint32_t REG_TX_CAPACITY  = 0x24;
    static constexpr uint32_t REG_TX_HEAD      = 0x28;
    static constexpr uint32_t REG_TX_TAIL      = 0x2C;
    static constexpr uint32_t REG_TX_DOORBELL  = 0x30;
    static constexpr uint32_t REG_HARDWARE_ID  = 0x40;

    static constexpr uint32_t HARDWARE_MAGIC   = 0x41535036; // "ASP6"

    ZynqUioBridge() noexcept = default;

    ~ZynqUioBridge() noexcept {
        close_uio();
    }

    // Non-copyable, non-movable hardware accessor
    ZynqUioBridge(const ZynqUioBridge&) = delete;
    ZynqUioBridge& operator=(const ZynqUioBridge&) = delete;

    bool open_uio(const char* dev_path = "/dev/uio0") noexcept {
        if (fd_ >= 0) return true;

        fd_ = ::open(dev_path, O_RDWR | O_SYNC | O_NONBLOCK);
        if (fd_ < 0) {
            return false;
        }

        void* ptr = ::mmap(nullptr, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
        if (ptr == MAP_FAILED) {
            ::close(fd_);
            fd_ = -1;
            return false;
        }

        base_ = reinterpret_cast<volatile uint32_t*>(ptr);

        // Verify hardware magic identification
        if (read_reg(REG_HARDWARE_ID) != HARDWARE_MAGIC) {
            ::munmap(ptr, MAP_SIZE);
            ::close(fd_);
            base_ = nullptr;
            fd_ = -1;
            return false;
        }

        // Enable DMA bridge and egress interrupts
        write_reg(REG_CONTROL, 0x01);    // Enable DMA
        write_reg(REG_IRQ_ENABLE, 0x01); // Unmask IRQ_F2P[0]

        // Re-arm Linux UIO interrupt eventfd
        unmask_interrupt();

        return true;
    }

    void close_uio() noexcept {
        if (base_ != nullptr) {
            // Mask interrupts and disable DMA
            write_reg(REG_IRQ_ENABLE, 0x00);
            write_reg(REG_CONTROL, 0x00);
            ::munmap(const_cast<uint32_t*>(base_), MAP_SIZE);
            base_ = nullptr;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    [[nodiscard]] bool is_open() const noexcept {
        return base_ != nullptr && fd_ >= 0;
    }

    [[nodiscard]] int fd() const noexcept {
        return fd_;
    }

    [[nodiscard]] uint32_t read_reg(uint32_t offset) const noexcept {
        if (!base_) return 0;
        return base_[offset / sizeof(uint32_t)];
    }

    void write_reg(uint32_t offset, uint32_t val) noexcept {
        if (!base_) return;
        base_[offset / sizeof(uint32_t)] = val;
    }

    // Configure SPSC Coherent Ring Base Physical Addresses & Capacity
    void setup_dma_rings(uint32_t rx_phys_addr, uint32_t rx_cap,
                         uint32_t tx_phys_addr, uint32_t tx_cap) noexcept {
        write_reg(REG_RX_BASE, rx_phys_addr);
        write_reg(REG_RX_CAPACITY, rx_cap);
        write_reg(REG_RX_HEAD, 0);

        write_reg(REG_TX_BASE, tx_phys_addr);
        write_reg(REG_TX_CAPACITY, tx_cap);
        write_reg(REG_TX_TAIL, 0);
    }

    [[nodiscard]] uint32_t rx_head() const noexcept { return read_reg(REG_RX_HEAD); }
    [[nodiscard]] uint32_t rx_tail() const noexcept { return read_reg(REG_RX_TAIL); }
    void set_rx_head(uint32_t head) noexcept { write_reg(REG_RX_HEAD, head); }

    [[nodiscard]] uint32_t tx_head() const noexcept { return read_reg(REG_TX_HEAD); }
    [[nodiscard]] uint32_t tx_tail() const noexcept { return read_reg(REG_TX_TAIL); }
    void set_tx_tail(uint32_t tail) noexcept { write_reg(REG_TX_TAIL, tail); }

    void signal_tx_doorbell() noexcept {
        write_reg(REG_TX_DOORBELL, 0x01);
    }

    // Unmask UIO interrupt in Linux kernel so next IRQ_F2P will trigger event
    // @impl [SPEC-ZYNQ-02] hw/zynq7000/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
    void unmask_interrupt() noexcept {
        if (fd_ >= 0) {
            const uint32_t unmask = 1;
            [[maybe_unused]] auto res = ::write(fd_, &unmask, sizeof(unmask));
        }
    }

    // Clear pending IRQ in FPGA register and unmask UIO event
    void ack_interrupt() noexcept {
        write_reg(REG_IRQ_STATUS, 0x01);
        unmask_interrupt();
    }

private:
    int fd_{-1};
    volatile uint32_t* base_{nullptr};
};

} // namespace abstractx::target
