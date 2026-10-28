#!/usr/bin/env python3
# @verifies [SPEC-AUDIT-15] docs/SPECTRACE.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for tools/boundary_review.py wake-handshake check ([SPEC-AUDIT-15], 2026-10-28).
The fixtures are the old UartRxRing shapes that lost wakes, the fixed ones, and the waiver.
Runs with plain python3 or pytest.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import boundary_review as br  # noqa: E402

RING = """
struct Ring {
    std::atomic<void*> waiter_{nullptr};
    std::atomic<uint32_t> wr_{0};
    bool satisfied() const noexcept { return wr_.load(std::memory_order_acquire) != 0; }
    void produce(uint32_t w) noexcept {
        wr_.store(w, std::memory_order_release);
        %(pfence)s
        if (waiter_.load(std::memory_order_acquire) != nullptr) wake();
    }
    bool await_suspend(void* h) noexcept {
        waiter_.store(h, std::memory_order_release);
        %(cfence)s
        if (satisfied()) { return false; }
        return true;
    }
    void clear() noexcept { waiter_.store(nullptr, std::memory_order_release); (void)wr_.load(); }
};
"""
FENCE = "std::atomic_thread_fence(std::memory_order_seq_cst);"


def run(pfence="", cfence=""):
    return br.handshake_scan("x.hpp", RING % {"pfence": pfence, "cfence": cfence}, set())


def test_old_handshake_flagged_on_both_sides():
    hits = run()
    roles = sorted(h[3] for h in hits)
    assert [h[2] for h in hits] == ["wr_", "waiter_"], hits
    assert roles == ["data published, then waiter checked", "waiter published, then data checked"]


def test_fenced_handshake_passes():
    assert run(FENCE, FENCE) == []


def test_one_side_fixed_still_flags_the_other():
    assert [h[2] for h in run(FENCE, "")] == ["waiter_"]
    assert [h[2] for h in run("", FENCE)] == ["wr_"]


def test_waiver_needs_its_own_comment():
    src = RING % {"pfence": "", "cfence": FENCE}
    src = src.replace("        wr_.store(w,", "        // handshake: same hart, IRQ unmasked after publish\n        wr_.store(w,")
    assert br.handshake_scan("x.hpp", src, set()) == []


def test_clearing_the_slot_and_seq_cst_stores_are_not_handshakes():
    src = RING % {"pfence": FENCE, "cfence": FENCE}
    assert br.handshake_scan("x.hpp", src.replace("std::memory_order_release", "std::memory_order_seq_cst"), set()) == []


def test_repo_has_no_unfenced_handshake():
    files = []
    for base in br.DEFAULT_PATHS:
        for p in sorted((br.ROOT / base).rglob("*")):
            if p.suffix in br.EXTS and p.is_file() and not (set(p.relative_to(br.ROOT).parts) & br.SKIP_PARTS):
                files.append((p.relative_to(br.ROOT).as_posix(), p.read_text(encoding="utf-8", errors="ignore")))
    waiters = set()
    for _, t in files:
        waiters |= set(br.WAITER_DECL.findall(br.strip_comments(t)))
    hits = [h for rel, t in files for h in br.handshake_scan(rel, t, waiters)]
    assert hits == [], hits


if __name__ == "__main__":
    test_old_handshake_flagged_on_both_sides()
    test_fenced_handshake_passes()
    test_one_side_fixed_still_flags_the_other()
    test_waiver_needs_its_own_comment()
    test_clearing_the_slot_and_seq_cst_stores_are_not_handshakes()
    test_repo_has_no_unfenced_handshake()
    print("[PASS] boundary_review wake handshake")
