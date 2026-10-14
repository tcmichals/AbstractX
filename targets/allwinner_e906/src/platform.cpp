/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E906 Platform Hooks & Driver Factories
 */

#include "abstractx/hal/platform.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/interrupt_lock.hpp"
#include "hal/ccu.hpp"
#include "hal/timer.hpp"
#include "hal/uart.hpp"
#include "hal/spi.hpp"
#include "hal/i2c.hpp"
#include "hal/pio.hpp"
#include "hal/msgbox.hpp"
#include "hal/rpmsg_trace_transport.hpp"
#include "resource_table.h"

extern "C" const struct rpmsg_resource_table global_resource_table;

namespace abstractx::hal {

// SPI HAL Adapter for E906
class E906SpiDriver final : public ISpi {
public:
    E906SpiDriver() = default;

    bool init(const SpiConfig& config) override {
        fc::hal::Spi0::init(config.frequency_hz ? config.frequency_hz : 20'000'000);
        return true;
    }

    void select(bool active) override {
        fc::hal::Pio::set_cs1(active);
    }

    uint8_t transfer_byte(uint8_t tx) override {
        uint8_t rx = 0;
        fc::hal::Spi0::transceive_imu_single_sync(&tx, &rx, 1);
        return rx;
    }

    bool transfer_sync(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) override {
        size_t len = tx_data.empty() ? rx_data.size() : tx_data.size();
        return fc::hal::Spi0::transceive_imu_single_sync(tx_data.data(), rx_data.data(), len);
    }

protected:
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        size_t len = req.tx_data.empty() ? req.rx_data.size() : req.tx_data.size();
        fc::hal::Spi0::start_dma_transfer(
            1 /* CS1 */,
            req.tx_data.data(),
            req.rx_data.data(),
            len,
            etl::delegate<void(bool)>::create<E906SpiDriver, &E906SpiDriver::on_dma_done>(*this)
        );
    }

private:
    void on_dma_done(bool success) noexcept {
        SpiResult res{};
        res.status = success ? SpiStatus::Ok : SpiStatus::DmaError;
        SpiRequest req{};
        if (pop_next_request_from_isr(req)) {
            push_completion_from_isr(req, res);
        }
        set_hardware_idle_from_isr();
    }
};

// UART HAL Adapter for E906
class E906UartDriver final : public IUart {
public:
    E906UartDriver() = default;

    void init(uint32_t baudrate) override {
        fc::hal::Uart2::init(baudrate);
    }

    void write_byte(uint8_t ch) override {
        fc::hal::Uart2::write_byte(ch);
    }

    void puts(const char* str) override {
        fc::hal::Uart2::write_str(str);
    }

    size_t write(std::span<const uint8_t> buffer) override {
        fc::hal::Uart2::write(buffer.data(), buffer.size());
        return buffer.size();
    }

    bool read_byte(uint8_t& ch) override {
        if (!fc::hal::Uart2::has_data()) return false;
        ch = fc::hal::Uart2::read_byte();
        return true;
    }

    size_t read(std::span<uint8_t> buffer) override {
        size_t count = 0;
        for (auto& b : buffer) {
            if (!read_byte(b)) break;
            count++;
        }
        return count;
    }

    void flush() override {}

protected:
    void start_hardware_transfer_from_isr(const UartTxRequest& req) noexcept override {
        write(req.data);
        UartResult res{};
        res.status = UartStatus::Ok;
        res.bytes_transferred = req.data.size();
        push_completion_from_isr(req, res);
        set_hardware_idle_from_isr();
    }
};

// Timer HAL Adapter for E906
class E906TimerDriver final : public ITimer {
public:
    E906TimerDriver() = default;

    void delay_us(uint32_t us) override {
        ::hal::Timer::delay_us(us);
    }

    void delay_ms(uint32_t ms) override {
        ::hal::Timer::delay_ms(ms);
    }

    uint64_t get_time_us() const override {
        return ::hal::Timer::get_time_ns() / 1000ULL;
    }

    uint32_t get_time_ms() const override {
        return static_cast<uint32_t>(::hal::Timer::get_time_ns() / 1'000'000ULL);
    }

protected:
    void start_hardware_transfer_from_isr(const TimerRequest& req) noexcept override {
        TimerResult res{};
        res.status = TimerStatus::Ok;
        res.timestamp_us = get_time_us() + req.duration_us;
        push_completion_from_isr(req, res);
        set_hardware_idle_from_isr();
    }
};

// GPIO HAL Adapter for E906
class E906GpioDriver final : public IGpio {
public:
    E906GpioDriver() = default;

    void configure_pin(uint32_t pin, PinMode mode, PinPull pull = PinPull::None) override {
        (void)pin; (void)mode; (void)pull;
    }

    void write_pin(uint32_t pin, bool level) override {
        if (pin == 3) fc::hal::Pio::set_cs0(level);
        else if (pin == 7) fc::hal::Pio::set_cs1(level);
    }

    void toggle_pin(uint32_t pin) override {
        write_pin(pin, !read_pin(pin));
    }

    bool read_pin(uint32_t pin) override {
        if (pin == 6) return fc::hal::Pio::get_fpga_frame_ready();
        if (pin == 8) return fc::hal::Pio::get_imu_drdy();
        return false;
    }

    bool configure_interrupt(uint32_t pin, EdgeTrigger trigger, GpioInterruptHandler handler, void* context) override {
        (void)pin; (void)trigger; (void)handler; (void)context;
        return true;
    }

    void enable_interrupt(uint32_t pin, bool enable) override {
        (void)pin; (void)enable;
    }
};

static E906SpiDriver   g_e906_spi;
static E906UartDriver  g_e906_uart;
static E906TimerDriver g_e906_timer;
static E906GpioDriver  g_e906_gpio;

ISpi& get_spi_driver() noexcept {
    return g_e906_spi;
}

IUart& get_uart_driver() noexcept {
    return g_e906_uart;
}

ITimer& get_timer_driver() noexcept {
    return g_e906_timer;
}

IGpio& get_gpio_driver() noexcept {
    return g_e906_gpio;
}

// RPMsg to Linux ([SPEC-E906-IPC-04]); ring state lives in rpmsg.cpp.
::hal::Rpmsg g_rpmsg;
::hal::RpmsgTraceTransport g_trace_transport{g_rpmsg};

void platform_init() noexcept {
    ::hal::Ccu::init();
    ::hal::Timer::init();
    g_rpmsg.init(&global_resource_table);
    g_trace_transport.attach();
}

void platform_launch_processing_domain(void (*entry)()) noexcept {
    if (entry) {
        entry();
    }
}

// @impl [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-06
void platform_idle_wait() noexcept {
    // Mask MIE, re-check, then wfi: an ISR that posts a resume between the
    // caller's check and the wfi would otherwise be slept through. wfi still
    // wakes on a pending interrupt while MIE is clear; it is taken on unmask.
    const interrupt_flags_t flags = disable_interrupts_save_flags();
    if (!Dispatcher::has_pending_resumes()) {
        __asm__ volatile("wfi");
    }
    restore_interrupts_flags(flags);
}

void platform_poll_network() noexcept {
    // No network stack on the coprocessor: service RPMsg (name-service
    // announcement, host messages) from the cooperative loop.
    g_trace_transport.service();
}

volatile uint32_t g_telemetry_dropped{0};

// @impl [SPEC-E906-IPC-04] [SPEC-TRACE-07] targets/allwinner_e906/SPECIFICATION.md
trace::ITraceTransport* platform_trace_transport() noexcept {
    return &g_trace_transport;
}

// Byte-buffer API for callers that already hold the data (e.g. a TLP built by
// an application): one copy into the RPMsg buffer. Trace packets do not use
// this path; they are written in place through platform_trace_transport().
// @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
void platform_send_telemetry(const uint8_t* data, size_t len) noexcept {
    if (!data || len == 0) {
        return;
    }
    std::span<uint8_t> buf = g_trace_transport.acquire(len);
    if (buf.size() < len) {
        if (!buf.empty()) {
            g_trace_transport.commit(buf, 0);
        }
        g_telemetry_dropped = g_telemetry_dropped + 1u;
        return;
    }
    for (size_t i = 0; i < len; ++i) {
        buf[i] = data[i];
    }
    g_trace_transport.commit(buf, len);
}

} // namespace abstractx::hal
