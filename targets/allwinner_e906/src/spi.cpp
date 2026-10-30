/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E906 SPI1 Driver
 * --------------------------------------------
 * Dual-Mode Engine with Threshold Balancing ([SPEC-E906-HAL-01], [SPEC-E906-HAL-03]):
 * 1. Low-Latency Fast FIFO Direct Transfer (<= 64 bytes, SPI1 FIFO depth): Zero DMA setup overhead
 * 2. High-Performance Dual-Channel DMA (> 64 bytes, DRQ_SPI1 = 23): Zero CPU bus cycles
 * 3. Dual-Mode (Single-SPI 1-bit on PB13/PB14 & Dual-SPI 2-bit on PB13/PB14)
 */

#include "hal/spi.hpp"
#include "hal/pio.hpp"
#include "hal/dma.hpp"
#include "hal/ccu.hpp"
#include "hal/timer.hpp"
#include "abstractx/interrupt_lock.hpp"
// @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
#include "memory_map.h"
#include "abstractx/isr_dispatcher.hpp"
#include <cstring>

namespace fc::hal {

/* SPI1 Hardware Register Mapping (Base: 0x04026000) */
#define SPI1_GCR        (*(volatile uint32_t *)(SPI1_BASE + 0x04))
#define SPI1_TCR        (*(volatile uint32_t *)(SPI1_BASE + 0x08))
#define SPI1_IER        (*(volatile uint32_t *)(SPI1_BASE + 0x10))
#define SPI1_ISR        (*(volatile uint32_t *)(SPI1_BASE + 0x14))
#define SPI1_FCR        (*(volatile uint32_t *)(SPI1_BASE + 0x18))
#define SPI1_FSR        (*(volatile uint32_t *)(SPI1_BASE + 0x1C))
#define SPI1_CCR        (*(volatile uint32_t *)(SPI1_BASE + 0x24))
#define SPI1_MBC        (*(volatile uint32_t *)(SPI1_BASE + 0x30))
#define SPI1_MTC        (*(volatile uint32_t *)(SPI1_BASE + 0x34))
#define SPI1_BCC        (*(volatile uint32_t *)(SPI1_BASE + 0x38))
#define SPI1_TXD_8      (*(volatile uint8_t  *)(SPI1_BASE + 0x200))
#define SPI1_RXD_8      (*(volatile uint8_t  *)(SPI1_BASE + 0x300))

namespace {

static int g_spi_tx_chan = -1;
static int g_spi_rx_chan = -1;
alignas(4) static ::hal::DmaLli g_spi_tx_lli __attribute__((section(".sram_c")));
alignas(4) static ::hal::DmaLli g_spi_rx_lli __attribute__((section(".sram_c")));

static volatile bool g_spi_busy = false;  // shared: claim_bus() sets (InterruptGuard test-and-set; FIFO and DMA paths), FIFO path or SPI DMA ISR clears; volatile
static int g_active_cs = -1;
static etl::delegate<void(bool)> g_spi_callback{};
static uint8_t g_dummy_tx = 0xFF;
static uint8_t g_dummy_rx = 0;

// One owner of SPI1 at a time, FIFO or DMA, thread or ISR context.
// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
bool claim_bus() noexcept {
    abstractx::InterruptGuard guard;
    if (g_spi_busy) return false;
    g_spi_busy = true;
    return true;
}

volatile bool g_sync_dma_ok = false;  // shared: SPI DMA ISR writes, sync wrapper (bus owner) reads; volatile

void on_sync_dma_done(bool success) noexcept {
    g_sync_dma_ok = success;
}

void on_spi_rx_dma_complete(uint8_t channel, bool success) noexcept {
    (void)channel;
    // Deassert CS
    if (g_active_cs == 0) Pio::set_cs0(false);
    else if (g_active_cs == 1) Pio::set_cs1(false);

    // Disable SPI DRQs (TX bit 24 and RX bit 8)
    SPI1_FCR &= ~((1U << 24) | (1U << 8));

    // Stop and disable DMA channels
    if (g_spi_tx_chan >= 0) ::hal::DmaController::stop_transfer(static_cast<uint8_t>(g_spi_tx_chan));
    if (g_spi_rx_chan >= 0) ::hal::DmaController::stop_transfer(static_cast<uint8_t>(g_spi_rx_chan));

    g_spi_busy = false;
    auto cb = g_spi_callback;
    g_spi_callback = {};
    if (cb.is_valid()) {
        cb(success);
    }
}

} // namespace

void Spi1::init(uint32_t speed_hz) {
    // 1. Enable CCU bus gate and module clock for SPI1
    ::hal::Ccu::enable_module(::hal::Ccu::BusModule::Spi1);

    // 2. Configure Port B pinmux for PB11..PB14 (40-pin header)
    // PB11: CS0 (Pin 24) -> Function 1 (GPIO Output)
    // PB12: CLK (Pin 23) -> Function 4 (SPI1_CLK)
    // PB13: MOSI/IO0 (Pin 19) -> Function 4 (SPI1_MOSI)
    // PB14: MISO/IO1 (Pin 21) -> Function 4 (SPI1_MISO)
    volatile uint32_t& pb_cfg1 = *(volatile uint32_t *)(PIO_BASE + 0x0034);
    pb_cfg1 &= ~(0xFFFFU << 12);
    pb_cfg1 |= (0x4441U << 12);

    // Deassert CS0 (PB11 High)
    Pio::set_cs0(false);

    // 3. Soft reset controller
    SPI1_GCR = (1U << 31);
    const uint64_t rst_deadline = ::hal::Timer::get_ticks() + 5ULL * ::hal::Timer::TICKS_PER_MS;
    while ((SPI1_GCR & (1U << 31)) && (::hal::Timer::get_ticks() < rst_deadline)) {
    }

    // 4. Enable Master Mode & Transmit Pause Enable & Bus Enable
    SPI1_GCR = (1U << 7) | (1U << 1) | (1U << 0);

    // 5. Reset FIFOs
    SPI1_FCR = (1U << 31) | (1U << 15);

    // 6. Disable manual interrupts (DMA handles completion)
    SPI1_IER = 0x00;

    // 7. Configure Clock
    set_speed(speed_hz);

    // 8. Allocate DMA Channels (Ensuring DmaController is initialized first)
    ::hal::DmaController::init();
    if (g_spi_tx_chan < 0) {
        g_spi_tx_chan = ::hal::DmaController::allocate_channel();
    }
    if (g_spi_rx_chan < 0) {
        g_spi_rx_chan = ::hal::DmaController::allocate_channel();
    }
}

void Spi1::set_speed(uint32_t speed_hz) {
    if (speed_hz == 0) speed_hz = 1000000;
    // CDR2: f_sclk = 24MHz / (2 * (M + 1))
    int32_t div = static_cast<int32_t>(24000000 / (2 * speed_hz));
    if (div > 0) div -= 1;
    if (div > 0xFF) div = 0xFF;
    if (div < 0) div = 0;
    SPI1_CCR = static_cast<uint32_t>(div);
}

bool Spi1::is_busy() noexcept {
    return g_spi_busy;
}

bool Spi1::transceive_fifo_sync(int cs_id, const uint8_t *tx_buf, uint8_t *rx_buf, size_t length, bool dual) {
    if (length == 0) return true;
    // @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
    if (!claim_bus()) return false;  // a DMA (or DRDY auto-DMA) owns SPI1

    // 1. Assert Chip Select
    if (cs_id == 0) Pio::set_cs0(true);
    else if (cs_id == 1) Pio::set_cs1(true);

    // 2. Reset FIFOs
    SPI1_FCR = (1U << 31) | (1U << 15);
    SPI1_FCR = 0;

    // 3. Set Total Burst Counter
    SPI1_MBC = static_cast<uint32_t>(length);
    SPI1_MTC = static_cast<uint32_t>(length);
    SPI1_BCC = dual ? ((1U << 28) | static_cast<uint32_t>(length)) : static_cast<uint32_t>(length);

    // 4. Manual CS mode (CS_MANUAL | CS_LEVEL)
    SPI1_TCR = (1U << 7) | (1U << 6);

    // 5. Fill TX FIFO directly
    for (size_t i = 0; i < length; ++i) {
        SPI1_TXD_8 = tx_buf ? tx_buf[i] : 0xFF;
    }

    // 6. Start transfer (XCH bit 31)
    SPI1_TCR |= (1U << 31);

    // 7. Wait for transfer complete with bounded hardware timer (XCH self-clears)
    const uint64_t deadline = ::hal::Timer::get_ticks() + 10ULL * ::hal::Timer::TICKS_PER_MS;
    while ((SPI1_TCR & (1U << 31)) && (::hal::Timer::get_ticks() < deadline)) {
    }
    if (SPI1_TCR & (1U << 31)) {
        if (cs_id == 0) Pio::set_cs0(false);
        else if (cs_id == 1) Pio::set_cs1(false);
        g_spi_busy = false;
        return false;
    }

    // 8. Drain RX FIFO
    for (size_t i = 0; i < length; ++i) {
        uint8_t byte = SPI1_RXD_8;
        if (rx_buf) rx_buf[i] = byte;
    }

    // 9. Deassert Chip Select
    if (cs_id == 0) Pio::set_cs0(false);
    else if (cs_id == 1) Pio::set_cs1(false);

    g_spi_busy = false;
    return true;
}

bool Spi1::start_dma_transfer(
    int cs_id,
    const uint8_t *tx,
    uint8_t *rx,
    size_t len,
    etl::delegate<void(bool)> callback,
    bool dual
) noexcept {
    if (len == 0) return true;
    if (g_spi_tx_chan < 0 || g_spi_rx_chan < 0) return false;

    // A transfer in flight owns the LLIs and the callback: refuse instead of overwriting them
    // (callers run in thread and ISR context, so test-and-set with interrupts masked).
    // @impl [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md
    if (!claim_bus()) return false;
    g_active_cs = cs_id;
    g_spi_callback = callback;

    // 1. Assert Chip Select
    if (cs_id == 0) Pio::set_cs0(true);
    else if (cs_id == 1) Pio::set_cs1(true);

    // 2. Flush & Reset FIFOs
    SPI1_FCR = (1U << 31) | (1U << 15);

    // Setup FIFO DMA request trigger levels and DRQ enables:
    // RF_RDY_TRIG_LEVEL = 1 (bits 0..7)
    // TF_ERQ_TRIG_LEVEL = 32 (bits 16..23)
    // RF_DRQ_EN = bit 8
    // TF_DRQ_EN = bit 24
    // Resets (bits 31, 15) must be 0!
    SPI1_FCR = (1U << 24) | (32U << 16) | (1U << 8) | (1U << 0);

    // 3. Set Total Burst Counter
    SPI1_MBC = static_cast<uint32_t>(len);
    SPI1_MTC = static_cast<uint32_t>(len);
    SPI1_BCC = dual ? ((1U << 28) | static_cast<uint32_t>(len)) : static_cast<uint32_t>(len);

    // 4. Configure TCR (CS_MANUAL | CS_LEVEL)
    SPI1_TCR = (1U << 7) | (1U << 6);

    // 5. Setup RX DMA LLI (SPI1_RXD_8 -> rx buffer) ([SPEC-E906-HAL-01], [SPEC-E906-HAL-03])
    g_spi_rx_lli.src = ::hal::DmaController::pa(&SPI1_RXD_8);
    g_spi_rx_lli.dst = ::hal::DmaController::pa(rx ? rx : &g_dummy_rx);
    g_spi_rx_lli.len = static_cast<uint32_t>(len);
    g_spi_rx_lli.para = 0x40U;
    g_spi_rx_lli.p_lli_next = ::hal::LLI_LAST_ITEM;
    g_spi_rx_lli.cfg = ::hal::DmaController::build_cfg(
        ::hal::DRQ_SPI1, ::hal::DMA_BURST_1, ::hal::DMA_WIDTH_8, ::hal::DMA_MODE_IO,
        ::hal::DRQ_SDRAM, ::hal::DMA_BURST_1, ::hal::DMA_WIDTH_8, rx ? ::hal::DMA_MODE_LINEAR : ::hal::DMA_MODE_IO
    );

    // 6. Setup TX DMA LLI (tx buffer -> SPI1_TXD_8)
    g_spi_tx_lli.src = ::hal::DmaController::pa(tx ? tx : &g_dummy_tx);
    g_spi_tx_lli.dst = ::hal::DmaController::pa(&SPI1_TXD_8);
    g_spi_tx_lli.len = static_cast<uint32_t>(len);
    g_spi_tx_lli.para = 0x40U;
    g_spi_tx_lli.p_lli_next = ::hal::LLI_LAST_ITEM;
    g_spi_tx_lli.cfg = ::hal::DmaController::build_cfg(
        ::hal::DRQ_SDRAM, ::hal::DMA_BURST_1, ::hal::DMA_WIDTH_8, tx ? ::hal::DMA_MODE_LINEAR : ::hal::DMA_MODE_IO,
        ::hal::DRQ_SPI1, ::hal::DMA_BURST_1, ::hal::DMA_WIDTH_8, ::hal::DMA_MODE_IO
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

    // 8. Start Hardware Transfer (XCH bit 31)
    SPI1_TCR |= (1U << 31);

    return true;
}

/* Synchronous Transfers with Fast-Path Thresholding */
bool Spi1::transceive_imu_single_sync(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length) {
    if (length == 0) return true;
    if (length <= DMA_THRESHOLD) {
        return transceive_fifo_sync(0 /* CS0: PB11 */, tx_buf, rx_buf, length, false);
    }
    g_sync_dma_ok = false;
    if (!start_dma_transfer(0 /* CS0: PB11 */, tx_buf, rx_buf, length,
                            etl::delegate<void(bool)>::create<on_sync_dma_done>(), false)) {
        return false;  // another transfer owns SPI1; never wait for it ([SPEC-E906-HAL-03])
    }
    const abstractx::interrupt_flags_t flags = abstractx::disable_interrupts_save_flags();
    while (is_busy()) {
#if defined(__riscv)
        __asm__ volatile("csrs mstatus, %0\nwfi\n" :: "r"(8U) : "memory");
        abstractx::disable_interrupts_save_flags();
#else
        break;
#endif
    }
    abstractx::restore_interrupts_flags(flags);
    return !is_busy() && g_sync_dma_ok;
}

bool Spi1::transceive_fpga_dual_sync(const uint8_t *tx_buf, uint8_t *rx_buf, size_t length) {
    if (length == 0) return true;
    if (length <= DMA_THRESHOLD) {
        return transceive_fifo_sync(0 /* CS0: PB11 */, tx_buf, rx_buf, length, true);
    }
    g_sync_dma_ok = false;
    if (!start_dma_transfer(0 /* CS0: PB11 */, tx_buf, rx_buf, length,
                            etl::delegate<void(bool)>::create<on_sync_dma_done>(), true)) {
        return false;  // another transfer owns SPI1; never wait for it ([SPEC-E906-HAL-03])
    }
    const abstractx::interrupt_flags_t flags = abstractx::disable_interrupts_save_flags();
    while (is_busy()) {
#if defined(__riscv)
        __asm__ volatile("csrs mstatus, %0\nwfi\n" :: "r"(8U) : "memory");
        abstractx::disable_interrupts_save_flags();
#else
        break;
#endif
    }
    abstractx::restore_interrupts_flags(flags);
    return !is_busy() && g_sync_dma_ok;
}

/* AsyncTransferAwaiter Implementation */
Spi1::AsyncTransferAwaiter::AsyncTransferAwaiter(SpiMode m, int cs, const uint8_t *t, uint8_t *r, size_t l)
    : mode(m), cs_id(cs), tx(t), rx(r), len(l), completed(false) {}

bool Spi1::AsyncTransferAwaiter::await_ready() const noexcept {
    return len == 0;
}

// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
void Spi1::AsyncTransferAwaiter::on_done(bool success) noexcept {
    completed = success;
    const auto handle = waiter;
    waiter = {};
    if (handle) {
        abstractx::IsrDispatcher::post(handle);
    }
}

bool Spi1::AsyncTransferAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
    const bool dual = (mode == SpiMode::DualIO);
    // Threshold decision:
    // If len <= DMA_THRESHOLD, execute immediate fast-path FIFO and resume
    if (len <= DMA_THRESHOLD) {
        completed = transceive_fifo_sync(cs_id, tx, rx, len, dual);
        return false; // resume immediately; never resume inline
    }

    // Large payload: stream via dual DMA channels. The handle and the result live in this
    // awaiter (coroutine frame); a refused start leaves every other transfer untouched.
    waiter = handle;
    if (!start_dma_transfer(cs_id, tx, rx, len,
                            etl::delegate<void(bool)>::create<AsyncTransferAwaiter, &AsyncTransferAwaiter::on_done>(*this),
                            dual)) {
        waiter = {};
        completed = false;
        return false;
    }
    return true;
}

bool Spi1::AsyncTransferAwaiter::await_resume() noexcept {
    return completed;
}

void Spi1::handle_irq() {
    uint32_t status = SPI1_ISR;
    SPI1_ISR = status; // Clear pending bits
}

} // namespace fc::hal
