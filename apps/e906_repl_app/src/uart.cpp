/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * uart.cpp - MicroPython glue for UART3 (abstractx.uart_*). The driver is the target's E906Uart.
 *
 * @impl [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
 * @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md
 */

#include "hal/uart.hpp"
#include "hal/driver_wait.hpp"
#include "repl_wait.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace {
abstractx::hal::E906Uart& uart() noexcept { return abstractx::hal::get_e906_uart3(); }
} // namespace

extern "C" {

bool e906_uart3_init(uint32_t baud) {
    hal::set_driver_wait_hook(repl_wait_step);
    uart().init(baud);
    return uart().attached();
}

bool e906_uart3_set_dma(bool enable) { return uart().set_dma(enable); }
bool e906_uart3_get_dma(void) { return uart().dma_active(); }

size_t e906_uart3_read(uint8_t* dst, size_t max_len) {
    return uart().read(std::span<uint8_t>(dst, max_len));
}

size_t e906_uart3_write(const uint8_t* src, size_t len) {
    return uart().write(std::span<const uint8_t>(src, len));
}

uint32_t e906_uart3_lsr(void) { return uart().line_status(); }
uint32_t e906_uart3_rx_dropped(void) { return uart().rx_dropped(); }

} // extern "C"
