/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef TARGETS_PICO2W_HAL_WIFI_JOIN_HPP
#define TARGETS_PICO2W_HAL_WIFI_JOIN_HPP

#include <cstdint>

namespace abstractx::hal::pico {

// @impl [SPEC-PICO-NET-01] targets/pico2w_rp2350/SPECIFICATION.md
// Values match CYW43_LINK_* from cyw43.h so the platform passes the status through.
namespace wifi_link {
inline constexpr int kDown    = 0;
inline constexpr int kJoin    = 1;
inline constexpr int kNoIp    = 2;
inline constexpr int kUp      = 3;
inline constexpr int kFail    = -1;
inline constexpr int kNoNet   = -2;
inline constexpr int kBadAuth = -3;
} // namespace wifi_link

struct WifiJoinAction {
    bool leave{false};        // cyw43_wifi_leave() before a new attempt
    bool start_join{false};   // cyw43_arch_wifi_connect_async()
    bool led_changed{false};  // write led_on to the LED
    bool led_on{false};
    bool log_joined{false};
    bool log_failed{false};   // attempt failed or timed out; status in failed_status
    bool log_lost{false};     // link dropped after UP
    int  failed_status{0};
};

// Pure state machine: no SDK calls, no allocation; time is a wrapping millisecond counter.
// @impl [SPEC-PICO-NET-01] targets/pico2w_rp2350/SPECIFICATION.md
class WifiJoinMachine {
public:
    static constexpr uint32_t kPollPeriodMs     = 100;
    static constexpr uint32_t kAttemptTimeoutMs = 20000;
    static constexpr uint32_t kRetryDelayMs     = 2000;
    static constexpr uint32_t kBlinkPeriodMs    = 500;

    enum class State : uint8_t { Idle, Joining, WaitRetry, Up };

    // First attempt: the caller has just started (or must start) the join.
    WifiJoinAction start(uint32_t now_ms) noexcept {
        WifiJoinAction a{};
        a.start_join = true;
        enter_joining(now_ms);
        set_led(a, true, now_ms);
        return a;
    }

    WifiJoinAction step(uint32_t now_ms, int link_status) noexcept {
        WifiJoinAction a{};
        if (state_ == State::Idle) return a;
        if (static_cast<uint32_t>(now_ms - last_poll_ms_) < kPollPeriodMs) return a;
        last_poll_ms_ = now_ms;

        switch (state_) {
        case State::Joining:
            if (link_status == wifi_link::kUp) {
                state_ = State::Up;
                a.log_joined = true;
                ++joins_;
                set_led(a, true, now_ms);
                return a;
            }
            if (link_status < 0 ||
                static_cast<uint32_t>(now_ms - attempt_start_ms_) >= kAttemptTimeoutMs) {
                a.log_failed = true;
                a.failed_status = link_status;
                enter_wait(a, now_ms);
                return a;
            }
            break;
        case State::WaitRetry:
            if (static_cast<uint32_t>(now_ms - retry_from_ms_) >= kRetryDelayMs) {
                a.start_join = true;
                enter_joining(now_ms);
            }
            break;
        case State::Up:
            if (link_status == wifi_link::kDown || link_status < 0) {
                a.log_lost = true;
                a.failed_status = link_status;
                enter_wait(a, now_ms);
                return a;
            }
            return a;  // LED steady on
        case State::Idle:
            break;
        }
        blink(a, now_ms);
        return a;
    }

    State    state() const noexcept { return state_; }
    uint32_t attempts() const noexcept { return attempts_; }
    uint32_t joins() const noexcept { return joins_; }
    bool     led() const noexcept { return led_; }

private:
    void enter_joining(uint32_t now_ms) noexcept {
        state_ = State::Joining;
        attempt_start_ms_ = now_ms;
        last_poll_ms_ = now_ms;
        ++attempts_;
    }
    void enter_wait(WifiJoinAction& a, uint32_t now_ms) noexcept {
        state_ = State::WaitRetry;
        retry_from_ms_ = now_ms;
        a.leave = true;
        set_led(a, false, now_ms);
    }
    void set_led(WifiJoinAction& a, bool on, uint32_t now_ms) noexcept {
        if (on != led_) {
            led_ = on;
            a.led_changed = true;
        }
        a.led_on = led_;
        led_toggle_ms_ = now_ms;
    }
    void blink(WifiJoinAction& a, uint32_t now_ms) noexcept {
        if (static_cast<uint32_t>(now_ms - led_toggle_ms_) >= kBlinkPeriodMs) {
            set_led(a, !led_, now_ms);
        }
    }

    State    state_{State::Idle};
    uint32_t attempt_start_ms_{0};
    uint32_t retry_from_ms_{0};
    uint32_t last_poll_ms_{0};
    uint32_t led_toggle_ms_{0};
    uint32_t attempts_{0};
    uint32_t joins_{0};
    bool     led_{false};
};

} // namespace abstractx::hal::pico

#endif // TARGETS_PICO2W_HAL_WIFI_JOIN_HPP
