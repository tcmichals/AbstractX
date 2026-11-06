// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// The portable unit tests on the host ([SPEC-AUDIT-18]); the E906 runs the same table (e906_unit_tests).
// First the runner itself must be able to fail: a table with a failing check is reported as FAIL.
// @verifies [SPEC-AUDIT-18] docs/SPECTRACE.md (run_all kAllTests)
#include "all_tests.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
std::string g_out;
void capture(const char* t) { g_out += t; }
void always_fails(abstractx::test::Context& c) { ABX_CHECK(c, 1 + 1 == 3); }
void always_passes(abstractx::test::Context& c) { ABX_CHECK(c, 1 + 1 == 2); }
} // namespace

int main() {
    const abstractx::test::UnitTest self[] = {{"always_fails", &always_fails}, {"always_passes", &always_passes}};
    const auto s0 = abstractx::test::run_all(self, 2, &capture);
    const bool runner_ok = s0.failed == 1 && s0.passed == 1 &&
                           g_out.find("FAIL always_fails") != std::string::npos &&
                           g_out.find("CHECK failed: 1 + 1 == 3 at ") != std::string::npos &&
                           g_out.find("UNIT: 1 passed, 1 failed") != std::string::npos;
    if (!runner_ok) {
        std::printf("runner self-check failed:\n%s", g_out.c_str());
        return 1;
    }
    const auto s = abstractx::test::run_all(abx_unit::kAllTests, abx_unit::kAllTestsCount,
                                            [](const char* t) { std::fputs(t, stdout); });
    return (s.failed == 0 && s.passed == abx_unit::kAllTestsCount) ? 0 : 1;
}
