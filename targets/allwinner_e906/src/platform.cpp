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

// UART HAL Adapter for E906 (UART2 Navigation/CRSF)
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

// UART3 HAL Adapter for E906 (u-blox NEO-M8N GPS on 40-Pin Header, Pins 16/31, PI11/PI12)
class E906Uart3Driver final : public IUart {
public:
    static constexpr uintptr_t kUart3Base = 0x02500C00U;

    E906Uart3Driver() = default;

    void init(uint32_t baudrate) override {
        // 1. Enable CCU bus clock gating & deassert reset
        ::hal::Ccu::enable_module(::hal::Ccu::BusModule::Uart3);

        // 2. Configure PIO pinmux: PI11 (TX) and PI12 (RX) -> Function 4 (UART3)
        constexpr uintptr_t kPio = 0x02000000U;
        auto& pi_cfg1 = *reinterpret_cast<volatile uint32_t*>(kPio + 0x0124U);
        pi_cfg1 = (pi_cfg1 & ~0x000FF000U) | 0x00044000U;

        // 3. Configure UART baud rate divisor
        auto& u_lcr = *reinterpret_cast<volatile uint32_t*>(kUart3Base + 0x0C);
        auto& u_rbr = *reinterpret_cast<volatile uint32_t*>(kUart3Base + 0x00);
        auto& u_ier = *reinterpret_cast<volatile uint32_t*>(kUart3Base + 0x04);
        auto& u_fcr = *reinterpret_cast<volatile uint32_t*>(kUart3Base + 0x08);

        u_lcr = 0x83; // 8-N-1, DLAB=1
        uint32_t baud = baudrate ? baudrate : 115200;
        uint32_t div = 24000000U / (16U * baud);
        u_rbr = div & 0xFF;        // DLL
        u_ier = (div >> 8) & 0xFF; // DLH
        u_lcr = 0x03; // DLAB=0, 8-N-1

        // FIFO on, reset RX/TX
        u_fcr = 0x07;
    }

    void write_byte(uint8_t ch) override {
        auto& u_lsr = *reinterpret_cast<volatile uint32_t*>(kUart3Base + 0x14);
        auto& u_thr = *reinterpret_cast<volatile uint32_t*>(kUart3Base + 0x00);
        while ((u_lsr & (1U << 5)) == 0) {}
        u_thr = ch;
    }

    void puts(const char* str) override {
        if (!str) return;
        while (*str) {
            write_byte(static_cast<uint8_t>(*str++));
        }
    }

    size_t write(std::span<const uint8_t> buffer) override {
        for (uint8_t b : buffer) {
            write_byte(b);
        }
        return buffer.size();
    }

    bool read_byte(uint8_t& ch) override {
        auto& u_lsr = *reinterpret_cast<volatile uint32_t*>(kUart3Base + 0x14);
        if ((u_lsr & 0x01) == 0) return false;
        auto& u_rbr = *reinterpret_cast<volatile uint32_t*>(kUart3Base + 0x00);
        ch = static_cast<uint8_t>(u_rbr & 0xFF);
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

// TWI1 HAL Adapter for E906 (HMC5883L/QMC5883L Compass on 40-Pin Header, Pins 3/5, PB0/PB1)
class E906Twi1Driver final : public II2c {
public:
    static constexpr uintptr_t kTwi1Base = 0x02502400U;

    E906Twi1Driver() = default;

    bool init(const I2cConfig& config) override {
        (void)config;
        constexpr uintptr_t kCcu = 0x02001000U;
        constexpr uintptr_t kPio = 0x02000000U;
        auto& ccu_twi_bgr = *reinterpret_cast<volatile uint32_t*>(kCcu + 0x091CU);
        auto& pb_cfg0 = *reinterpret_cast<volatile uint32_t*>(kPio + 0x0030U);

        // Deassert TWI1 reset (bit 17) and enable TWI1 bus gate (bit 1)
        ccu_twi_bgr |= (1U << 17) | (1U << 1);

        // Configure PB0 (TWI1_SCK) and PB1 (TWI1_SDA) -> Function 4
        pb_cfg0 = (pb_cfg0 & ~0x000000FFU) | 0x00000044U;

        auto& twi_srst = *reinterpret_cast<volatile uint32_t*>(kTwi1Base + 0x18);
        auto& twi_ccr  = *reinterpret_cast<volatile uint32_t*>(kTwi1Base + 0x14);
        auto& twi_cntr = *reinterpret_cast<volatile uint32_t*>(kTwi1Base + 0x0C);

        twi_srst = 1;
        ::hal::Timer::delay_ms(1);
        twi_srst = 0;

        // 400 kHz @ 24 MHz APB: div_m = 2, div_n = 2
        twi_ccr = (2U << 3) | 2U;
        twi_cntr = (1U << 6); // Bus Enable (BUSEN)
        return true;
    }

    bool set_frequency(uint32_t frequency_hz) override {
        auto& twi_ccr = *reinterpret_cast<volatile uint32_t*>(kTwi1Base + 0x14);
        if (frequency_hz >= 400'000) {
            twi_ccr = (2U << 3) | 2U;
        } else {
            twi_ccr = (5U << 3) | 2U;
        }
        return true;
    }

    I2cCapabilities capabilities() const noexcept override {
        I2cCapabilities caps{};
        caps.max_hz = 400'000;
        caps.max_msgs = 2;
        caps.max_msg_bytes = 64;
        return caps;
    }

    bool write_read_sync(uint8_t slave_addr,
                         std::span<const uint8_t> tx_data,
                         std::span<uint8_t> rx_data) override {
        auto& twi_data = *reinterpret_cast<volatile uint32_t*>(kTwi1Base + 0x08);
        auto& twi_cntr = *reinterpret_cast<volatile uint32_t*>(kTwi1Base + 0x0C);
        auto& twi_stat = *reinterpret_cast<volatile uint32_t*>(kTwi1Base + 0x10);

        // START
        twi_cntr = (1U << 6) | (1U << 5) | (1U << 3);
        if (!wait_int()) return false;
        uint32_t st = twi_stat & 0xFF;
        if (st != 0x08 && st != 0x10) return false;

        // SLA + W
        twi_data = (slave_addr << 1);
        twi_cntr = (1U << 6) | (1U << 3);
        if (!wait_int() || (twi_stat & 0xFF) != 0x18) return false;

        // Write bytes
        for (uint8_t b : tx_data) {
            twi_data = b;
            twi_cntr = (1U << 6) | (1U << 3);
            if (!wait_int() || (twi_stat & 0xFF) != 0x28) return false;
        }

        if (!rx_data.empty()) {
            // REPEATED START
            twi_cntr = (1U << 6) | (1U << 5) | (1U << 3);
            if (!wait_int()) return false;
            st = twi_stat & 0xFF;
            if (st != 0x10 && st != 0x08) return false;

            // SLA + R
            twi_data = (slave_addr << 1) | 1U;
            twi_cntr = (1U << 6) | (1U << 3);
            if (!wait_int()) return false;

            for (size_t i = 0; i < rx_data.size(); ++i) {
                bool ack = (i + 1 < rx_data.size());
                twi_cntr = (1U << 6) | (ack ? (1U << 2) : 0U) | (1U << 3);
                if (!wait_int()) return false;
                rx_data[i] = static_cast<uint8_t>(twi_data & 0xFF);
            }
        }

        // STOP
        twi_cntr = (1U << 6) | (1U << 4) | (1U << 3);
        return true;
    }

    bool write_sync(uint8_t slave_addr, std::span<const uint8_t> tx_data) override {
        return write_read_sync(slave_addr, tx_data, {});
    }

    bool read_sync(uint8_t slave_addr, std::span<uint8_t> rx_data) override {
        return write_read_sync(slave_addr, {}, rx_data);
    }

protected:
    void start_hardware_transfer_from_isr(const I2cRequest& req) noexcept override {
        uint8_t addr = req.address ? static_cast<uint8_t>(req.address & 0x7F) : req.slave_addr;
        I2cResult res{};
        bool ok = false;
        if (!req.msgs.empty()) {
            if (req.msgs.size() == 2 && !req.msgs[0].is_read() && req.msgs[1].is_read()) {
                ok = write_read_sync(addr, req.msgs[0].tx, req.msgs[1].rx);
                res.transferred_bytes = ok ? (req.msgs[0].tx.size() + req.msgs[1].rx.size()) : 0;
                res.msgs_done = ok ? 2 : 0;
            } else if (req.msgs.size() == 1) {
                if (req.msgs[0].is_read()) {
                    ok = read_sync(addr, req.msgs[0].rx);
                    res.transferred_bytes = ok ? req.msgs[0].rx.size() : 0;
                    res.msgs_done = ok ? 1 : 0;
                } else {
                    ok = write_sync(addr, req.msgs[0].tx);
                    res.transferred_bytes = ok ? req.msgs[0].tx.size() : 0;
                    res.msgs_done = ok ? 1 : 0;
                }
            }
        } else {
            if (!req.tx_data.empty() && !req.rx_data.empty()) {
                ok = write_read_sync(addr, req.tx_data, req.rx_data);
                res.transferred_bytes = ok ? (req.tx_data.size() + req.rx_data.size()) : 0;
            } else if (!req.tx_data.empty()) {
                ok = write_sync(addr, req.tx_data);
                res.transferred_bytes = ok ? req.tx_data.size() : 0;
            } else if (!req.rx_data.empty()) {
                ok = read_sync(addr, req.rx_data);
                res.transferred_bytes = ok ? req.rx_data.size() : 0;
            }
        }
        res.status = ok ? I2cStatus::Ok : I2cStatus::BusError;
        res.timestamp_us = ::hal::Timer::get_time_us();
        push_completion_from_isr(req, res);
        set_hardware_idle_from_isr();
    }

private:
    static bool wait_int() noexcept {
        auto& twi_cntr = *reinterpret_cast<volatile uint32_t*>(kTwi1Base + 0x0C);
        uint64_t deadline = ::hal::Timer::get_time_us() + 10000ULL;
        while (!(twi_cntr & (1U << 3))) {
            if (::hal::Timer::get_time_us() >= deadline) return false;
        }
        return true;
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
static E906UartDriver  g_e906_uart;
static E906Uart3Driver g_e906_uart3;
static E906TimerDriver g_e906_timer;
static E906GpioDriver  g_e906_gpio;
static E906Twi1Driver  g_e906_twi1;

ISpi& get_spi_driver() noexcept {
    return g_e906_spi;
}

IUart& get_uart_driver() noexcept {
    return g_e906_uart;
}

IUart* get_uart_port(uint8_t index) noexcept {
    if (index == 0 || index == 2) {
        return &g_e906_uart;
    }
    if (index == 3) {
        return &g_e906_uart3;
    }
    return nullptr;
}

II2c* get_i2c_port(uint8_t bus) noexcept {
    if (bus == 0) {
        return &get_e906_i2c();
    }
    if (bus == 1) {
        return &g_e906_twi1;
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
    if (!Dispatcher::has_pending_resumes() && !::hal::rpmsg_doorbell_pending()) {
        g_e906_diag[0] = g_e906_diag[0] + 1u;
        g_e906_diag[4] = *reinterpret_cast<volatile uint32_t*>(::hal::clic::kIntBase + 4U * ::hal::clic::kLineMsgbox);
        g_e906_diag[5] = *reinterpret_cast<volatile uint32_t*>(::hal::clic::kIntBase + 4U * ::hal::clic::kLineTimer);
        uint32_t mie = 0, mstatus = 0;
        __asm__ volatile("csrr %0, mie" : "=r"(mie));
        __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus));
        g_e906_diag[6] = mie;
        g_e906_diag[7] = mstatus;
        // Wake for the next status report even if nothing else happens ([SPEC-E906-IRQ-05]).
        const uint64_t due_us = status_deadline_us();
        if (due_us != UINT64_MAX) {
            ::hal::Timer::arm_wake(due_us * ::hal::Timer::TICKS_PER_US);
        }
        stats::idle_begin();  // idle = this window minus the ISR cycles taken in it
        __asm__ volatile(
            "csrs mstatus, %0\n"
            ".global e906_idle_wfi\n"
            "e906_idle_wfi:\n"
            "wfi\n"
            :: "r"(1U << 3) : "memory");
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
