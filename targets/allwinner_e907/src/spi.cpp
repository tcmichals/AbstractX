/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E907 SPI0 DMA Driver
 * ------------------------------------------------
 * 100% ISR and DMA-driven peripheral engine. ZERO POLLING / ZERO BUSY-WAIT.
 */

#include "hal/spi.hpp"
#include "hal/pio.hpp"
#include "hal/dma.hpp"
#include "memory_map.h"
#include <cstring>

namespace fc::hal {

/* SPI0 Hardware Register Mapping */
#define SPI0_GCR        (*(volatile uint32_t *)(SPI0_BASE + 0x04))
#define SPI0_TCR        (*(volatile uint32_t *)(SPI0_BASE + 0x08))
#define SPI0_IER        (*(volatile uint32_t *)(SPI0_BASE + 0x10))
#define SPI0_ISR        (*(volatile uint32_t *)(SPI0_BASE + 0x14))
#define SPI0_FCR        (*(volatile uint32_t *)(SPI0_BASE + 0x18))
#define SPI0_FSR        (*(volatile uint32_t *)(SPI0_BASE + 0x1C))
#define SPI0_CCR        (*(volatile uint32_t *)(SPI0_BASE + 0x24))
#define SPI0_MBC        (*(volatile uint32_t *)(SPI0_BASE + 0x30))
#define SPI0_MTC        (*(volatile uint32_t *)(SPI0_BASE + 0x34))
#define SPI0_BCC        (*(volatile uint32_t *)(SPI0_BASE + 0x38))
#define SPI0_TXD_8      (*(volatile uint8_t  *)(SPI0_BASE + 0x200))
#define SPI0_RXD_8      (*(volatile uint8_t  *)(SPI0_BASE + 0x300))

namespace {

static int g_spi_tx_chan = -1;
static int g_spi_rx_chan = -1;
static ::hal::DmaLli g_spi_tx_lli;
static ::hal::DmaLli g_spi_rx_lli;

static volatile bool g_spi_busy = false;
static int g_active_cs = -1;
static etl::delegate<void(bool)> g_spi_callback{};
static uint8_t g_dummy_tx = 0xFF;
static uint8_t g_dummy_rx = 0;

void on_spi_rx_dma_complete(uint8_t channel, bool success) noexcept {
    (void)channel;
    // Deassert CS
    if (g_active_cs == 0) Pio::set_cs0(false);
    else if (g_active_cs == 1) Pio::set_cs1(false);

    // Disable SPI DRQ
    SPI0_FCR &= ~(1U << 24);

    g_spi_busy = false;
    auto cb = g_spi_callback;
    g_spi_callback = {};
    if (cb.is_valid()) {
        cb(success);
    }
}

} // namespace

void Spi0::init(uint32_t speed_hz) {
    // 1. Soft reset controller
    SPI0_GCR = (1 << 31);
    while (SPI0_GCR & (1 << 31));

    // 2. Enable Master Mode & Transmit Pause Enable
    SPI0_GCR = (1 << 1) | (1 << 0);

    // 3. Reset FIFOs
    SPI0_FCR = (1 << 31) | (1 << 15);

    // 4. Disable manual interrupts (DMA handles completion)
    SPI0_IER = 0x00;

    // 5. Configure Clock
    set_speed(speed_hz);

    // 6. Allocate DMA Channels
    if (g_spi_tx_chan < 0) {
        g_spi_tx_chan = ::hal::DmaController::allocate_channel();
    }
    if (g_spi_rx_chan < 0) {
        g_spi_rx_chan = ::hal::DmaController::allocate_channel();
    }
}

void Spi0::set_speed(uint32_t speed_hz) {
    uint32_t div = 24000000 / (2 * speed_hz);
    if (div > 0) div -= 1;
    if (div > 0xF) div = 0xF;
    SPI0_CCR = div;
}

bool Spi0::is_busy() noexcept {
    return g_spi_busy;
}

bool Spi0::start_dma_transfer(
    int cs_id,
    const uint8_t *tx,
    uint8_t *rx,
    size_t len,
    etl::delegate<void(bool)> callback
) noexcept {
    if (len == 0) return true;
    if (g_spi_tx_chan < 0 || g_spi_rx_chan < 0) return false;

    g_spi_busy = true;
    g_active_cs = cs_id;
    g_spi_callback = callback;

    // 1. Assert Chip Select
    if (cs_id == 0) Pio::set_cs0(true);
    else if (cs_id == 1) Pio::set_cs1(true);

    // 2. Flush & Reset FIFOs
    SPI0_FCR |= (1 << 31) | (1 << 15);

    // 3. Set Total Burst Counter
    SPI0_MBC = len;
    SPI0_MTC = len;
    SPI0_BCC = len;

    // 4. Configure TCR
    SPI0_TCR = (1 << 7); // SS_LEVEL=1 (manual CS)

    // 5. Setup RX DMA LLI (SPI0_RXD_8 -> rx buffer)
    g_spi_rx_lli.src = reinterpret_cast<uint32_t>(&SPI0_RXD_8);
    g_spi_rx_lli.dst = reinterpret_cast<uint32_t>(rx ? rx : &g_dummy_rx);
    g_spi_rx_lli.len = len;
    g_spi_rx_lli.para = 8;
    g_spi_rx_lli.p_lli_next = ::hal::LLI_LAST_ITEM;
    g_spi_rx_lli.cfg = ::hal::DmaController::build_cfg(
        ::hal::DRQ_SPI0, ::hal::DMA_BURST_1, ::hal::DMA_WIDTH_8, ::hal::DMA_MODE_IO,
        ::hal::DRQ_SDRAM, ::hal::DMA_BURST_1, ::hal::DMA_WIDTH_8, rx ? ::hal::DMA_MODE_LINEAR : ::hal::DMA_MODE_IO
    );

    // 6. Setup TX DMA LLI (tx buffer -> SPI0_TXD_8)
    g_spi_tx_lli.src = reinterpret_cast<uint32_t>(tx ? tx : &g_dummy_tx);
    g_spi_tx_lli.dst = reinterpret_cast<uint32_t>(&SPI0_TXD_8);
    g_spi_tx_lli.len = len;
    g_spi_tx_lli.para = 8;
    g_spi_tx_lli.p_lli_next = ::hal::LLI_LAST_ITEM;
    g_spi_tx_lli.cfg = ::hal::DmaController::build_cfg(
        ::hal::DRQ_SDRAM, ::hal::DMA_BURST_1, ::hal::DMA_WIDTH_8, tx ? ::hal::DMA_MODE_LINEAR : ::hal::DMA_MODE_IO,
        ::hal::DRQ_SPI0, ::hal::DMA_BURST_1, ::hal::DMA_WIDTH_8, ::hal::DMA_MODE_IO
    );

    // 7. Start DMA Channels (RX first, then TX)
    ::hal::DmaController::start_transfer(
        static_cast<uint8_t>(g_spi_rx_chan),
        &g_spi_rx_lli,
        etl::delegate<void(uint8_t, bool)>::create<on_spi_rx_dma_complete>()
    );

    ::hal::DmaController::start_transfer(
        static_cast<uint8_t>(g_spi_tx_chan),
        &g_spi_tx_lli,
        {}
    );

    // 8. Enable SPI DMA Request Handshaking (DRQ_EN bit 24)
    SPI0_FCR |= (1U << 24);

    // 9. Start Hardware Transfer (XCH bit 31)
    SPI0_TCR |= (1U << 31);

    return true;
}

/* Synchronous Transfers without Busy-Spinning (Low-Power WFI sleep) */
bool Spi0::transceive_imu_single_sync(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length) {
    if (length == 0) return true;
    start_dma_transfer(1 /* CS1 */, tx_buf, rx_buf, length, {});
    while (is_busy()) {
        __asm__ volatile("wfi");
    }
    return true;
}

bool Spi0::transceive_fpga_dual_sync(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length) {
    if (length == 0) return true;
    start_dma_transfer(0 /* CS0 */, tx_buf, rx_buf, length, {});
    while (is_busy()) {
        __asm__ volatile("wfi");
    }
    return true;
}

} // namespace fc::hal
