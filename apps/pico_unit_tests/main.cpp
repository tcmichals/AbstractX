// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// Pico 2 W unit-test firmware ([SPEC-AUDIT-18]): runs tests/unit/all_tests.cpp on the RP2350 and prints
// the results on the stdio console (UART1, GP8 TX / GP9 RX, 115200, [SPEC-PICO-HAL-03]), ending with
// "UNIT: <n> passed, <m> failed", once a second so a late terminal still sees it.
#include "all_tests.hpp"
#include "pico/stdlib.h"

#include <cstdio>

// @impl [SPEC-AUDIT-18] docs/SPECTRACE.md (Pico firmware: results on the console)
int main() {
    stdio_init_all();
    for (;;) {
        std::printf("[RP2350] unit tests\n");
        (void)abstractx::test::run_all(abx_unit::kAllTests, abx_unit::kAllTestsCount,
                                       [](const char* t) { std::fputs(t, stdout); });
        sleep_ms(1000);
    }
}
