// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// Linux IGpio: libgpiod v2 edge interrupts on an edge thread ([SPEC-LINUX-GPIO-02]).
#pragma once

#include "abstractx/hal/gpio.hpp"
#include "abstractx/hal/platform.hpp"
#include "abstractx/trace/tracer.hpp"
#include "abstractx/wake.hpp"
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
    ~LinuxGpioDriver() override { stop_edge_thread(); }

    LinuxGpioDriver(const LinuxGpioDriver&) = delete;
    LinuxGpioDriver& operator=(const LinuxGpioDriver&) = delete;

    // Levels are kept in memory only ([SPEC-LINUX-GPIO-02] not covered: no gpiod output request).
    void configure_pin(uint32_t pin, PinMode mode, PinPull pull = PinPull::None) override {
        (void)pull;
        if (pin < pin_modes_.size()) pin_modes_[pin] = mode;
    }
    void write_pin(uint32_t pin, bool level) override {
        if (pin < pin_levels_.size()) {
            pin_levels_[pin] = level;
            trace::g_tracer.trace_hal(4 /* GPIO */, 1, 1, 0, 0, get_timer_driver().get_time_us());
        }
    }
    void toggle_pin(uint32_t pin) override {
        if (pin < pin_levels_.size()) {
            pin_levels_[pin] = !pin_levels_[pin];
            trace::g_tracer.trace_hal(4 /* GPIO */, 1, 1, 0, 0, get_timer_driver().get_time_us());
        }
    }
    bool read_pin(uint32_t pin) override { return pin < pin_levels_.size() ? pin_levels_[pin] : false; }

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

    void stop_edge_thread() noexcept {
        if (!thread_.joinable()) return;
        running_.store(false, std::memory_order_release);
        stop_fd_.notify(1);
        thread_.join();
    }

private:
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
    std::array<bool, 64>    pin_levels_{};
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
