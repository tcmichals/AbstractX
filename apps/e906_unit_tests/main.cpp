// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// E906 unit-test firmware ([SPEC-AUDIT-18]): runs tests/unit/all_tests.cpp on the processor and writes
// the results to remoteproc trace0, ending with "UNIT: <n> passed, <m> failed". Then it idles.
#include "all_tests.hpp"
#include "hal/trace.hpp"

extern "C" {
void trace_init(void);
void trace_puts(const char* s);
}

// @impl [SPEC-AUDIT-18] docs/SPECTRACE.md (E906 firmware: results to trace0)
int main(void) {
    trace_init();
    trace_puts("[E906] unit tests\n");
    (void)abstractx::test::run_all(abx_unit::kAllTests, abx_unit::kAllTestsCount, &trace_puts);
    for (;;) {
#if defined(__riscv)
        asm volatile("wfi");
#endif
    }
}
