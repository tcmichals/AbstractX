// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// Linux IGpio: libgpiod v2 edge interrupts on an edge thread ([SPEC-LINUX-GPIO-02]).
#pragma once

#include "abstractx/hal/gpio.hpp"
#include "abstractx/hal/platform.hpp"
#include "abstractx/trace/tracer.hpp"
#include "abstractx/wake.hpp"
#include "bus_sim.hpp"
#include "eventfd.hpp"
#include "gpio_gpiod.hpp"

#include <poll.h>

#include <array>
#include <atomic>
#include <thread>

namespace abstractx::hal {

// @impl [SPEC-LINUX-GPIO-02] targets/linux/SPECIFICATION.md
class LinuxGpioDriver : public IGpio {
public:
    explicit LinuxGpioDriver(const char* chip_path = "/dev/gpiochip0") noexcept : chip_path_(chip_path) {}
    ~LinuxGpioDriver() override {
        stop_edge_thread();
        release_lines();
    }

    LinuxGpioDriver(const LinuxGpioDriver&) = delete;
    LinuxGpioDriver& operator=(const LinuxGpioDriver&) = delete;

    // Pin I/O through libgpiod v2, one request per line ([SPEC-LINUX-GPIO-02] pin I/O). No chip / line:
    // nothing happens and io_errors() counts it; memory levels only with ABSTRACTX_SIM_BUS=1.
    void configure_pin(uint32_t pin, PinMode mode, PinPull pull = PinPull::None) override {
        if (pin >= kPins) return;
        if (mode != PinMode::Input && mode != PinMode::Output) {   // not a Linux GPIO function
            io_errors_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        pin_modes_[pin] = mode;
        (void)request_line(pin, mode == PinMode::Output, pull, pin_levels_[pin]);
    }
    void write_pin(uint32_t pin, bool level) override {
        if (pin >= kPins) return;
        pin_levels_[pin] = level;
        if (!set_line(pin, level)) return;
        trace::g_tracer.trace_hal(4 /* GPIO */, 1, 1, 0, 0, get_timer_driver().get_time_us());
    }
    void toggle_pin(uint32_t pin) override {
        if (pin < kPins) write_pin(pin, !pin_levels_[pin]);
    }
    bool read_pin(uint32_t pin) override {
        if (pin >= kPins) return false;
#if defined(ABSTRACTX_HAS_LIBGPIOD_V2) && ABSTRACTX_HAS_LIBGPIOD_V2
        if (lines_[pin] == nullptr) (void)request_line(pin, false, PinPull::None, false);
        if (lines_[pin] != nullptr) {
            const auto v = ::gpiod_line_request_get_value(lines_[pin], pin);
            if (v != GPIOD_LINE_VALUE_ERROR) return v == GPIOD_LINE_VALUE_ACTIVE;
        }
#endif
        if (linux_bus_sim_allowed()) return pin_levels_[pin];
        io_errors_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    uint32_t io_errors() const noexcept { return io_errors_.load(std::memory_order_relaxed); }

    // Opens the line and starts the edge thread; a running thread is stopped first.
    bool configure_interrupt(uint32_t pin, EdgeTrigger trigger, GpioInterruptHandler handler, void* context) override {
        stop_edge_thread();
        const bool rising = trigger == EdgeTrigger::Rising || trigger == EdgeTrigger::Both;
        const bool falling = trigger == EdgeTrigger::Falling || trigger == EdgeTrigger::Both;
        if (handler == nullptr || !monitor_.open_pin_interrupt(chip_path_, pin, rising, falling)) return false;
        handler_ = handler;
        context_ = context;
        stop_fd_ = target::EventFd(true);
        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this] { edge_loop(); });
        return true;
    }

    void enable_interrupt(uint32_t pin, bool enable) override {
        (void)pin;
        enabled_.store(enable, std::memory_order_release);
    }

    target::GpiodV2Monitor& monitor() noexcept { return monitor_; }
    uint64_t edges_delivered() const noexcept { return delivered_.load(std::memory_order_relaxed); }

    void release_lines() noexcept {
#if defined(ABSTRACTX_HAS_LIBGPIOD_V2) && ABSTRACTX_HAS_LIBGPIOD_V2
        for (auto& l : lines_) {
            if (l) ::gpiod_line_request_release(l);
            l = nullptr;
        }
        if (chip_) ::gpiod_chip_close(chip_);
        chip_ = nullptr;
#endif
    }

    void stop_edge_thread() noexcept {
        if (!thread_.joinable()) return;
        running_.store(false, std::memory_order_release);
        stop_fd_.notify(1);
        thread_.join();
    }

private:
    static constexpr uint32_t kPins = 64;

    // Request one line (re-request with the new direction). false: no chip or line; counted.
    bool request_line(uint32_t pin, bool output, PinPull pull, bool level) noexcept {
#if defined(ABSTRACTX_HAS_LIBGPIOD_V2) && ABSTRACTX_HAS_LIBGPIOD_V2
        if (lines_[pin]) {
            ::gpiod_line_request_release(lines_[pin]);
            lines_[pin] = nullptr;
        }
        if (!chip_) chip_ = ::gpiod_chip_open(chip_path_);
        if (chip_) {
            gpiod_line_settings* st = ::gpiod_line_settings_new();
            gpiod_line_config* lc = ::gpiod_line_config_new();
            gpiod_request_config* rc = ::gpiod_request_config_new();
            if (st && lc && rc) {
                ::gpiod_line_settings_set_direction(st, output ? GPIOD_LINE_DIRECTION_OUTPUT : GPIOD_LINE_DIRECTION_INPUT);
                if (output) ::gpiod_line_settings_set_output_value(st, level ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
                ::gpiod_line_settings_set_bias(st, pull == PinPull::PullUp ? GPIOD_LINE_BIAS_PULL_UP
                                               : pull == PinPull::PullDown ? GPIOD_LINE_BIAS_PULL_DOWN
                                                                           : GPIOD_LINE_BIAS_AS_IS);
                const unsigned int off = pin;
                ::gpiod_line_config_add_line_settings(lc, &off, 1, st);
                ::gpiod_request_config_set_consumer(rc, "AbstractX");
                lines_[pin] = ::gpiod_chip_request_lines(chip_, rc, lc);
            }
            if (rc) ::gpiod_request_config_free(rc);
            if (lc) ::gpiod_line_config_free(lc);
            if (st) ::gpiod_line_settings_free(st);
            if (lines_[pin]) {
                line_is_output_[pin] = output;
                return true;
            }
        }
#else
        (void)output; (void)pull; (void)level;
#endif
        if (!linux_bus_sim_allowed()) io_errors_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool set_line(uint32_t pin, bool level) noexcept {
#if defined(ABSTRACTX_HAS_LIBGPIOD_V2) && ABSTRACTX_HAS_LIBGPIOD_V2
        if (lines_[pin] == nullptr || !line_is_output_[pin]) (void)request_line(pin, true, PinPull::None, level);
        if (lines_[pin] != nullptr && line_is_output_[pin]) {
            return ::gpiod_line_request_set_value(lines_[pin], pin,
                       level ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE) == 0;
        }
#else
        // @impl [SPEC-LINUX-GPIO-02] targets/linux/SPECIFICATION.md (no libgpiod: no request_line to count it)
        if (!linux_bus_sim_allowed()) io_errors_.fetch_add(1, std::memory_order_relaxed);
#endif
        return linux_bus_sim_allowed();   // simulated level kept in pin_levels_ only on request
    }

    // The Linux "GPIO interrupt": runs the handler per edge, never resumes, then wakes the loop.
    void edge_loop() noexcept {
        while (running_.load(std::memory_order_acquire)) {
            pollfd fds[2]{};
            fds[0].fd = monitor_.get_fd();
            fds[0].events = POLLIN;
            fds[1].fd = stop_fd_.fd();
            fds[1].events = POLLIN;
            if (::poll(fds, 2, -1) <= 0) continue;
            if (fds[1].revents & POLLIN) break;
            if (!(fds[0].revents & POLLIN)) continue;
            bool any = false;
            monitor_.process_events([this, &any](unsigned int line, uint64_t /*ts_ns*/) {
                if (!enabled_.load(std::memory_order_acquire)) return;   // drained, not delivered
                handler_(line, context_);
                delivered_.fetch_add(1, std::memory_order_relaxed);
                any = true;
            });
            if (any) abstractx::wake();
        }
    }

    const char* chip_path_;
    std::array<PinMode, 64> pin_modes_{};
    std::array<bool, 64>    pin_levels_{};          // last level written (simulated levels when allowed)
    std::array<bool, 64>    line_is_output_{};
#if defined(ABSTRACTX_HAS_LIBGPIOD_V2) && ABSTRACTX_HAS_LIBGPIOD_V2
    gpiod_chip*             chip_{nullptr};
    std::array<gpiod_line_request*, 64> lines_{};
#endif
    std::atomic<uint32_t>   io_errors_{0};       // shared: main increments, any reads; atomic
    target::GpiodV2Monitor  monitor_{};
    // shared: configure_interrupt (main, edge thread stopped) writes; edge thread reads; set before start
    GpioInterruptHandler    handler_{nullptr};
    void*                   context_{nullptr};
    std::atomic<bool>       enabled_{false};    // shared: main writes, edge thread reads; atomic
    std::atomic<bool>       running_{false};    // shared: main writes, edge thread reads; atomic
    std::atomic<uint64_t>   delivered_{0};      // shared: edge thread writes, any reads; atomic
    target::EventFd         stop_fd_{true};
    std::thread             thread_{};
};

} // namespace abstractx::hal
