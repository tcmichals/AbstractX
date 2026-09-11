/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Pico 2 / Pico 2 W (RP2350) HAL Headers
 */

#pragma once

#include "abstractx/hal/spi.hpp"
#include "abstractx/hal/uart.hpp"
#include "abstractx/hal/timer.hpp"

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/uart.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#endif

namespace abstractx::hal {

class PicoPioDualSpi : public ISpi {
public:
    PicoPioDualSpi() = default;

    bool init(const SpiConfig& config) override {
#ifdef PICO_ON_DEVICE
        spi_init(spi0, config.frequency_hz);
        gpio_set_function(16, GPIO_FUNC_SPI); // RX / MISO
        gpio_set_function(18, GPIO_FUNC_SPI); // SCK
        gpio_set_function(19, GPIO_FUNC_SPI); // TX / MOSI
        gpio_init(17);                        // CS
        gpio_set_dir(17, GPIO_OUT);
        gpio_put(17, 1);
#else
        (void)config;
#endif
        return true;
    }

    void select(bool active) override {
#ifdef PICO_ON_DEVICE
        gpio_put(17, active ? 0 : 1);
#else
        (void)active;
#endif
    }

    uint8_t transfer_byte(uint8_t tx) override {
#ifdef PICO_ON_DEVICE
        uint8_t rx = 0;
        spi_write_read_blocking(spi0, &tx, &rx, 1);
        return rx;
#else
        return tx;
#endif
    }

    bool transfer_sync(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) override {
#ifdef PICO_ON_DEVICE
        select(true);
        if (!tx_data.empty() && !rx_data.empty()) {
            spi_write_read_blocking(spi0, tx_data.data(), rx_data.data(), tx_data.size());
        } else if (!tx_data.empty()) {
            spi_write_blocking(spi0, tx_data.data(), tx_data.size());
        } else if (!rx_data.empty()) {
            spi_read_blocking(spi0, 0x00, rx_data.data(), rx_data.size());
        }
        select(false);
        return true;
#else
        (void)tx_data;
        (void)rx_data;
        return true;
#endif
    }

protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        transfer_sync(req.tx_data, req.rx_data);
        SpiResult result{};
        result.status = SpiStatus::Ok;
        result.transferred_bytes = req.tx_data.empty() ? req.rx_data.size() : req.tx_data.size();
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }
};

class PicoUart : public IUart {
public:
    PicoUart() = default;

    void init(uint32_t baudrate) override {
#ifdef PICO_ON_DEVICE
        uart_init(uart0, baudrate);
        gpio_set_function(0, GPIO_FUNC_UART); // TX
        gpio_set_function(1, GPIO_FUNC_UART); // RX
#else
        (void)baudrate;
#endif
    }

    void write_byte(uint8_t ch) override {
#ifdef PICO_ON_DEVICE
        uart_putc_raw(uart0, ch);
#else
        (void)ch;
#endif
    }

    void puts(const char* str) override {
#ifdef PICO_ON_DEVICE
        uart_puts(uart0, str);
#else
        (void)str;
#endif
    }

    size_t write(std::span<const uint8_t> buffer) override {
#ifdef PICO_ON_DEVICE
        for (uint8_t b : buffer) {
            uart_putc_raw(uart0, b);
        }
        return buffer.size();
#else
        return buffer.size();
#endif
    }

    bool read_byte(uint8_t& ch) override {
#ifdef PICO_ON_DEVICE
        if (uart_is_readable(uart0)) {
            ch = uart_getc(uart0);
            return true;
        }
#endif
        (void)ch;
        return false;
    }

    size_t read(std::span<uint8_t> buffer) override {
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

    void flush() override {}

protected:
    void start_hardware_transfer_from_isr(const UartTxRequest& req) noexcept override {
        write(req.data);
        UartResult result{};
        result.status = UartStatus::Ok;
        result.bytes_transferred = req.data.size();
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }
};

class PicoTimer : public ITimer {
public:
    PicoTimer() = default;

    void delay_us(uint32_t us) override {
#ifdef PICO_ON_DEVICE
        sleep_us(us);
#endif
    }

    void delay_ms(uint32_t ms) override {
#ifdef PICO_ON_DEVICE
        sleep_ms(ms);
#endif
    }

    uint64_t get_time_us() const override {
#ifdef PICO_ON_DEVICE
        return time_us_64();
#else
        return 0;
#endif
    }

    uint32_t get_time_ms() const override {
#ifdef PICO_ON_DEVICE
        return to_ms_since_boot(get_absolute_time());
#else
        return 0;
#endif
    }
};

} // namespace abstractx::hal
