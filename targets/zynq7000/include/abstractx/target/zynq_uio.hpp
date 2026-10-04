/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Zynq-7000 UIO Hardware Interface
 * -------------------------------------------
 * Direct memory-mapped access to the AbstractX FPGA fabric on M_AXI_GP0 (0x4000_0000).
 * Operates over /dev/uio0 with zero heap allocation and zero blocking spinloops.
 *
 * @impl [SPEC-ZYNQ-01] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
 * @impl [SPEC-ZYNQ-02] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
 * @impl [SPEC-ZYNQ-06] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-06
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

    // Register Byte Offsets matching asp_axi_lite_bridge.sv [SPEC-ZYNQ-01]
    static constexpr uint32_t REG_CONTROL      = 0x00;
    static constexpr uint32_t REG_STATUS       = 0x04;
    static constexpr uint32_t REG_IRQ_STATUS   = 0x08;
    static constexpr uint32_t REG_IRQ_ENABLE   = 0x0C;
    static constexpr uint32_t REG_TLP_IN_PORT  = 0x10;
    static constexpr uint32_t REG_TLP_OUT_PORT = 0x14;
    static constexpr uint32_t REG_EGR_COUNT    = 0x18;
    static constexpr uint32_t REG_ING_FREE     = 0x1C;
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

        // Enable bridge and egress interrupts
        write_reg(REG_CONTROL, 0x01);    // Enable
        write_reg(REG_IRQ_ENABLE, 0x01); // Unmask IRQ_F2P[0]

        // Re-arm Linux UIO interrupt eventfd
        unmask_interrupt();

        return true;
    }

    void close_uio() noexcept {
        if (base_ != nullptr) {
            // Mask interrupts
            write_reg(REG_IRQ_ENABLE, 0x00);
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

    [[nodiscard]] uint32_t egress_count() const noexcept {
        return read_reg(REG_EGR_COUNT);
    }

    [[nodiscard]] uint32_t ingress_free_slots() const noexcept {
        return read_reg(REG_ING_FREE);
    }

    // Write 64-byte TLP into FPGA Ingress FIFO (16 DWORDs to 0x10)
    // @impl [SPEC-ZYNQ-01] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
    bool write_tlp(const Tlp64& tlp) noexcept {
        if (!base_ || ingress_free_slots() == 0) {
            return false;
        }

        const uint32_t* dwords = reinterpret_cast<const uint32_t*>(&tlp);
        for (size_t i = 0; i < 16; ++i) {
            base_[REG_TLP_IN_PORT / sizeof(uint32_t)] = dwords[i];
        }
        return true;
    }

    // Read 64-byte TLP from FPGA Egress FIFO (16 DWORDs from 0x14)
    // @impl [SPEC-ZYNQ-01] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-01
    bool read_tlp(Tlp64& tlp) noexcept {
        if (!base_ || egress_count() == 0) {
            return false;
        }

        uint32_t* dwords = reinterpret_cast<uint32_t*>(&tlp);
        for (size_t i = 0; i < 16; ++i) {
            dwords[i] = base_[REG_TLP_OUT_PORT / sizeof(uint32_t)];
        }
        return true;
    }

    // Unmask UIO interrupt in Linux kernel so next IRQ_F2P will trigger event
    // @impl [SPEC-ZYNQ-02] hw/qmtech_zynq7020/SPECIFICATION.md#spec-zynq-02
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
