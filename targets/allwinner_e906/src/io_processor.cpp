/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E906: I/O processor back end.
 * The shared IoEngine ([SPEC-ARCH-13]) decodes TLPs; this file maps its transfers onto SPI1
 * DMA and TWI, its trigger onto the DRDY interrupt hook, and its host notification onto the
 * MSGBOX doorbell.
 */

#include "abstractx/hal/io_engine.hpp"
#include "hal/spi.hpp"
#include "hal/i2c.hpp"
#include "hal/pio.hpp"
#include "hal/dma.hpp"
#include "hal/msgbox.hpp"
#include "hal/timer.hpp"

namespace abstractx::target {

// @impl [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md
class E906IoBackend final : public hal::IIoBackend {
public:
    void attach(hal::IoEngine& engine) noexcept { engine_ = &engine; }

    // One DRDY line feeds the hook below: the first GPIO-edge channel names its pin.
    bool init_channel(const hal::AutoChannelConfig& ch) noexcept override {
        if (!dma_ready_) {
            // DMA channels 8..15; the message box belongs to Rpmsg ([SPEC-E906-IRQ-04]).
            ::hal::DmaController::init();
            dma_ready_ = true;
        }
        if (ch.trigger_mode == hal::TriggerMode::GpioEdge && !drdy_bound_) {
            drdy_pin_ = ch.trigger_pin;
            drdy_bound_ = true;
        }
        if (ch.bus_type == hal::BusType::Spi) {
            fc::hal::Spi0::init(ch.bus_speed_hz ? ch.bus_speed_hz : 20'000'000);
        } else if (ch.bus_type == hal::BusType::I2c) {
            hal::I2cConfig cfg{};
            cfg.frequency_hz = ch.bus_speed_hz ? ch.bus_speed_hz : 400'000;
            i2c_.init(cfg);
            i2c_ready_ = true;
        }
        return true;
    }

    // SPI: DMA with completion from the DMA ISR. I2C: E906I2c only has blocking calls, so the
    // transfer completes before returning ([SPEC-ARCH-13] 5; listed in the target spec).
    bool start_transfer(const hal::IoTransfer& x) noexcept override {
        if (x.bus == hal::BusType::Spi) {
            if (fc::hal::Spi0::is_busy()) return false;   // one DMA at a time on SPI1
            spi_done_ = x.done;
            spi_len_ = x.rx.size();
            spi_t0_ = ::hal::Timer::get_time_ns();
            const bool ok = fc::hal::Spi0::start_dma_transfer(
                1 /* CS1 */, x.tx.data(), x.rx.data(), x.rx.size(),
                etl::delegate<void(bool)>::create<E906IoBackend, &E906IoBackend::on_spi_done>(*this));
            if (!ok) spi_done_ = {};
            return ok;
        }
        if (x.bus == hal::BusType::I2c) {
            if (!i2c_ready_) {
                hal::I2cConfig cfg{};
                cfg.frequency_hz = 400'000;
                i2c_.init(cfg);
                i2c_ready_ = true;
            }
            bool ok = true;
            if (!x.tx.empty() && !x.rx.empty()) ok = i2c_.write_read_sync(x.device, x.tx, x.rx);
            else if (!x.rx.empty())             ok = i2c_.read_sync(x.device, x.rx);
            else if (!x.tx.empty())             ok = i2c_.write_sync(x.device, x.tx);
            hal::IoResult r{};
            r.ok = ok;
            r.transferred = ok ? x.rx.size() : 0;
            r.timestamp_ns = ::hal::Timer::get_time_ns();
            x.done(r);
            return true;
        }
        return false;
    }

    bool gpio(const asp_tlp_gpio_req_header_t& req, uint8_t& level) noexcept override {
        const uint32_t bits = req.mask ? req.mask : (1U << req.pin);   // pin < 32 checked by the engine
        switch (req.cmd) {
        case ASP_GPIO_CMD_SET:   PC_DATA_REG |= bits; return true;
        case ASP_GPIO_CMD_CLR:   PC_DATA_REG &= ~bits; return true;
        case ASP_GPIO_CMD_XOR:   PC_DATA_REG ^= bits; return true;
        case ASP_GPIO_CMD_WRITE:
            if (req.value) PC_DATA_REG |= (1U << req.pin); else PC_DATA_REG &= ~(1U << req.pin);
            return true;
        case ASP_GPIO_CMD_READ:
            level = (PC_DATA_REG & (1U << req.pin)) ? 1 : 0;
            return true;
        default:
            return false;
        }
    }

    // Virtual BAR: the Radxa Cubie A5E status LED (PC1).
    uint32_t read_reg(uint32_t addr) noexcept override {
        if (addr >= bar::LedBase && addr < bar::LedBase + 0x1000) return (PC_DATA_REG & (1U << 1)) ? 1U : 0U;
        return 0;
    }
    void write_reg(uint32_t addr, uint32_t value) noexcept override {
        if (addr >= bar::LedBase && addr < bar::LedBase + 0x1000) {
            if (value != 0) PC_DATA_REG |= (1U << 1); else PC_DATA_REG &= ~(1U << 1);
        }
    }

    void notify_host() noexcept override { ::hal::MsgBox::send(::hal::MsgBox::Channel::Channel0, 0x01); }
    void idle_wait() noexcept override { __asm__ volatile("wfi"); }
    uint64_t now_ns() noexcept override { return ::hal::Timer::get_time_ns(); }

    void on_drdy_from_isr() noexcept {
        if (engine_ && drdy_bound_) engine_->on_trigger(drdy_pin_, ::hal::Timer::get_time_ns());
    }

private:
    void on_spi_done(bool success) noexcept {
        hal::IoResult r{};
        r.ok = success;
        r.transferred = success ? spi_len_ : 0;
        r.timestamp_ns = spi_t0_;
        auto cb = spi_done_;
        spi_done_ = {};
        if (cb.is_valid()) cb(r);
    }

    hal::IoEngine* engine_{nullptr};
    hal::E906I2c& i2c_ = hal::get_e906_i2c();  // the TWI0 port object, never a second driver ([SPEC-E906-HAL-04])
    bool dma_ready_{false};
    bool i2c_ready_{false};
    bool drdy_bound_{false};
    uint32_t drdy_pin_{0};
    etl::delegate<void(const hal::IoResult&)> spi_done_{};   // shared: SPI DMA ISR clears; one transfer in flight
    size_t spi_len_{0};
    uint64_t spi_t0_{0};
};

struct E906IoProcessor {
    E906IoBackend backend{};
    hal::IoEngine engine{backend};
    E906IoProcessor() noexcept { backend.attach(engine); }
};

static E906IoProcessor g_e906_io;

} // namespace abstractx::target

namespace abstractx::hal {

IIoProcessor& get_target_io_processor() noexcept {
    return target::g_e906_io.engine;
}

} // namespace abstractx::hal

// DRDY interrupt hook. Not yet attached to a CLIC line (TODO.md: E906 DRDY routing).
extern "C" __attribute__((section(".fastcode")))
void fc_gpio_drdy_isr() noexcept {
    abstractx::target::g_e906_io.backend.on_drdy_from_isr();
}
