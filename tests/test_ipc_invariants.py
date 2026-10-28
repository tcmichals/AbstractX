#!/usr/bin/env python3
# @verifies [SPEC-E906-IPC-02] targets/allwinner_e906/SPECIFICATION.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for tools/audit_ipc_invariants.py IPC-STALL: a ring error that returns without consuming
the descriptor is flagged; one that already advanced last_avail_idx and continues is not, even
with "return" in a comment (2026-10-28 false positive on rpmsg.cpp). Plain python3 or pytest.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import audit_ipc_invariants as ipc  # noqa: E402

STALLS = """
bool poll(T& t) {
    const uint16_t desc_idx = t.rx.avail->ring[t.rx.last_avail_idx & t.rx.mask];
    if (desc_idx >= t.rx.num) {
        t.ring_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}
"""
CONSUMES = """
bool poll(T& t) {
    const uint16_t desc_idx = t.rx.avail->ring[t.rx.last_avail_idx & t.rx.mask];
    t.rx.last_avail_idx++;
    if (desc_idx >= t.rx.num) {
        t.ring_errors.fetch_add(1, std::memory_order_relaxed);
        continue;  // invalid id: skip, nothing to return
    }
}
"""


def stalls(src):
    out = []
    ipc.check_cpp("x.cpp", src, out)
    return [o for o in out if "IPC-STALL" in o]


def test_return_without_consuming_is_flagged():
    assert len(stalls(STALLS)) == 1


def test_word_return_in_a_comment_is_not_a_return():
    assert stalls(CONSUMES) == []


if __name__ == "__main__":
    test_return_without_consuming_is_flagged()
    test_word_return_in_a_comment_is_not_a_return()
    print("[PASS] audit_ipc_invariants IPC-STALL")
