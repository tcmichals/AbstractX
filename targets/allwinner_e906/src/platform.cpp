/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Allwinner XuanTie E906 Platform Hooks & Driver Factories
 */

#include "abstractx/hal/platform.hpp"
#include "abstractx/domain_dispatcher.hpp"
#include "abstractx/interrupt_lock.hpp"
#include "hal/idle_policy.hpp"
#include "hal/ccu.hpp"
#include "hal/timer.hpp"
#include "hal/uart.hpp"
#include "hal/spi.hpp"
#include "hal/i2c.hpp"
#include "hal/pio.hpp"
#include "hal/msgbox.hpp"
#include "hal/clic.hpp"
#include "hal/rpmsg_endpoints.hpp"
#include "resource_table.h"
#include "abstractx/abstractx.hpp"
#include "abstractx/trace/runtime_stats.hpp"
#include "abstractx/trace/event_trace.hpp"

extern "C" const struct rpmsg_resource_table global_resource_table;

namespace abstractx::hal {

// @impl [SPEC-HAL-02] targets/SPECIFICATION.md#42-spi-interface-ispi--asyncspidriver
// @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
// SPI HAL Adapter for E906
class E906SpiDriver final : public ISpi {
public:
    E906SpiDriver() = default;

    bool init(const SpiConfig& config) override {
        fc::hal::Spi1::init(config.frequency_hz ? config.frequency_hz : 10'000'000);
        return true;
    }

    void select(bool active) override {
        fc::hal::Pio::set_cs0(active);
    }


protected:
    // @impl [SPEC-E906-HAL-01] targets/allwinner_e906/SPECIFICATION.md
    // @impl [SPEC-E906-HAL-03] targets/allwinner_e906/SPECIFICATION.md
    void start_hardware_transfer_from_isr(const SpiRequest& req) noexcept override {
        active_req_ = req;
        const size_t len = req.tx_data.empty() ? req.rx_data.size() : req.tx_data.size();
        const bool dual = req.is_dual_spi;
        if (!fc::hal::Spi1::start_dma_transfer(
            0 /* CS0: PB11 */,
            req.tx_data.data(),
            req.rx_data.data(),
            len,
            etl::delegate<void(bool)>::create<E906SpiDriver, &E906SpiDriver::on_dma_done>(*this),
            dual
        )) {
            on_dma_done(false);
        }
    }

private:
    SpiRequest active_req_{};

    void on_dma_done(bool success) noexcept {
        SpiResult res{};
        res.status = success ? SpiStatus::Ok : SpiStatus::DmaError;
        res.transferred_bytes = active_req_.tx_data.empty() ? active_req_.rx_data.size() : active_req_.tx_data.size();
        res.timestamp_us = ::hal::Timer::get_time_us();
        push_completion_from_isr(active_req_, res);
        active_req_ = {};
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
    // @impl [SPEC-E906-IRQ-03] targets/allwinner_e906/SPECIFICATION.md
    void start_hardware_transfer_from_isr(const TimerRequest& req) noexcept override {
        active_req_ = req;
        if (req.duration_us == 0) {
            on_alarm(this);
            return;
        }
        waiter_.target_ticks = ::hal::Timer::get_ticks() +
                               static_cast<uint64_t>(req.duration_us) * ::hal::Timer::TICKS_PER_US;
        waiter_.handle = nullptr;
        waiter_.callback = &E906TimerDriver::on_alarm;
        waiter_.ctx = this;
        ::hal::Timer::schedule_waiter(&waiter_);
    }

    bool abort_active_from_isr() noexcept override {
        ::hal::Timer::cancel_waiter(&waiter_);
        push_completion_from_isr(active_req_, make_cancelled_result<TimerResult>());
        active_req_ = {};
        return true;
    }

private:
    static void on_alarm(void* ctx) noexcept {
        auto* self = static_cast<E906TimerDriver*>(ctx);
        TimerResult res{};
        res.status = TimerStatus::Ok;
        res.timestamp_us = self->get_time_us();
        self->push_completion_from_isr(self->active_req_, res);
        self->active_req_ = {};
        self->set_hardware_idle_from_isr();
    }

    TimerRequest               active_req_{};
    ::hal::Timer::SleepAwaiter waiter_{};
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
static E906TimerDriver g_e906_timer;
static E906GpioDriver  g_e906_gpio;

ISpi& get_spi_driver() noexcept {
    return g_e906_spi;
}

IUart& get_uart_driver() noexcept {
    return get_e906_uart3();
}

// @impl [SPEC-E906-HAL-04] targets/allwinner_e906/SPECIFICATION.md (port list)
IUart* get_uart_port(uint8_t index) noexcept {
    if (index == 0 || index == 3) {
        return &get_e906_uart3();  // UART2's PB0/PB1 are not connected on the Cubie A5E
    }
    return nullptr;
}

II2c* get_i2c_port(uint8_t bus) noexcept {
    if (bus == 0) {
        return &get_e906_i2c();
    }
    if (bus == 1) {
        return &get_e906_twi1();
    }
    return nullptr;
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
::hal::RpmsgTlpChannel g_tlp_channel{g_rpmsg};

void platform_init() noexcept {
#if defined(__riscv)
    uint32_t mtvec = 0;
    __asm__ volatile("csrr %0, mtvec" : "=r"(mtvec));
    g_e906_diag[3] = mtvec;
#endif
    ::hal::Ccu::init();
    ::hal::Timer::init();
    // Runtime status clock: mcycle at 200 MHz ([SPEC-E906-IRQ-05], [SPEC-TRACE-08])
    stats::set_cycle_clock(+[]() noexcept -> uint64_t {
        uint32_t hi0 = 0, hi1 = 0, lo = 0;
#if defined(__riscv)
        do {
            __asm__ volatile("csrr %0, mcycleh" : "=r"(hi0));
            __asm__ volatile("csrr %0, mcycle" : "=r"(lo));
            __asm__ volatile("csrr %0, mcycleh" : "=r"(hi1));
        } while (hi0 != hi1);
#endif
        return (static_cast<uint64_t>(hi0) << 32) | lo;
    }, 200'000'000u);
    ::hal::irq::describe(::hal::clic::kLineTimer, "timer");
    ::hal::irq::describe(::hal::clic::kLineMsgbox, "msgbox");
    g_rpmsg.init(&global_resource_table);
    g_trace_transport.attach();
    g_tlp_channel.attach();
}

void platform_launch_processing_domain(void (*entry)()) noexcept {
    if (entry) {
        entry();
    }
}

// @impl [SPEC-ARCH-06] docs/DESIGN_SPECIFICATION.md#spec-arch-06
// @impl [SPEC-E906-IRQ-01] targets/allwinner_e906/SPECIFICATION.md
// Sleep with MIE set, so nothing depends on wfi waking while interrupts are
// masked (not verified on the E906). Race-free: check with MIE clear, then enable
// and wfi; an interrupt that arrives in between is taken on the wfi itself and the
// trap entry (startup.S) returns past it. noinline: the label must be unique.
__attribute__((noinline)) void platform_idle_wait() noexcept {
    const interrupt_flags_t flags = disable_interrupts_save_flags();
    // @impl [SPEC-E906-IRQ-04] targets/allwinner_e906/SPECIFICATION.md (kicks on + ring re-check before wfi)
    if (!Dispatcher::has_pending_resumes() && !::hal::rpmsg_doorbell_pending() &&
        !::hal::rpmsg_pending_before_sleep()) {
        g_e906_diag[0] = g_e906_diag[0] + 1u;
        g_e906_diag[4] = *reinterpret_cast<volatile uint32_t*>(::hal::clic::kIntBase + 4U * ::hal::clic::kLineMsgbox);
        g_e906_diag[5] = *reinterpret_cast<volatile uint32_t*>(::hal::clic::kIntBase + 4U * ::hal::clic::kLineTimer);
        uint32_t mie = 0, mstatus = 0;
        __asm__ volatile("csrr %0, mie" : "=r"(mie));
        __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
        g_e906_diag[6] = mie;
        g_e906_diag[7] = mstatus;
        // Wake for the next status report even if nothing else happens ([SPEC-E906-IRQ-05]),
        // sooner while a kick is owed: nothing else would retry it ([SPEC-E906-IPC-02]).
        // @impl [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
        const uint64_t due_us = ::hal::idle::wake_deadline_us(::hal::Timer::get_time_us(),
                                                              status_deadline_us(), g_rpmsg.kick_owed());
        if (due_us != UINT64_MAX) {
            ::hal::Timer::arm_wake(due_us * ::hal::Timer::TICKS_PER_US);
        }
        stats::idle_begin();  // idle = this window minus the ISR cycles taken in it
        // @impl [SPEC-E906-IRQ-06] targets/allwinner_e906/SPECIFICATION.md (still asleep)
        g_idle_sleeping = 1u;  // interrupts masked; any real interrupt's trap return clears it
        __asm__ volatile(
            "csrs mstatus, %0\n"
            ".global e906_idle_wfi\n"
            "e906_idle_wfi:\n"
            "wfi\n"
            :: "r"(1U << 3) : "memory");
        g_idle_sleeping = 0u;
        stats::idle_end();
    }
    restore_interrupts_flags(flags);
}

// @impl [SPEC-E906-IRQ-04] targets/allwinner_e906/SPECIFICATION.md
void platform_poll_network() noexcept {
    // No network stack on the coprocessor: announcements, host messages only
    // after a message box interrupt, then any partial TLP batch so TLPs never
    // wait for a full message.
    g_tlp_channel.service();
    g_trace_transport.service();
    if (::hal::rpmsg_take_doorbell()) {
        (void)g_rpmsg.poll();  // irq-gated: only after a message box interrupt
    }
    ::hal::rpmsg_arm_doorbell();
    g_tlp_channel.flush();
    g_rpmsg.flush_kick();  // a kick the full FIFO refused is retried, never spun on ([SPEC-E906-IPC-02])
    poll_i2c_timeouts();   // async I2C deadlines ([SPEC-E906-HAL-04] rule 8)
}

// shared: telemetry producers increment, platform_poll_network reads; volatile counter
volatile uint32_t g_telemetry_dropped{0};

// @impl [SPEC-E906-IPC-04] [SPEC-TRACE-07] targets/allwinner_e906/SPECIFICATION.md
// Profiler and timeline rings in SRAM Space 1 (.sram_c, NOLOAD: no zeroing needed, the rings
// track their own fill) ([SPEC-E906-IRQ-06], [SPEC-E906-MEM-01]).
// @impl [SPEC-E906-IRQ-06] targets/allwinner_e906/SPECIFICATION.md
namespace {
__attribute__((section(".sram_c"))) events::ProfileSample s_profile_ring[256];
__attribute__((section(".sram_c"))) events::TimelineEvent s_timeline_ring[512];
}

void platform_event_buffers(EventBuffers& out) noexcept {
    out = EventBuffers{s_profile_ring, 256, s_timeline_ring, 512};
}

// @impl [SPEC-E906-IPC-05] targets/allwinner_e906/SPECIFICATION.md
bool platform_trace_listening() noexcept {
    return g_trace_transport.listening();
}

void platform_profile_start(uint32_t hz) noexcept {
    ::hal::Timer::set_sample_period(hz ? ::hal::Timer::CLOCK_FREQ_HZ / hz : 0);
}

trace::ITraceTransport* platform_trace_transport() noexcept {
    return &g_trace_transport;
}

// Callers hand over already-built 64-byte TLPs (e.g. gps_imu_app): each goes
// into a slot of the TLP endpoint's current batch (one 64-byte copy). Lengths
// that are not a whole number of TLPs are dropped and counted.
// @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
void platform_send_telemetry(const uint8_t* data, size_t len) noexcept {
    constexpr size_t kTlp = ::hal::RpmsgTlpChannel::kTlpBytes;
    if (!data || len == 0 || len % kTlp != 0) {
        g_telemetry_dropped = g_telemetry_dropped + 1u;
        return;
    }
    for (size_t off = 0; off < len; off += kTlp) {
        asp_tlp64_unaligned_t* slot = g_tlp_channel.emplace();
        if (!slot) {
            g_telemetry_dropped = g_telemetry_dropped + 1u;
            return;
        }
        // Caller's bytes may be unaligned: copy byte-wise into the slot.
        auto* d = reinterpret_cast<uint8_t*>(slot);
        for (size_t i = 0; i < kTlp; ++i) {
            d[i] = data[off + i];
        }
    }
}

} // namespace abstractx::hal
