/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E907 Sunxi DMA Controller Implementation
 * ---------------------------------------------------------------------
 * 100% ISR and DMA-driven peripheral engine. ZERO POLLING / ZERO BUSY-WAIT.
 */

#include "hal/dma.hpp"
#include "hal/ccu.hpp"
#include "abstractx/interrupt_lock.hpp"

#include <array>

namespace hal {

namespace {

struct DmaChannelState {
    bool allocated{false};
    DmaController::CompletionCallback callback{};
};

static std::array<DmaChannelState, DmaController::NUM_CHANNELS> g_channels{};

} // namespace

void DmaController::init() noexcept {
    // Enable DMA bus gating and deassert reset in CCU
    Ccu::enable_module(Ccu::BusModule::McuDma);

    // Disable all channel interrupts initially
    DMA_IRQ_EN_REG0 = 0;
    DMA_IRQ_EN_REG1 = 0;

    // Clear all pending status
    DMA_IRQ_STAT_REG0 = 0xFFFFFFFFU;
    DMA_IRQ_STAT_REG1 = 0xFFFFFFFFU;

    // Reset channel states
    for (auto& ch : g_channels) {
        ch.allocated = false;
        ch.callback = {};
    }
}

int DmaController::allocate_channel() noexcept {
    abstractx::InterruptGuard guard;
    for (size_t i = 0; i < NUM_CHANNELS; ++i) {
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
    g_channels[channel].callback = {};
}

bool DmaController::start_transfer(
    uint8_t channel,
    const DmaLli* lli_desc,
    CompletionCallback callback
) noexcept {
    if (channel >= NUM_CHANNELS || !lli_desc) return false;

    abstractx::InterruptGuard guard;
    g_channels[channel].callback = callback;

    // Clear any previous status for this channel
    if (channel < 8) {
        DMA_IRQ_STAT_REG0 = (0x0FU << (channel * 4));
        DMA_IRQ_EN_REG0 |= (0x01U << (channel * 4)); // Package End interrupt
    } else {
        uint8_t ch_idx = channel - 8;
        DMA_IRQ_STAT_REG1 = (0x0FU << (ch_idx * 4));
        DMA_IRQ_EN_REG1 |= (0x01U << (ch_idx * 4)); // Package End interrupt
    }

    // Load LLI descriptor physical address and start channel
    DMA_CHAN_LLI_ADDR_REG(channel) = reinterpret_cast<uint32_t>(lli_desc);
    DMA_CHAN_ENABLE_REG(channel) = 1;

    return true;
}

void DmaController::stop_transfer(uint8_t channel) noexcept {
    if (channel >= NUM_CHANNELS) return;

    abstractx::InterruptGuard guard;
    DMA_CHAN_ENABLE_REG(channel) = 0;

    // Disable IRQ for channel
    if (channel < 8) {
        DMA_IRQ_EN_REG0 &= ~(0x0FU << (channel * 4));
    } else {
        uint8_t ch_idx = channel - 8;
        DMA_IRQ_EN_REG1 &= ~(0x0FU << (ch_idx * 4));
    }
}

bool DmaController::is_busy(uint8_t channel) noexcept {
    if (channel >= NUM_CHANNELS) return false;
    return (DMA_CHAN_ENABLE_REG(channel) & 1) != 0;
}

void DmaController::handle_irq() noexcept {
    uint32_t stat0 = DMA_IRQ_STAT_REG0;
    uint32_t stat1 = DMA_IRQ_STAT_REG1;

    // Acknowledge all active interrupts
    DMA_IRQ_STAT_REG0 = stat0;
    DMA_IRQ_STAT_REG1 = stat1;

    // Process channels 0..7
    for (uint8_t ch = 0; ch < 8; ++ch) {
        uint32_t ch_stat = (stat0 >> (ch * 4)) & 0x0F;
        if (ch_stat != 0) {
            // Package End complete
            bool success = (ch_stat & 0x01) != 0;
            auto cb = g_channels[ch].callback;
            g_channels[ch].callback = {};
            // Disable channel enable bit
            DMA_CHAN_ENABLE_REG(ch) = 0;
            if (cb.is_valid()) {
                cb(ch, success);
            }
        }
    }

    // Process channels 8..15
    for (uint8_t ch = 8; ch < NUM_CHANNELS; ++ch) {
        uint8_t ch_idx = ch - 8;
        uint32_t ch_stat = (stat1 >> (ch_idx * 4)) & 0x0F;
        if (ch_stat != 0) {
            bool success = (ch_stat & 0x01) != 0;
            auto cb = g_channels[ch].callback;
            g_channels[ch].callback = {};
            DMA_CHAN_ENABLE_REG(ch) = 0;
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
