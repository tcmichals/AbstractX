/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: DMA completion acts on enabled status bits only [SPEC-E906-HAL-01]
 * Compiles the real targets/allwinner_e906/src/dma.cpp against a fake DMAC register block.
 * The DMAC latches half-package / package-end bits even when only queue-end is enabled; the
 * handler must not complete (or stop) a channel on those bits when another channel's interrupt
 * brought it in. Board case (then on ch 8 / 11): PEND1 = 0x1007, EN1 = 0x4004 (TX done, RX half).
 * Channels 8, 9 and 10 are firewalled ([SPEC-E906-HAL-01]), so the first two are 11 and 12.
 */
// @verifies [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
#undef NDEBUG  // the checks below must run in Release builds too

#include "hal/dma.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>

alignas(64) unsigned char g_fake_dma_regs[0x800];

namespace {

volatile uint32_t &reg(size_t off) {
    return *reinterpret_cast<volatile uint32_t *>(g_fake_dma_regs + off);
}
volatile uint32_t &chan_enable(uint8_t ch) { return reg(0x100 + ch * 0x40U); }

struct Done {
    int calls = 0;
    bool ok = false;
};
Done g_done[16];

template <uint8_t Ch>
void on_done(uint8_t channel, bool success) noexcept {
    assert(channel == Ch);
    ++g_done[Ch].calls;
    g_done[Ch].ok = success;
}

hal::DmaLli g_lli{};

void reset_counts() {
    for (auto &d : g_done) d = Done{};
}

}  // namespace

int main() {
    using hal::DmaController;
    DmaController::init();
    const int tx = DmaController::allocate_channel();
    const int rx = DmaController::allocate_channel();
    assert(tx == 11 && rx == 12);  // 8, 9 and 10 are firewalled

    for (int iter = 0; iter < 1000; ++iter) {
        reset_counts();
        // SPI1 DMA: RX carries the callback, TX has none; both enable queue end only.
        assert(DmaController::start_transfer(12, &g_lli,
               DmaController::CompletionCallback::create<on_done<12>>()));
        assert(DmaController::start_transfer(11, &g_lli,
               DmaController::CompletionCallback::create<on_done<11>>()));
        assert(reg(0x04) == 0x44000U);  // queue end enabled on ch 11 and ch 12

        // 1. TX (ch 11) queue end while RX (ch 12) has only reached half package.
        reg(0x14) = 0x17000U;
        DmaController::handle_irq();
        assert(g_done[11].calls == 1 && g_done[11].ok);
        assert(g_done[12].calls == 0);           // RX not completed on a disabled bit
        assert(chan_enable(12) == 1);            // and not stopped

        // 2. RX reaches queue end: now it completes, successfully, exactly once.
        reg(0x14) = 0x70000U;
        DmaController::handle_irq();
        assert(g_done[12].calls == 1 && g_done[12].ok);
        assert(chan_enable(12) == 0);
        assert(g_done[11].calls == 1);
    }

    // 3. Cyclic ring with package end enabled still gets every package.
    reset_counts();
    assert(DmaController::start_transfer(11, &g_lli,
           DmaController::CompletionCallback::create<on_done<11>>(), hal::DMA_IRQ_PKG, true));
    for (int i = 0; i < 1000; ++i) {
        reg(0x14) = 0x3000U;  // ch 11: half + package end
        DmaController::handle_irq();
    }
    assert(g_done[11].calls == 1000 && g_done[11].ok);
    DmaController::stop_transfer(11);

    std::cout << "test_e906_dma_irq: OK\n";
    return 0;
}
