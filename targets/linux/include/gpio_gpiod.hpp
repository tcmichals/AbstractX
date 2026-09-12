/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * AbstractX Target: libgpiod v2 Edge Detection & Event Wrapper
 */

#ifndef ABSTRACTX_TARGET_GPIO_GPIOD_HPP
#define ABSTRACTX_TARGET_GPIO_GPIOD_HPP

#if __has_include(<gpiod.h>)
#include <gpiod.h>
#define ABSTRACTX_HAVE_LIBGPIOD 1
#else
struct gpiod_chip;
struct gpiod_line_request;
struct gpiod_edge_event_buffer;
#endif

#include <cstdint>
#include <functional>
#include <atomic>
#include "eventfd.hpp"

namespace abstractx::target {

class GpiodV2Monitor {
public:
    GpiodV2Monitor() noexcept = default;

    ~GpiodV2Monitor() noexcept {
        close();
    }

    bool open_pin_interrupt(const char* chip_path, unsigned int line_offset, bool rising = true, bool falling = false) noexcept {
        close();

#ifdef ABSTRACTX_HAVE_LIBGPIOD
        if (!chip_path || chip_path[0] == '\0') {
            chip_path = "/dev/gpiochip0";
        }

        chip_ = ::gpiod_chip_open(chip_path);
        if (!chip_) {
            sim_eventfd_ = EventFd(true);
            return true;
        }

        struct gpiod_line_settings* settings = ::gpiod_line_settings_new();
        if (!settings) return false;

        ::gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
        
        enum gpiod_line_edge edge = GPIOD_LINE_EDGE_NONE;
        if (rising && falling) {
            edge = GPIOD_LINE_EDGE_BOTH;
        } else if (rising) {
            edge = GPIOD_LINE_EDGE_RISING;
        } else if (falling) {
            edge = GPIOD_LINE_EDGE_FALLING;
        }
        ::gpiod_line_settings_set_edge_detection(settings, edge);

        struct gpiod_line_config* line_cfg = ::gpiod_line_config_new();
        if (!line_cfg) {
            ::gpiod_line_settings_free(settings);
            return false;
        }

        unsigned int offsets[1] = {line_offset};
        ::gpiod_line_config_add_line_settings(line_cfg, offsets, 1, settings);
        ::gpiod_line_settings_free(settings);

        struct gpiod_request_config* req_cfg = ::gpiod_request_config_new();
        if (!req_cfg) {
            ::gpiod_line_config_free(line_cfg);
            return false;
        }
        ::gpiod_request_config_set_consumer(req_cfg, "AbstractX_DRDY");

        request_ = ::gpiod_chip_request_lines(chip_, req_cfg, line_cfg);
        ::gpiod_request_config_free(req_cfg);
        ::gpiod_line_config_free(line_cfg);

        if (!request_) {
            sim_eventfd_ = EventFd(true);
            return true;
        }

        event_buf_ = ::gpiod_edge_event_buffer_new(16);
        return true;
#else
        (void)chip_path; (void)line_offset; (void)rising; (void)falling;
        sim_eventfd_ = EventFd(true);
        return true;
#endif
    }

    void close() noexcept {
#ifdef ABSTRACTX_HAVE_LIBGPIOD
        if (event_buf_) {
            ::gpiod_edge_event_buffer_free(event_buf_);
            event_buf_ = nullptr;
        }
        if (request_) {
            ::gpiod_line_request_release(request_);
            request_ = nullptr;
        }
        if (chip_) {
            ::gpiod_chip_close(chip_);
            chip_ = nullptr;
        }
#endif
    }

    int get_fd() const noexcept {
#ifdef ABSTRACTX_HAVE_LIBGPIOD
        if (request_) {
            return ::gpiod_line_request_get_fd(request_);
        }
#endif
        return sim_eventfd_.fd();
    }

    bool is_simulated() const noexcept {
        return (request_ == nullptr);
    }

    template <typename Callback>
    int process_events(Callback&& cb) noexcept {
#ifdef ABSTRACTX_HAVE_LIBGPIOD
        if (request_ && event_buf_) {
            int count = ::gpiod_line_request_read_edge_events(request_, event_buf_, 16);
            if (count > 0) {
                for (int i = 0; i < count; ++i) {
                    struct gpiod_edge_event* ev = ::gpiod_edge_event_buffer_get_event(event_buf_, i);
                    if (ev) {
                        uint64_t ts_ns = ::gpiod_edge_event_get_timestamp_ns(ev);
                        unsigned int line = ::gpiod_edge_event_get_line_offset(ev);
                        cb(line, ts_ns);
                    }
                }
            }
            return count;
        }
#endif
        if (sim_eventfd_.is_valid()) {
            uint64_t val = sim_eventfd_.drain();
            if (val > 0) {
                cb(3, 0);
                return 1;
            }
        }
        return 0;
    }

    void trigger_simulated_edge() noexcept {
        if (sim_eventfd_.is_valid()) {
            sim_eventfd_.notify(1);
        }
    }

private:
    struct gpiod_chip*              chip_{nullptr};
    struct gpiod_line_request*      request_{nullptr};
    struct gpiod_edge_event_buffer* event_buf_{nullptr};
    EventFd                         sim_eventfd_{};

};

} // namespace abstractx::target

#endif // ABSTRACTX_TARGET_GPIO_GPIOD_HPP
