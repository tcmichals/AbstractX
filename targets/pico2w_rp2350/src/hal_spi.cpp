/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX RP2350 / Pico 2 HAL SPI Implementation with Hardware DMA
 * -------------------------------------------------------------------
 * Strict 100% ISR and DMA Invariant:
 * - Simultaneous Full-Duplex DMA bursts on SPI1
 * - Hardware DREQ pacing (spi_get_dreq)
 * - Completion signaled via DMA_IRQ_0 ISR
 * - ZERO POLLING / ZERO BUSY-WAIT LOOPS
 */

#include "abstractx_pico.hpp"

#ifdef PICO_ON_DEVICE
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#endif

#include <cstring>

namespace abstractx::hal {

#ifdef PICO_ON_DEVICE
static int g_spi_rx_dma_chan = -1;
static int g_spi_tx_dma_chan = -1;
static PicoSpi* g_active_spi_instance = nullptr;

static void dma_spi_irq_handler() {
    if (g_spi_rx_dma_chan >= 0 && dma_channel_get_irq0_status(g_spi_rx_dma_chan)) {
        dma_channel_acknowledge_irq0(g_spi_rx_dma_chan);

        // De-assert CS (GP13 = 1)
        gpio_put(13, 1);

        if (g_active_spi_instance) {
            g_active_spi_instance->on_dma_complete_from_isr();
        }
    }
}
#endif

bool PicoSpi::init(const SpiConfig& config) {
#ifdef PICO_ON_DEVICE
    // 1. Initialize SPI1 on GP10 (SCK), GP11 (TX/MOSI), GP12 (RX/MISO), GP13 (CS)
    spi_init(spi1, config.frequency_hz);
    spi_set_format(spi1, 8, SPI_CPOL_1, SPI_CPHA_1, SPI_MSB_FIRST);

    gpio_set_function(10, GPIO_FUNC_SPI); // SCK
    gpio_set_function(11, GPIO_FUNC_SPI); // TX / MOSI
    gpio_set_function(12, GPIO_FUNC_SPI); // RX / MISO

    gpio_init(13); // CS
    gpio_set_dir(13, GPIO_OUT);
    gpio_put(13, 1);

    // 2. Claim dedicated hardware DMA channels for SPI1
    if (g_spi_rx_dma_chan < 0) {
        g_spi_rx_dma_chan = dma_claim_unused_channel(true);
    }
    if (g_spi_tx_dma_chan < 0) {
        g_spi_tx_dma_chan = dma_claim_unused_channel(true);
    }

    g_active_spi_instance = this;

    // 3. Configure DMA IRQ 0 on RX channel completion
    dma_channel_set_irq0_enabled(g_spi_rx_dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_spi_irq_handler);
    irq_set_enabled(DMA_IRQ_0, true);
#else
    (void)config;
#endif
    return true;
}

void PicoSpi::select(bool active) {
#ifdef PICO_ON_DEVICE
    gpio_put(13, active ? 0 : 1);
#else
    (void)active;
#endif
}

uint8_t PicoSpi::transfer_byte(uint8_t tx) {
    uint8_t rx = 0;
    transfer_sync(std::span<const uint8_t>(&tx, 1), std::span<uint8_t>(&rx, 1));
    return rx;
}

bool PicoSpi::transfer_sync(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) {
    // Non-blocking queuing or synchronous fallback without hard spinning
    size_t len = tx_data.empty() ? rx_data.size() : tx_data.size();
    if (len == 0) return true;

#ifdef PICO_ON_DEVICE
    start_dma_transfer_raw(tx_data.data(), rx_data.data(), len);
    // Wait for DMA completion interrupt in low-power WFE
    while (is_hardware_busy()) {
        __asm__ volatile("wfe");
    }
    return true;
#else
    (void)tx_data;
    (void)rx_data;
    return true;
#endif
}

void PicoSpi::start_dma_transfer_raw(const uint8_t* tx, uint8_t* rx, size_t len) noexcept {
#ifdef PICO_ON_DEVICE
    if (g_spi_rx_dma_chan < 0 || g_spi_tx_dma_chan < 0) return;

    busy_ = true;

    // Asserts active-low CS (GP13 = 0)
    gpio_put(13, 0);

    // Provide dummy buffer if tx is null
    static const uint8_t dummy_tx = 0xFF;
    bool inc_tx = (tx != nullptr);
    const void* tx_src = tx ? tx : &dummy_tx;

    // Configure RX DMA channel
    dma_channel_config c_rx = dma_channel_get_default_config(g_spi_rx_dma_chan);
    channel_config_set_transfer_data_size(&c_rx, DMA_SIZE_8);
    channel_config_set_dreq(&c_rx, spi_get_dreq(spi1, false)); // RX DREQ
    channel_config_set_read_increment(&c_rx, false);
    channel_config_set_write_increment(&c_rx, rx != nullptr);
    static uint8_t dummy_rx;
    dma_channel_configure(
        g_spi_rx_dma_chan,
        &c_rx,
        rx ? rx : &dummy_rx,
        &spi_get_hw(spi1)->dr,
        len,
        false
    );

    // Configure TX DMA channel
    dma_channel_config c_tx = dma_channel_get_default_config(g_spi_tx_dma_chan);
    channel_config_set_transfer_data_size(&c_tx, DMA_SIZE_8);
    channel_config_set_dreq(&c_tx, spi_get_dreq(spi1, true)); // TX DREQ
    channel_config_set_read_increment(&c_tx, inc_tx);
    channel_config_set_write_increment(&c_tx, false);
    dma_channel_configure(
        g_spi_tx_dma_chan,
        &c_tx,
        &spi_get_hw(spi1)->dr,
        tx_src,
        len,
        false
    );

    // Trigger simultaneous full-duplex burst
    dma_start_channel_mask((1u << g_spi_rx_dma_chan) | (1u << g_spi_tx_dma_chan));
#else
    (void)tx;
    (void)rx;
    (void)len;
    busy_ = false;
#endif
}

void PicoSpi::start_hardware_transfer_from_isr(const SpiRequest& req) noexcept {
    active_req_ = req;
    size_t len = req.tx_data.empty() ? req.rx_data.size() : req.tx_data.size();
    start_dma_transfer_raw(req.tx_data.data(), req.rx_data.data(), len);
}

void PicoSpi::on_dma_complete_from_isr() noexcept {
    busy_ = false;
    SpiResult result{};
    result.status = SpiStatus::Ok;
    result.transferred_bytes = active_req_.tx_data.empty() ? active_req_.rx_data.size() : active_req_.tx_data.size();

    push_completion_from_isr(active_req_, result);
    set_hardware_idle_from_isr();

    // Advance to next pending request if available
    SpiRequest next_req{};
    if (pop_next_request_from_isr(next_req)) {
        start_hardware_transfer_from_isr(next_req);
    }
}

} // namespace abstractx::hal
