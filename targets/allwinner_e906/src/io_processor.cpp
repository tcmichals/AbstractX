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
#include "hal/uart.hpp"
#include "hal/pio.hpp"
#include "hal/dma.hpp"
#include "hal/msgbox.hpp"
#include "hal/timer.hpp"
#include "hal/gpio_irq.hpp"
#include "hal/auto_dma.hpp"
#include "abstractx/interrupt_lock.hpp"

#include <atomic>

namespace abstractx::target {

// @impl [SPEC-ARCH-13] docs/DESIGN_SPECIFICATION.md
class E906IoBackend final : public hal::IIoBackend {
public:
    void attach(hal::IoEngine& engine) noexcept { engine_ = &engine; }

    // A GPIO-edge channel arms its pin ([SPEC-E906-IRQ-07]); pin = bank * 32 + n (PI8 = 264).
    // @impl [SPEC-E906-HAL-05] targets/allwinner_e906/SPECIFICATION.md
    bool init_channel(const hal::AutoChannelConfig& ch) noexcept override {
        if (!dma_ready_) {
            // DMA channels 8..15; the message box belongs to Rpmsg ([SPEC-E906-IRQ-04]).
            ::hal::DmaController::init();
            dma_ready_ = true;
        }
        if (ch.trigger_mode == hal::TriggerMode::GpioEdge) {
            if (!::hal::gpio_irq::arm(ch.trigger_pin, ch.trigger_rising, &E906IoBackend::on_edge, this)) {
                return false;
            }
            drdy_pin_ = ch.trigger_pin;
            drdy_bound_ = true;
        }
        if (ch.bus_type == hal::BusType::Spi) {
            fc::hal::Spi1::init(ch.bus_speed_hz ? ch.bus_speed_hz : 10'000'000);
            spi_ready_ = true;
        } else if (ch.bus_type == hal::BusType::I2c) {
            hal::I2cConfig cfg{};
            cfg.frequency_hz = ch.bus_speed_hz ? ch.bus_speed_hz : 400'000;
            i2c_port(ch.bus_index).init(cfg);
            i2c_ready_[ch.bus_index & 1U] = true;
        }
        return true;
    }

    // SPI: DMA with completion from the DMA ISR. I2C: async request, completion from the TWI interrupt
    // with the rule-8 deadline ([SPEC-E906-HAL-04] rule 9): never a sync call, an auto channel starts
    // from the DRDY edge handler with interrupts masked.
    // SPI always by DMA here, also below DMA_THRESHOLD: the auto read runs from the edge
    // handler and must not keep it busy for the burst ([SPEC-E906-HAL-05]).
    // The E906 owns the sensor bus and Linux sees results ([SPEC-ARCH-14]; built on the E906 only).
    // @impl [SPEC-ARCH-14] docs/DESIGN_SPECIFICATION.md
    bool start_transfer(const hal::IoTransfer& x) noexcept override {
        if (x.bus == hal::BusType::Spi) {
            if (!spi_ready_) {
                fc::hal::Spi1::init(10'000'000);
                spi_ready_ = true;
            }
            if (fc::hal::Spi1::is_busy()) {                // one DMA at a time on SPI1: drop
                if (in_edge_) ++stats_.busy_drops;
                return false;
            }
            spi_done_ = x.done;
            spi_len_ = x.rx.size();
            spi_t0_ = ::hal::Timer::get_time_ns();
            const uint32_t start = mcycle_lo();
            const bool ok = fc::hal::Spi1::start_dma_transfer(
                x.device /* chip select: the IMU is CS0, PB11 */, x.tx.data(), x.rx.data(), x.rx.size(),
                etl::delegate<void(bool)>::create<E906IoBackend, &E906IoBackend::on_spi_done>(*this));
            if (!ok) {
                spi_done_ = {};
                if (in_edge_) ++stats_.busy_drops;
                return false;
            }
            dma_start_ = start;
            if (in_edge_) stats_.edge_to_start.add(start - edge_cycles_);
            return true;
        }
        if (x.bus == hal::BusType::I2c) {
            // @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (rule 9: async engine I2C)
            const uint8_t idx = x.bus_index & 1U;
            auto& bus = i2c_port(idx);
            if (!i2c_ready_[idx]) {
                hal::I2cConfig cfg{};
                cfg.frequency_hz = 400'000;
                bus.init(cfg);
                i2c_ready_[idx] = true;
            }
            I2cJob* j = alloc_i2c_job();
            if (j == nullptr) {                 // every job in flight: refuse, never wait
                if (in_edge_) ++stats_.busy_drops;
                return false;
            }
            j->done = x.done;
            hal::I2cRequest req{};
            req.slave_addr = x.device;
            req.tx_data = x.tx;                 // engine-owned buffers: live until the completion
            req.rx_data = x.rx;
            req.repeated_start = !x.tx.empty() && !x.rx.empty();
            req.callback = etl::delegate<void(const hal::I2cResult&)>::create<I2cJob, &I2cJob::on_done>(*j);
            if (!bus.submit_request(req)) {
                j->in_use.store(false, std::memory_order_release);
                if (in_edge_) ++stats_.busy_drops;
                return false;
            }
            return true;
        }
        return false;
    }

    // GPIO bridge and LED BAR refused on the E906: they acted on Port C, which Linux owns, with
    // read-modify-writes; the E906 owns no free header GPIO ([SPEC-E906-HAL-04] GPIO bridge).
    // @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (GPIO bridge and LED BAR)
    bool gpio(const asp_tlp_gpio_req_header_t& req, uint8_t& level) noexcept override {
        (void)req;
        level = 0;
        return false;
    }

    uint32_t read_reg(uint32_t addr) noexcept override {
        (void)addr;
        return 0;
    }
    void write_reg(uint32_t addr, uint32_t value) noexcept override {
        (void)addr;
        (void)value;
    }

    size_t serial_xfer(uint8_t /*port*/, uint32_t baud_hz,
                       std::span<const uint8_t> tx,
                       std::span<uint8_t> rx) noexcept override {
        // @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (rule 10: never waits)
        auto& uart = hal::get_e906_uart3();
        const uint32_t want = baud_hz != 0 ? baud_hz : (uart_ready_ ? uart_baud_ : 115200U);
        if (!uart_ready_ || want != uart_baud_) {   // re-program only on a change (keeps the RX ring)
            uart.init(want);
            uart.set_dma(true);
            uart_baud_ = want;
            uart_ready_ = true;
        }
        if (!tx.empty()) {
            (void)uart.queue_tx(tx);                // what does not fit is counted in tx_dropped()
        }
        if (!rx.empty()) {
            return uart.read(rx);
        }
        return 0;
    }

    void notify_host() noexcept override { ::hal::MsgBox::send(::hal::MsgBox::Channel::Channel0, 0x01); }
    void idle_wait() noexcept override { __asm__ volatile("wfi"); }
    uint64_t now_ns() noexcept override { return ::hal::Timer::get_time_ns(); }

    void on_stop() noexcept override {
        if (drdy_bound_) ::hal::gpio_irq::disarm(drdy_pin_);
    }

    void on_drdy_from_isr() noexcept {
        if (engine_ && drdy_bound_) on_edge(this, drdy_pin_, mcycle_lo());
    }

    ::hal::auto_dma::Stats stats() const noexcept {
        abstractx::InterruptGuard guard;
        return stats_;
    }
    void reset_stats() noexcept {
        abstractx::InterruptGuard guard;
        stats_ = {};
    }

private:
    static inline uint32_t mcycle_lo() noexcept {
        uint32_t v = 0;
#if defined(__riscv)
        asm volatile("csrr %0, mcycle" : "=r"(v));
#endif
        return v;
    }

    // Edge handler context ([SPEC-E906-IRQ-07]): never resumes, only starts the transfer.
    static void on_edge(void* ctx, uint32_t gpio, uint32_t edge_cycles) noexcept {
        auto& self = *static_cast<E906IoBackend*>(ctx);
        if (!self.engine_) return;
        self.edge_cycles_ = edge_cycles;
        ++self.stats_.edges;
        self.in_edge_ = true;
        self.engine_->on_trigger(gpio, ::hal::Timer::get_time_ns());
        self.in_edge_ = false;
    }

    void on_spi_done(bool success) noexcept {
        if (success) ++stats_.completed; else ++stats_.failed;
        stats_.dma.add(mcycle_lo() - dma_start_);
        hal::IoResult r{};
        r.ok = success;
        r.transferred = success ? spi_len_ : 0;
        r.timestamp_ns = spi_t0_;
        auto cb = spi_done_;
        spi_done_ = {};
        if (cb.is_valid()) cb(r);
    }

    // One in-flight engine I2C transfer per job; the job holds the engine's completion
    // ([SPEC-E906-HAL-04] rule 9). shared: start_transfer (main or edge ISR) claims, the TWI completion
    // releases; atomic in_use.
    struct I2cJob {
        std::atomic<bool> in_use{false};
        etl::delegate<void(const hal::IoResult&)> done{};
        void on_done(const hal::I2cResult& r) noexcept {
            hal::IoResult out{};
            out.ok = r.status == hal::I2cStatus::Ok;
            out.transferred = out.ok ? r.transferred_bytes : 0;
            out.timestamp_ns = r.timestamp_us * 1000ULL;
            auto cb = done;
            done = {};
            in_use.store(false, std::memory_order_release);
            if (cb.is_valid()) cb(out);
        }
    };
    I2cJob* alloc_i2c_job() noexcept {
        for (auto& j : i2c_jobs_) {
            bool expected = false;
            if (j.in_use.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return &j;
        }
        return nullptr;
    }
    I2cJob i2c_jobs_[4]{};

    static hal::E906I2c& i2c_port(uint8_t idx) noexcept {
        // On the Radxa Cubie A5E, the external sensor header is TWI1 (PB4/PB5).
        return (idx == 0 || idx == 1) ? hal::get_e906_twi1() : hal::get_e906_i2c();
    }

    hal::IoEngine* engine_{nullptr};
    bool dma_ready_{false};
    bool spi_ready_{false};
    bool i2c_ready_[2]{false, false};
    bool uart_ready_{false};
    uint32_t uart_baud_{0};
    bool drdy_bound_{false};
    uint32_t drdy_pin_{0};
    etl::delegate<void(const hal::IoResult&)> spi_done_{};   // shared: SPI DMA ISR clears; one transfer in flight
    size_t spi_len_{0};
    uint64_t spi_t0_{0};
    // shared: edge and DMA handlers write (interrupts do not nest), stats() copies under InterruptGuard
    ::hal::auto_dma::Stats stats_{};
    uint32_t edge_cycles_{0};   // shared: edge handler only
    uint32_t dma_start_{0};     // shared: written before the DMA starts, read by its completion
    bool in_edge_{false};       // shared: edge handler only
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

namespace hal::auto_dma {

// @impl [SPEC-E906-HAL-05] targets/allwinner_e906/SPECIFICATION.md
Stats stats() noexcept { return abstractx::target::g_e906_io.backend.stats(); }
void reset() noexcept { abstractx::target::g_e906_io.backend.reset_stats(); }

} // namespace hal::auto_dma

// DRDY hook for code that takes the edge itself; the GPIO-edge channel arms PI8 through
// hal::gpio_irq instead ([SPEC-E906-IRQ-07]).
extern "C" __attribute__((section(".fastcode")))
void fc_gpio_drdy_isr() noexcept {
    abstractx::target::g_e906_io.backend.on_drdy_from_isr();
}
