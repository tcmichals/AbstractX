// Copyright (C) 2026 Tim Michals
// SPDX-License-Identifier: GPL-3.0-or-later
// Freestanding unit-test runner: no heap, no printf, no static registration ([SPEC-AUDIT-18]).
// The same test sources run on the host (ctest) and on the processor (results to trace0).
#pragma once

#include <cstddef>
#include <cstdint>

namespace abstractx::test {

using ReportFn = void (*)(const char* text);   // receives text pieces; the caller adds nothing

struct Context {
    ReportFn report{nullptr};
    uint32_t failures{0};    // checks failed in the current test
};

using TestFn = void (*)(Context&);
struct UnitTest {
    const char* name;
    TestFn fn;
};

// Decimal text of v into buf (>= 11 bytes); returns buf.
inline const char* to_dec(uint32_t v, char* buf) noexcept {
    char tmp[11];
    int n = 0;
    do {
        tmp[n++] = static_cast<char>('0' + v % 10U);
        v /= 10U;
    } while (v != 0U && n < 10);
    for (int i = 0; i < n; ++i) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return buf;
}

inline void fail(Context& c, const char* file, uint32_t line, const char* expr) noexcept {
    ++c.failures;
    if (c.report == nullptr) return;
    char num[11];
    c.report("  CHECK failed: ");
    c.report(expr);
    c.report(" at ");
    c.report(file);
    c.report(":");
    c.report(to_dec(line, num));
    c.report("\n");
}

struct Summary {
    uint32_t passed{0};
    uint32_t failed{0};
};

// @impl [SPEC-AUDIT-18] docs/SPECTRACE.md (one runner for host and target)
inline Summary run_all(const UnitTest* tests, size_t n, ReportFn report) noexcept {
    Summary s{};
    char num[11];
    for (size_t i = 0; i < n; ++i) {
        Context c{report, 0};
        tests[i].fn(c);
        if (c.failures == 0) {
            ++s.passed;
            report("PASS ");
        } else {
            ++s.failed;
            report("FAIL ");
        }
        report(tests[i].name);
        report("\n");
    }
    report("UNIT: ");
    report(to_dec(s.passed, num));
    report(" passed, ");
    report(to_dec(s.failed, num));
    report(" failed\n");
    return s;
}

} // namespace abstractx::test

#define ABX_CHECK(ctx, cond)                                                                      \
    do {                                                                                          \
        if (!(cond)) ::abstractx::test::fail((ctx), __FILE__, static_cast<uint32_t>(__LINE__), #cond); \
    } while (0)
