/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Pico 2 / Pico 2 W (RP2350) HAL Headers
 */

#pragma once

#include "abstractx/hal/spi.hpp"
#include "abstractx/hal/i2c.hpp"
#include "abstractx/hal/uart.hpp"
#include "abstractx/hal/timer.hpp"
#include "abstractx/hal/gpio.hpp"
#include "abstractx/hal/io_processor.hpp"
#include "spsc_tlp_ring.hpp"

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include "hardware/uart.h"
#include "hardware/timer.h"
#include "hardware/gpio.h"
#endif

namespace abstractx::hal {

// ----------------------------------------------------------------------------
// PicoSpi: Hardware SPI1 with DMA Channels (INAV/Linux Style)
// ----------------------------------------------------------------------------
class PicoSpi : public ISpi {
public:
    PicoSpi() = default;

    bool init(const SpiConfig& config) override;
    void select(bool active) override;
    uint8_t transfer_byte(uint8_t tx) override;
    bool transfer_sync(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) override;

    void start_dma_transfer_raw(const uint8_t* tx, uint8_t* rx, size_t len) noexcept;
    void on_dma_complete_from_isr() noexcept;
    bool is_hardware_busy() const noexcept { return busy_; }

protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override;

private:
    SpiRequest active_req_{};
    volatile bool busy_{false};
};

using PicoPioDualSpi = PicoSpi; // Compatibility alias

// ----------------------------------------------------------------------------
// PicoUart: Non-blocking Interrupt-Driven UART0
// ----------------------------------------------------------------------------
class PicoUart : public IUart {
public:
    PicoUart() = default;

    void init(uint32_t baudrate) override;
    void write_byte(uint8_t ch) override;
    void puts(const char* str) override;
    size_t write(std::span<const uint8_t> buffer) override;

    bool read_byte(uint8_t& ch) override;
    size_t read(std::span<uint8_t> buffer) override;
    void flush() override {}

    void on_rx_byte_from_isr(uint8_t ch) noexcept;
    void on_tx_ready_from_isr() noexcept;

protected:
    void start_hardware_transfer_from_isr(const UartTxRequest& req) noexcept override {
        write(req.data);
        UartResult result{};
        result.status = UartStatus::Ok;
        result.bytes_transferred = req.data.size();
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }

private:
    abstractx::SpscRingBuffer<uint8_t, 512> rx_queue_{};
    abstractx::SpscRingBuffer<uint8_t, 512> tx_queue_{};
};

// ----------------------------------------------------------------------------
// PicoTimer: 64-bit Hardware Timer with Alarm Interrupts
// ----------------------------------------------------------------------------
class PicoTimer : public ITimer {
public:
    PicoTimer() = default;

    void delay_us(uint32_t us) override {
#ifdef PICO_ON_DEVICE
        sleep_us(us);
#else
        (void)us;
#endif
    }

    void delay_ms(uint32_t ms) override {
#ifdef PICO_ON_DEVICE
        sleep_ms(ms);
#else
        (void)ms;
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

protected:
    void start_hardware_transfer_from_isr(const TimerRequest& req) noexcept override {
        active_req_ = req;
#ifdef PICO_ON_DEVICE
        alarm_id_ = add_alarm_in_us(req.duration_us, alarm_callback_wrapper, this, true);
#else
        TimerResult result{};
        result.status = TimerStatus::Ok;
        result.timestamp_us = get_time_us() + req.duration_us;
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
#endif
    }

private:
#ifdef PICO_ON_DEVICE
    static int64_t alarm_callback_wrapper(alarm_id_t id, void* user_data) {
        (void)id;
        auto* self = static_cast<PicoTimer*>(user_data);
        TimerResult result{};
        result.status = TimerStatus::Ok;
        result.timestamp_us = self->get_time_us();

        TimerRequest req = self->active_req_;
        self->push_completion_from_isr(req, result);
        self->set_hardware_idle_from_isr();

        TimerRequest next_req{};
        if (self->pop_next_request_from_isr(next_req)) {
            self->start_hardware_transfer_from_isr(next_req);
        }
        return 0;
    }

    alarm_id_t alarm_id_{0};
#endif
    TimerRequest active_req_{};
};

// ----------------------------------------------------------------------------
// PicoGpio: RP2350 GPIO & Edge Interrupt Driver (IGpio)
// ----------------------------------------------------------------------------
class PicoGpio : public IGpio {
public:
    PicoGpio() = default;

    void configure_pin(uint32_t pin, PinMode mode, PinPull pull = PinPull::None) override;
    void write_pin(uint32_t pin, bool level) override;
    void toggle_pin(uint32_t pin) override;
    bool read_pin(uint32_t pin) override;
    bool configure_interrupt(uint32_t pin, EdgeTrigger trigger, GpioInterruptHandler handler, void* context) override;
    void enable_interrupt(uint32_t pin, bool enable) override;
};

// ----------------------------------------------------------------------------
// PicoI2c: RP2350 Hardware I2C0 Driver (II2c)
// ----------------------------------------------------------------------------
class PicoI2c : public II2c {
public:
    PicoI2c() = default;

    bool init(const I2cConfig& config) override;
    bool set_frequency(uint32_t frequency_hz) override;

    bool write_read_sync(uint8_t slave_addr,
                         std::span<const uint8_t> tx_data,
                         std::span<uint8_t> rx_data) override;

    bool write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) override;
    bool read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) override;

    bool is_hardware_busy() const noexcept { return busy_; }

protected:
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override;

private:
    I2cConfig config_{};
    I2cRequest active_req_{};
    volatile bool busy_{false};
};

PicoI2c& get_pico_i2c() noexcept;
II2c& get_i2c_driver() noexcept;
II2c& get_i2c() noexcept;

PicoGpio& get_pico_gpio() noexcept;
IGpio& get_gpio_driver() noexcept;
IGpio& get_gpio() noexcept;

} // namespace abstractx::hal
