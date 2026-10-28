/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * spi.cpp - MicroPython glue for SPI1 (abstractx.spi_*). The driver is the target's Spi1.
 *
 * @impl [SPEC-E906-REPL-20] apps/e906_repl_app/SPECIFICATION.md
 */

#include "hal/spi.hpp"
#include "hal/timer.hpp"
#include "repl_wait.hpp"

#include <cstddef>
#include <cstdint>

extern "C" {

// ---------------------------------------------------------------------------
// Hardware SPI1 & Dual-SPI DMA Interface ([SPEC-E906-HAL-03], [SPEC-E906-REPL-20])
// ---------------------------------------------------------------------------
namespace {
volatile bool s_spi_dma_done = false;  // shared: main clears, SPI DMA ISR sets last; main polls with a deadline; single hart
volatile bool s_spi_dma_ok = false;  // shared: SPI DMA ISR writes before s_spi_dma_done; single hart, volatile order
__attribute__((section(".sram_c"))) static uint8_t s_spi_tx_staging[512];
__attribute__((section(".sram_c"))) static uint8_t s_spi_rx_staging[512];

void on_spi_transceive_done(bool ok) noexcept {
    s_spi_dma_ok = ok;
    s_spi_dma_done = true;
}
} // namespace

// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-REPL-20] apps/e906_repl_app/SPECIFICATION.md
bool e906_spi_init(uint32_t baud) {
    fc::hal::Spi1::init(baud ? baud : 10000000);
    return true;
}

// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-REPL-20] apps/e906_repl_app/SPECIFICATION.md
int e906_spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len, bool dual) {
    if (len == 0) return 0;
    if (len > sizeof(s_spi_tx_staging)) return -4;

    if (len <= fc::hal::Spi1::DMA_THRESHOLD) {
        bool ok = fc::hal::Spi1::transceive_fifo_sync(0 /* CS0: PB11 */, tx, rx, len, dual);
        return ok ? static_cast<int>(len) : -1;
    }

    // Copy to SRAM Space 1 staging buffer so DMAC can access it (MicroPython heap in DSP SRAM is inaccessible to DMAC)
    if (tx) {
        __builtin_memcpy(s_spi_tx_staging, tx, len);
    } else {
        __builtin_memset(s_spi_tx_staging, 0xFF, len);
    }

    s_spi_dma_done = false;
    s_spi_dma_ok = false;
    if (!fc::hal::Spi1::start_dma_transfer(
        0 /* CS0: PB11 */,
        s_spi_tx_staging,
        s_spi_rx_staging,
        len,
        etl::delegate<void(bool)>::create<on_spi_transceive_done>(),
        dual
    )) {
        return -1;
    }

    const uint64_t deadline = hal::Timer::get_ticks() + 100ULL * hal::Timer::TICKS_PER_MS;
    while (!s_spi_dma_done && hal::Timer::get_ticks() < deadline) {
        repl_wait_step(deadline);
    }
    if (!s_spi_dma_done) {
        return -2; // timeout
    }
    if (s_spi_dma_ok && rx) {
        __builtin_memcpy(rx, s_spi_rx_staging, len);
    }
    return s_spi_dma_ok ? static_cast<int>(len) : -3;
}

} // extern "C"
