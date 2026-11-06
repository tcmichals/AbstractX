/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX RP2350 / Pico 2 HAL UART Implementation
 * --------------------------------------------------
 * Strict 100% ISR and DMA Invariant:
 * - Non-blocking TX using TX FIFO interrupt & SPSC ring buffer
 * - Non-blocking RX using RX FIFO interrupt & SPSC ring buffer
 * - ZERO POLLING / ZERO BUSY-WAIT LOOPS
 */

#include "abstractx_pico.hpp"
#include "hal/ports.hpp"

#ifdef PICO_ON_DEVICE
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"
#endif

namespace abstractx::hal {

#ifdef PICO_ON_DEVICE
static PicoUart* g_active_uart_instance = nullptr;

static void uart0_irq_handler() {
    if (!g_active_uart_instance) return;

    // Drain all available RX FIFO bytes into SPSC ring buffer
    while (uart_is_readable(uart0)) {
        uint8_t ch = uart_getc(uart0);
        g_active_uart_instance->on_rx_byte_from_isr(ch);
    }

    // Feed TX FIFO from TX ring buffer if writable
    if (uart_is_writable(uart0)) {
        g_active_uart_instance->on_tx_ready_from_isr();
    }
}
#endif

void PicoUart::init(uint32_t baudrate) {
#ifdef PICO_ON_DEVICE
    uart_init(uart0, baudrate);
    static_assert(::hal::ports::kUart0.instance == 0, "uart0 here is ports::kUart0");
    gpio_set_function(::hal::ports::kUart0.tx_pin, GPIO_FUNC_UART);   // [SPEC-PICO-HAL-04]
    gpio_set_function(::hal::ports::kUart0.rx_pin, GPIO_FUNC_UART);

    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);

    rx_queue_.clear();
    tx_queue_.clear();

    g_active_uart_instance = this;

    // Set up interrupt handler for UART0
    irq_set_exclusive_handler(UART0_IRQ, uart0_irq_handler);
    irq_set_enabled(UART0_IRQ, true);

    // Enable RX interrupt
    uart_set_irq_enables(uart0, true, false);
#else
    (void)baudrate;
#endif
}

// @impl [SPEC-PICO-HAL-01] targets/pico2w_rp2350/SPECIFICATION.md (UART send queue)
bool PicoUart::queue_byte(uint8_t ch) noexcept {
#ifdef PICO_ON_DEVICE
    InterruptGuard lock;
    // Queue empty and hardware FIFO writable: write directly (keeps byte order with the TX interrupt)
    if (tx_queue_.empty() && uart_is_writable(uart0)) {
        uart_putc_raw(uart0, ch);
        return true;
    }
    if (!tx_queue_.push(ch)) {               // full: never report a byte that was not taken
        tx_dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    uart_set_irq_enables(uart0, true, true); // the TX interrupt drains the queue
    return true;
#else
    (void)ch;
    return true;
#endif
}

void PicoUart::write_byte(uint8_t ch) {
    (void)queue_byte(ch);                    // a dropped byte is counted in tx_dropped()
}

void PicoUart::puts(const char* str) {
    if (!str) return;
    while (*str) {
        if (*str == '\n') write_byte('\r');
        write_byte(static_cast<uint8_t>(*str++));
    }
}

// Returns the bytes taken; stops at the first that does not fit.
size_t PicoUart::write(std::span<const uint8_t> buffer) {
    size_t n = 0;
    for (uint8_t b : buffer) {
        if (!queue_byte(b)) break;
        ++n;
    }
    return n;
}

bool PicoUart::read_byte(uint8_t& ch) {
    return rx_queue_.pop(ch);
}

size_t PicoUart::read(std::span<uint8_t> buffer) {
    size_t count = 0;
    for (uint8_t& b : buffer) {
        if (read_byte(b)) {
            count++;
        } else {
            break;
        }
    }
    return count;
}

void PicoUart::on_rx_byte_from_isr(uint8_t ch) noexcept {
    rx_queue_.push(ch);
}

void PicoUart::on_tx_ready_from_isr() noexcept {
#ifdef PICO_ON_DEVICE
    uint8_t byte = 0;
    while (uart_is_writable(uart0) && tx_queue_.pop(byte)) {
        uart_putc_raw(uart0, byte);
    }

    if (tx_queue_.empty()) {
        // Disable TX interrupt when queue is empty to prevent interrupt storm
        uart_set_irq_enables(uart0, true, false);
    }
#endif
}

} // namespace abstractx::hal
