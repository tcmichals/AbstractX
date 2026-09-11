/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E907 Sunxi DMA Controller Driver
 * -------------------------------------------------------------
 * Strict 100% ISR and DMA Invariant:
 * - Direct LLI descriptor hardware execution
 * - Completion signaled via PLIC IRQ 64 (irq_id::DMA_E907)
 * - ZERO POLLING / ZERO BUSY-WAIT LOOPS
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <etl/delegate.h>

namespace hal {

/* ========================================================================= */
/* Sunxi DMA Hardware Register Offsets (Base: 0x03002000 / 0x07121000)       */
/* ========================================================================= */
#ifndef SUNXI_DMA_BASE
#define SUNXI_DMA_BASE              0x03002000U
#endif

#define DMA_IRQ_EN_REG0             (*(volatile uint32_t *)(SUNXI_DMA_BASE + 0x0000))
#define DMA_IRQ_EN_REG1             (*(volatile uint32_t *)(SUNXI_DMA_BASE + 0x0004))
#define DMA_IRQ_STAT_REG0           (*(volatile uint32_t *)(SUNXI_DMA_BASE + 0x0010))
#define DMA_IRQ_STAT_REG1           (*(volatile uint32_t *)(SUNXI_DMA_BASE + 0x0014))
#define DMA_STAT_REG                (*(volatile uint32_t *)(SUNXI_DMA_BASE + 0x0030))
#define DMA_GATE_REG                (*(volatile uint32_t *)(SUNXI_DMA_BASE + 0x0028))

#define DMA_CHAN_BASE(ch)           (SUNXI_DMA_BASE + 0x0100 + ((ch) * 0x0040))
#define DMA_CHAN_ENABLE_REG(ch)     (*(volatile uint32_t *)(DMA_CHAN_BASE(ch) + 0x0000))
#define DMA_CHAN_PAUSE_REG(ch)      (*(volatile uint32_t *)(DMA_CHAN_BASE(ch) + 0x0004))
#define DMA_CHAN_LLI_ADDR_REG(ch)   (*(volatile uint32_t *)(DMA_CHAN_BASE(ch) + 0x0008))
#define DMA_CHAN_CUR_CFG_REG(ch)    (*(volatile uint32_t *)(DMA_CHAN_BASE(ch) + 0x000C))
#define DMA_CHAN_CUR_SRC_REG(ch)    (*(volatile uint32_t *)(DMA_CHAN_BASE(ch) + 0x0010))
#define DMA_CHAN_CUR_DST_REG(ch)    (*(volatile uint32_t *)(DMA_CHAN_BASE(ch) + 0x0014))
#define DMA_CHAN_CUR_CNT_REG(ch)    (*(volatile uint32_t *)(DMA_CHAN_BASE(ch) + 0x0018))
#define DMA_CHAN_CUR_PARA_REG(ch)   (*(volatile uint32_t *)(DMA_CHAN_BASE(ch) + 0x001C))

/* DRQ Endpoint Ports (Allwinner A523 / T527) */
constexpr uint8_t DRQ_SDRAM = 1;
constexpr uint8_t DRQ_UART0 = 14;
constexpr uint8_t DRQ_UART2 = 16;
constexpr uint8_t DRQ_SPI0  = 22;

constexpr uint32_t LLI_LAST_ITEM = 0xFFFFF800U;

/* Burst lengths */
constexpr uint8_t DMA_BURST_1  = 0;
constexpr uint8_t DMA_BURST_4  = 1;
constexpr uint8_t DMA_BURST_8  = 2;
constexpr uint8_t DMA_BURST_16 = 3;

/* Bus widths */
constexpr uint8_t DMA_WIDTH_8  = 0;
constexpr uint8_t DMA_WIDTH_16 = 1;
constexpr uint8_t DMA_WIDTH_32 = 2;

/* Addressing modes */
constexpr uint8_t DMA_MODE_LINEAR = 0; // Incrementing memory address
constexpr uint8_t DMA_MODE_IO     = 1; // Fixed FIFO MMIO register address

/*
 * Hardware representation of Sunxi Linked-List Item (LLI)
 */
struct alignas(4) DmaLli {
    uint32_t cfg;        // Transfer configuration
    uint32_t src;        // Source physical address
    uint32_t dst;        // Destination physical address
    uint32_t len;        // Transfer byte count
    uint32_t para;       // Clock wait parameters (default: 8)
    uint32_t p_lli_next; // Physical address of next LLI (or LLI_LAST_ITEM)
};

class DmaController {
public:
    static constexpr size_t NUM_CHANNELS = 16;
    using CompletionCallback = etl::delegate<void(uint8_t channel, bool success)>;

    static void init() noexcept;

    // Allocate dedicated hardware channels (e.g. Ch 0 for SPI TX, Ch 1 for SPI RX)
    static int allocate_channel() noexcept;
    static void release_channel(uint8_t channel) noexcept;

    // Build configuration word matching Allwinner H6/A100/A523 DMA controller
    static constexpr uint32_t build_cfg(
        uint8_t src_drq, uint8_t src_burst, uint8_t src_width, uint8_t src_mode,
        uint8_t dst_drq, uint8_t dst_burst, uint8_t dst_width, uint8_t dst_mode
    ) noexcept {
        return (static_cast<uint32_t>(src_drq & 0x3F)) |
               (static_cast<uint32_t>(src_burst & 0x03) << 6) |
               (static_cast<uint32_t>(src_mode & 0x01) << 8) |
               (static_cast<uint32_t>(src_width & 0x03) << 9) |
               (static_cast<uint32_t>(dst_drq & 0x3F) << 16) |
               (static_cast<uint32_t>(dst_burst & 0x03) << 22) |
               (static_cast<uint32_t>(dst_mode & 0x01) << 24) |
               (static_cast<uint32_t>(dst_width & 0x03) << 25);
    }

    // Start asynchronous non-blocking DMA transfer
    static bool start_transfer(
        uint8_t channel,
        const DmaLli* lli_desc,
        CompletionCallback callback = {}
    ) noexcept;

    static void stop_transfer(uint8_t channel) noexcept;
    static bool is_busy(uint8_t channel) noexcept;

    // Top-half ISR called from PLIC IRQ 64 (irq_id::DMA_E907)
    static void handle_irq() noexcept;
};

} // namespace hal
