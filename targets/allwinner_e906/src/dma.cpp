/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E906 Sunxi DMA Controller Implementation
 * ---------------------------------------------------------------------
 * 100% ISR and DMA-driven peripheral engine. ZERO POLLING / ZERO BUSY-WAIT.
 */

#include "hal/dma.hpp"
#include "hal/clic.hpp"
#include "abstractx/interrupt_lock.hpp"

#include <array>

namespace hal {

namespace {

struct DmaChannelState {
    bool allocated{false};
    bool cyclic{false};
    DmaController::CompletionCallback callback{};
};

static std::array<DmaChannelState, DmaController::NUM_CHANNELS> g_channels{};

} // namespace

// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
void DmaController::init() noexcept {
    // The main DMAC's clock and reset belong to Linux (it keeps the controller clocked); the
    // E906 never touches them ([SPEC-E906-HAL-01]). Channels 0..7 are Linux's (overlay
    // dma-channels = <8>); only register 1 (channels 8..15) is written here.
    DMA_IRQ_EN_REG1 = 0;
    DMA_IRQ_STAT_REG1 = 0xFFFFFFFFU;
    // Route 8..15 to the E906 and away from Linux before any of them can interrupt: Linux's one
    // DMA interrupt covers all 16 channels and its handler only clears register 0.
    DMA_IRQ_CPU_EN_REG = DMA_IRQ_CPU_EN_REG & 0x00FFU;
    DMA_IRQ_MCU_EN_REG = 0xFF00U;
    clic::enable(clic::kLineDma);

    // Reset channel states
    for (auto& ch : g_channels) {
        ch.allocated = false;
        ch.cyclic = false;
        ch.callback = {};
    }
}

int DmaController::allocate_channel() noexcept {
    abstractx::InterruptGuard guard;
    for (size_t i = DMA_START_CHANNEL; i < NUM_CHANNELS; ++i) {
        if (!g_channels[i].allocated) {
            g_channels[i].allocated = true;
            g_channels[i].callback = {};
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DmaController::release_channel(uint8_t channel) noexcept {
    if (channel >= NUM_CHANNELS) return;
    stop_transfer(channel);

    abstractx::InterruptGuard guard;
    g_channels[channel].allocated = false;
    g_channels[channel].cyclic = false;
    g_channels[channel].callback = {};
}

// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
bool DmaController::start_transfer(
    uint8_t channel,
    const DmaLli* lli_desc,
    CompletionCallback callback,
    uint8_t irq_type,
    bool cyclic
) noexcept {
    if (channel < DMA_START_CHANNEL || channel >= NUM_CHANNELS || !lli_desc) return false;

    abstractx::InterruptGuard guard;
    g_channels[channel].callback = callback;
    g_channels[channel].cyclic = cyclic;

    // Per-channel nibble: bit0 half package, bit1 package end, bit2 queue end.
    const uint8_t ch_idx = channel - 8;
    DMA_IRQ_STAT_REG1 = (0x0FU << (ch_idx * 4));
    DMA_IRQ_EN_REG1 = (DMA_IRQ_EN_REG1 & ~(0x0FU << (ch_idx * 4))) |
                      (static_cast<uint32_t>(irq_type & 0x07U) << (ch_idx * 4));

    // The DMAC fetches the descriptor by physical address ([SPEC-E906-HAL-01]).
    DMA_CHAN_MODE_REG(channel) = 0;  // wait mode both sides, as Linux leaves it
    DMA_CHAN_LLI_ADDR_REG(channel) = pa(lli_desc);
    DMA_CHAN_ENABLE_REG(channel) = 1;

    return true;
}

// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
void DmaController::stop_transfer(uint8_t channel) noexcept {
    if (channel >= NUM_CHANNELS) return;

    abstractx::InterruptGuard guard;
    DMA_CHAN_ENABLE_REG(channel) = 0;

    // Disable IRQ for channel (E906 channels only)
    if (channel >= DMA_START_CHANNEL) {
        DMA_IRQ_EN_REG1 &= ~(0x0FU << ((channel - 8) * 4));
    }
}

bool DmaController::is_busy(uint8_t channel) noexcept {
    if (channel >= NUM_CHANNELS) return false;
    return (DMA_CHAN_ENABLE_REG(channel) & 1) != 0;
}

// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
void DmaController::handle_irq() noexcept {
    // Only channels 8..15 are the E906's; acknowledging register 0 would steal
    // Linux's completions ([SPEC-E906-HAL-01]).
    const uint32_t stat1 = DMA_IRQ_STAT_REG1;
    DMA_IRQ_STAT_REG1 = stat1;

    for (uint8_t ch = DMA_START_CHANNEL; ch < NUM_CHANNELS; ++ch) {
        const uint32_t ch_stat = (stat1 >> ((ch - 8) * 4)) & 0x0F;
        if (ch_stat != 0) {
            const bool success = (ch_stat & 0x02) != 0 || (ch_stat & 0x04) != 0;  // package / queue end
            auto cb = g_channels[ch].callback;
            if (!g_channels[ch].cyclic) {  // a ring keeps running and keeps its callback
                g_channels[ch].callback = {};
                DMA_CHAN_ENABLE_REG(ch) = 0;
            }
            if (cb.is_valid()) {
                cb(ch, success);
            }
        }
    }
}

} // namespace hal

// Weak alias override from irq_dispatcher.cpp
extern "C" __attribute__((section(".fastcode")))
void fc_dma_isr() noexcept {
    hal::DmaController::handle_irq();
}
