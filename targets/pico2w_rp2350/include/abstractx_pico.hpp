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
#include "tlp_ring.hpp"

#include <atomic>

#ifdef PICO_ON_DEVICE
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/sync.h"  // __sev() in the timer alarm ([SPEC-PICO-HAL-01])
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
// @impl [SPEC-HAL-02] targets/SPECIFICATION.md#42-spi-interface-ispi--asyncspidriver
class PicoSpi : public ISpi {
public:
    PicoSpi() = default;

    bool init(const SpiConfig& config) override;
    void select(bool active) override;

    bool start_dma_transfer_raw(const uint8_t* tx, uint8_t* rx, size_t len) noexcept;   // false: not started
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

    uint32_t tx_dropped() const noexcept { return tx_dropped_.load(std::memory_order_relaxed); }

protected:
    // [SPEC-PICO-HAL-01] UART send queue: completes with the bytes taken, QueueFull if not all fit.
    void start_hardware_transfer_from_isr(const UartTxRequest& req) noexcept override {
        const size_t taken = write(req.data);
        UartResult result{};
        result.status = taken == req.data.size() ? UartStatus::Ok : UartStatus::QueueFull;
        result.bytes_transferred = taken;
        push_completion_from_isr(req, result);
        set_hardware_idle_from_isr();
    }

private:
    abstractx::MpscRingBuffer<uint8_t, 512> rx_queue_{};
    abstractx::MpscRingBuffer<uint8_t, 512> tx_queue_{};
    std::atomic<uint32_t> tx_dropped_{0};   // shared: any writer increments, any thread reads; atomic
    bool queue_byte(uint8_t ch) noexcept;   // false: FIFO and queue full, byte dropped and counted
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
    // @impl [SPEC-HAL-05] docs/DESIGN_SPECIFICATION.md (Pico: Polled)
    // @impl [SPEC-PICO-HAL-01] targets/pico2w_rp2350/SPECIFICATION.md (timer)
    // One SDK alarm at the earliest deadline; its callback only signals, so the sleeps expire in
    // step() (service_due_us), never in the alarm interrupt.
    Alarm arm_alarm(uint64_t deadline_us) noexcept override {
#ifdef PICO_ON_DEVICE
        if (alarm_id_ > 0) cancel_alarm(alarm_id_);
        alarm_id_ = add_alarm_at(from_us_since_boot(deadline_us), alarm_callback, nullptr, true);
        return alarm_id_ < 0 ? Alarm::Unsupported : Alarm::Polled;  // 0: already fired (past)
#else
        (void)deadline_us;
        return Alarm::Polled;
#endif
    }

    void disarm_alarm() noexcept override {
#ifdef PICO_ON_DEVICE
        if (alarm_id_ > 0) cancel_alarm(alarm_id_);
        alarm_id_ = 0;
#endif
    }

private:
#ifdef PICO_ON_DEVICE
    static int64_t alarm_callback(alarm_id_t, void*) {
        __sev();  // end a wfe on either core; step() expires the sleeps
        abstractx::wake_from_isr();
        return 0;
    }

    alarm_id_t alarm_id_{0};  // shared: arm/disarm under InterruptGuard; the callback does not touch it
#endif
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
    // Ends a transfer past its deadline with Timeout ([SPEC-PICO-HAL-01]); main loop only.
    void poll_timeout() noexcept;

protected:
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override;

private:
    I2cConfig config_{};
    I2cRequest active_req_{};
    volatile bool busy_{false};
    
    // Async IRQ state tracking ([SPEC-PICO-HAL-01]); written only by the start path and I2C0_IRQ
    volatile size_t cmd_idx_{0};   // IC_DATA_CMD words queued (tx bytes, then read commands)
    volatile size_t rx_idx_{0};    // bytes drained into active_req_.rx_data
    // Deadline ([SPEC-PICO-HAL-01] I2C deadline). shared: start path / poll_timeout (under InterruptGuard)
    // and I2C0_IRQ (completion clears busy_); volatile, single core (Core 1)
    volatile uint64_t deadline_us_{0};
    int32_t deadline_alarm_{-1};    // SDK alarm id that wakes a wfe idle at the deadline

public:
#ifdef PICO_ON_DEVICE
    static void i2c0_irq_handler();
    void on_irq_from_isr() noexcept;
    static int64_t on_deadline_alarm(alarm_id_t id, void* user_data);
#endif
};

PicoI2c& get_pico_i2c() noexcept;
void poll_pico_i2c_timeouts() noexcept;   // called by platform_poll_network()
// The one PicoSpi for SPI1 ([SPEC-PICO-HAL-02]); get_spi_driver() returns the same object.
PicoSpi& get_pico_spi() noexcept;
II2c& get_i2c_driver() noexcept;
II2c& get_i2c() noexcept;

PicoGpio& get_pico_gpio() noexcept;
IGpio& get_gpio_driver() noexcept;
IGpio& get_gpio() noexcept;

} // namespace abstractx::hal
