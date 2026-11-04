/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Test: Pico 2 W non-blocking Wi-Fi join state machine
 * @verifies [SPEC-PICO-NET-01] targets/pico2w_rp2350/SPECIFICATION.md
 */

#include "hal/wifi_join.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>

using abstractx::hal::pico::WifiJoinAction;
using abstractx::hal::pico::WifiJoinMachine;
namespace link = abstractx::hal::pico::wifi_link;
using State = WifiJoinMachine::State;

namespace {

int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                        \
        }                                                                        \
    } while (0)

// Drives the machine in 10 ms ticks like abstractx::step() would; collects actions.
struct Driver {
    WifiJoinMachine m;
    uint32_t now{0};
    int joins_started{0};
    int leaves{0};
    int led_writes{0};
    bool led{false};

    void apply(const WifiJoinAction& a) {
        if (a.start_join) ++joins_started;
        if (a.leave) ++leaves;
        if (a.led_changed) { ++led_writes; led = a.led_on; }
        if (a.led_changed) CHECK(a.led_on == m.led());
        CHECK(led == m.led());
    }
    void start() { apply(m.start(now)); }
    void run(uint32_t ms, int status) {
        for (uint32_t t = 0; t < ms; t += 10) {
            now += 10;
            apply(m.step(now, status));
        }
    }
};

void test_idle_does_nothing() {
    WifiJoinMachine m;
    const WifiJoinAction a = m.step(1000, link::kUp);
    CHECK(!a.start_join && !a.leave && !a.led_changed);
    CHECK(m.state() == State::Idle);
}

void test_join_success_turns_led_on() {
    Driver d;
    d.start();
    CHECK(d.joins_started == 1);
    CHECK(d.m.state() == State::Joining);
    d.run(3000, link::kJoin);
    CHECK(d.m.state() == State::Joining);
    CHECK(d.led_writes >= 4);  // blinking while joining (500 ms period)
    d.run(200, link::kUp);
    CHECK(d.m.state() == State::Up);
    CHECK(d.led);
    const int writes = d.led_writes;
    d.run(10000, link::kUp);
    CHECK(d.led_writes == writes);  // steady on: no further LED writes
    CHECK(d.m.joins() == 1);
    CHECK(d.joins_started == 1);
}

void test_each_negative_status_retries() {
    for (int status : {link::kFail, link::kNoNet, link::kBadAuth}) {
        Driver d;
        d.start();
        d.run(200, status);
        CHECK(d.m.state() == State::WaitRetry);
        CHECK(d.leaves == 1);
        CHECK(!d.led || d.m.state() == State::WaitRetry);
        d.run(WifiJoinMachine::kRetryDelayMs - 300, link::kDown);
        CHECK(d.joins_started == 1);  // not before the retry delay
        d.run(300, link::kDown);
        CHECK(d.joins_started == 2);
        CHECK(d.m.state() == State::Joining);
        d.run(200, link::kUp);
        CHECK(d.m.state() == State::Up);
        CHECK(d.led);
        CHECK(d.m.attempts() == 2);
    }
}

void test_attempt_timeout_retries() {
    Driver d;
    d.start();
    d.run(WifiJoinMachine::kAttemptTimeoutMs - 200, link::kJoin);
    CHECK(d.m.state() == State::Joining);
    d.run(300, link::kJoin);
    CHECK(d.m.state() == State::WaitRetry);
    CHECK(d.leaves == 1);
}

void test_down_while_joining_is_not_failure() {
    // Right after leave/connect the driver may still report DOWN; only the timeout fails it.
    Driver d;
    d.start();
    d.run(5000, link::kDown);
    CHECK(d.m.state() == State::Joining);
    CHECK(d.leaves == 0);
}

void test_link_loss_rejoins() {
    for (int status : {link::kDown, link::kFail}) {
        Driver d;
        d.start();
        d.run(200, link::kUp);
        CHECK(d.m.state() == State::Up);
        d.run(200, status);
        CHECK(d.m.state() == State::WaitRetry);
        CHECK(d.leaves == 1);
        CHECK(!d.led);
        d.run(WifiJoinMachine::kRetryDelayMs + 200, link::kJoin);
        CHECK(d.joins_started == 2);
        d.run(200, link::kUp);
        CHECK(d.m.joins() == 2);
    }
}

void test_noip_is_still_joining() {
    Driver d;
    d.start();
    d.run(5000, link::kNoIp);
    CHECK(d.m.state() == State::Joining);
    d.run(200, link::kUp);
    CHECK(d.m.state() == State::Up);
}

void test_poll_rate_limit() {
    WifiJoinMachine m;
    (void)m.start(0);
    // Failure seen inside the 100 ms window is ignored until the window passes.
    WifiJoinAction a = m.step(50, link::kBadAuth);
    CHECK(!a.leave);
    CHECK(m.state() == State::Joining);
    a = m.step(WifiJoinMachine::kPollPeriodMs, link::kBadAuth);
    CHECK(a.leave && a.log_failed && a.failed_status == link::kBadAuth);
}

void test_ms_wraparound() {
    Driver d;
    d.now = 0xFFFFFFFFu - 1000u;
    d.start();
    d.run(3000, link::kJoin);  // crosses the 32-bit wrap
    CHECK(d.m.state() == State::Joining);
    d.run(WifiJoinMachine::kAttemptTimeoutMs - 3000 + 200, link::kJoin);  // 20.2 s total
    CHECK(d.m.state() == State::WaitRetry);
}

void test_steady_state_many_failures() {
    // 1000 failed attempts, then success: counters advance, actions stay one per transition.
    Driver d;
    d.start();
    for (int i = 0; i < 1000; ++i) {
        d.run(200, link::kBadAuth);
        d.run(WifiJoinMachine::kRetryDelayMs + 100, link::kDown);
    }
    CHECK(d.m.attempts() == 1001);
    CHECK(d.leaves == 1000);
    CHECK(d.joins_started == 1001);
    d.run(200, link::kUp);
    CHECK(d.m.state() == State::Up);
    CHECK(d.led);
}

} // namespace

int main() {
    test_idle_does_nothing();
    test_join_success_turns_led_on();
    test_each_negative_status_retries();
    test_attempt_timeout_retries();
    test_down_while_joining_is_not_failure();
    test_link_loss_rejoins();
    test_noip_is_still_joining();
    test_poll_rate_limit();
    test_ms_wraparound();
    test_steady_state_many_failures();
    if (g_failures != 0) {
        std::printf("test_pico_wifi_join: %d failure(s)\n", g_failures);
        return EXIT_FAILURE;
    }
    std::printf("test_pico_wifi_join: all checks passed\n");
    return EXIT_SUCCESS;
}
