/*
 * Copyright (C) 2026 Tim Michals
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Guard: sim tests are built with assert() live, also in Release (sim/CMakeLists.txt -UNDEBUG).
 * Fails to compile if NDEBUG reaches the sim directory again, and fails at run time if assert
 * does nothing.
 */
// @impl [SPEC-AUDIT-15] docs/SPECTRACE.md
#ifdef NDEBUG
#error "sim tests must be built with assert() enabled (sim/CMakeLists.txt add_compile_options(-UNDEBUG))"
#endif

#include <cassert>
#include <csignal>
#include <cstdio>
#include <cstdlib>

int main() {
    std::signal(SIGABRT, [](int) { std::printf("[PASS] assert() is live in sim tests\n"); std::_Exit(0); });
    assert(false && "must abort");
    std::printf("[FAIL] assert(false) returned: assertions are compiled out\n");
    return 1;
}
