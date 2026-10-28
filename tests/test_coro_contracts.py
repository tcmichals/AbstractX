#!/usr/bin/env python3
# @verifies [SPEC-ARCH-10] docs/DESIGN_SPECIFICATION.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for tools/audit_coro_contracts.py check 5: a public await_suspend that drops its handle
([SPEC-ARCH-10] every suspension has a waker, 2026-10-28). Runs with plain python3 or pytest.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import audit_coro_contracts as acc  # noqa: E402

DROPS = """
struct AsyncDelayAwaiter {
    uint64_t* reg;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) noexcept {
        if (reg) { *reg = 1; }
    }
    void await_resume() noexcept {}
};
template <typename P> struct WaitUntilAwaiter {
    P p;
    void await_suspend(std::coroutine_handle<> h) noexcept { (void)0; }
};
"""
KEEPS = """
struct Yield {
    void await_suspend(std::coroutine_handle<> h) noexcept { abstractx::Dispatcher::push(h); }
};
struct Initial {
    // waker: the Task object owns the handle (lazy start)
    void await_suspend(std::coroutine_handle<>) const noexcept {}
};
struct Req {
    bool await_suspend(std::coroutine_handle<> handle) noexcept {
        req.coro_handle = handle;  // the h in a comment does not count
        return submit(req);
    }
};
"""


def test_dropped_handles_are_found():
    assert acc.dropped_handles(DROPS) == [5, 12]


def test_registered_or_waived_handles_pass():
    assert acc.dropped_handles(KEEPS) == []


def test_comment_mentioning_the_name_does_not_count():
    src = "void await_suspend(std::coroutine_handle<> h) noexcept {\n    // h is ignored\n}\n"
    assert acc.dropped_handles(src) == [1]


def test_public_headers_have_no_dropped_handle():
    root = Path(__file__).resolve().parent.parent / "include"
    hits = []
    for p in sorted(root.rglob("*.hpp")):
        hits += [(p.name, ln) for ln in acc.dropped_handles(p.read_text(encoding="utf-8", errors="ignore"))]
    assert hits == [], hits


if __name__ == "__main__":
    test_dropped_handles_are_found()
    test_registered_or_waived_handles_pass()
    test_comment_mentioning_the_name_does_not_count()
    test_public_headers_have_no_dropped_handle()
    print("[PASS] audit_coro_contracts dropped handle")
