#!/usr/bin/env python3
# @verifies [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Unit Test Suite: abstractx_coro.py (MicroPython Coroutine & Channel Framework)
-----------------------------------------------------------------------------
Verifies:
1. AsyncQueue capacity, FIFO order, try_push, and try_pop contracts.
2. Cooperative generator coroutines and sleep_ms dispatching.
3. Channel pop() suspension and resumption when producer pushes.
4. Multi-rate task interleaving (high-rate vs low-rate tasks).
5. Primary-Paced Channel Pattern (primary sensor clocking fusion loop).
"""

import sys
from pathlib import Path

# Add apps/e906_repl_app to path
E906_APP_DIR = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(E906_APP_DIR))

import abstractx_coro


def test_async_queue_basic():
    """Verify AsyncQueue FIFO behavior, overflow protection, and counts."""
    q = abstractx_coro.AsyncQueue(capacity=4)
    assert q.is_empty()
    assert not q.is_full()
    assert q.count() == 0

    assert q.try_push("item1") is True
    assert q.try_push("item2") is True
    assert q.try_push("item3") is True
    assert q.try_push("item4") is True
    assert q.is_full()
    assert q.count() == 4

    # Overflow rejection
    assert q.try_push("overflow") is False

    # FIFO order extraction
    assert q.try_pop() == "item1"
    assert q.try_pop() == "item2"
    assert q.try_pop() == "item3"
    assert q.try_pop() == "item4"
    assert q.try_pop() is None
    assert q.is_empty()


def test_dispatcher_sleep_and_cooperative_dispatch():
    """Verify dispatcher scheduling with sleep_ms()."""
    log = []

    def task_a():
        log.append("a1")
        yield abstractx_coro.sleep_ms(10)
        log.append("a2")
        yield abstractx_coro.sleep_ms(10)
        log.append("a3")

    def task_b():
        log.append("b1")
        yield abstractx_coro.sleep_ms(5)
        log.append("b2")

    dispatcher = abstractx_coro.Dispatcher()
    dispatcher.spawn(task_a())
    dispatcher.spawn(task_b())
    dispatcher.run(timeout_ms=500)

    # Both tasks must run to completion
    assert "a1" in log and "a2" in log and "a3" in log
    assert "b1" in log and "b2" in log


def test_channel_pop_awaiter():
    """Verify consumer suspension on queue.pop() until producer pushes."""
    channel = abstractx_coro.AsyncQueue(8)
    consumed = []

    def producer():
        for i in range(3):
            yield abstractx_coro.sleep_ms(10)
            channel.try_push(f"msg_{i}")

    def consumer():
        for _ in range(3):
            item = yield channel.pop()
            consumed.append(item)

    dispatcher = abstractx_coro.Dispatcher()
    dispatcher.spawn(producer())
    dispatcher.spawn(consumer())
    dispatcher.run(timeout_ms=500)

    assert consumed == ["msg_0", "msg_1", "msg_2"]


def test_primary_paced_fusion_pattern():
    """
    Verify the AbstractX Primary-Paced Channel Pattern in MicroPython:
    - Primary sensor (IMU/Compass) clocks the loop with yield q_primary.pop()
    - Auxiliary sensor (GPS) is drained non-blockingly with while q_aux.try_pop()
    """
    imu_channel = abstractx_coro.AsyncQueue(8)
    gps_channel = abstractx_coro.AsyncQueue(8)
    fused_epochs = []

    # High-rate primary producer (e.g. 50 Hz, simulated with 10 ms steps)
    def imu_producer():
        for epoch in range(1, 6):
            yield abstractx_coro.sleep_ms(10)
            imu_channel.try_push(f"imu_sample_{epoch}")

    # Lower-rate auxiliary producer (e.g. 10 Hz, simulated with 25 ms steps)
    def gps_producer():
        yield abstractx_coro.sleep_ms(22)
        gps_channel.try_push("gps_fix_1")
        yield abstractx_coro.sleep_ms(25)
        gps_channel.try_push("gps_fix_2")

    # Consumer: primary-paced fusion task
    def fusion_consumer():
        gps_last = "none"
        for _ in range(5):
            imu = yield imu_channel.pop()
            # Drain all available auxiliary GPS fixes
            while True:
                gps = gps_channel.try_pop()
                if gps is None:
                    break
                gps_last = gps
            fused_epochs.append((imu, gps_last))

    dispatcher = abstractx_coro.Dispatcher()
    dispatcher.spawn(imu_producer())
    dispatcher.spawn(gps_producer())
    dispatcher.spawn(fusion_consumer())
    dispatcher.run(timeout_ms=500)

    assert len(fused_epochs) == 5
    # GPS fix arrives after epoch 2
    assert fused_epochs[0][1] == "none"
    assert fused_epochs[4][1] in ("gps_fix_1", "gps_fix_2")


if __name__ == "__main__":
    test_async_queue_basic()
    test_dispatcher_sleep_and_cooperative_dispatch()
    test_channel_pop_awaiter()
    test_primary_paced_fusion_pattern()
    print("All abstractx_coro unit tests passed.")
