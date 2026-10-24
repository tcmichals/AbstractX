# @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
abstractx_coro.py - The AbstractX Coroutine & Channel Framework for MicroPython
===============================================================================
A lightweight, zero-heap cooperative coroutine and channel library designed for
bare-metal MicroPython (XuanTie E906) and desktop Python SITL.

Key Architectural Principles:
1. Mirror C++20 Coroutine Engine:
   Maps 1-to-1 with AbstractX C++20 coro::Task, AsyncQueue, and primary-paced channels.
2. Zero Heap in Steady State:
   Pre-allocated static queues and stackless generator coroutines prevent
   MicroPython mark-and-sweep Garbage Collection (GC) pauses during sensor loops.
3. Hardware wfi Sleep:
   Calls abstractx.sleep_ms() when idle, entering RISC-V wfi (0.01% busy) while
   hardware DMA and CLIC interrupts continue running in parallel.
"""

try:
    import abstractx
    _ticks_ms = abstractx.ticks_ms
    _sleep_ms = abstractx.sleep_ms
except ImportError:
    import time
    abstractx = None
    _ticks_ms = lambda: int(time.time() * 1000) & 0x7FFFFFFF
    _sleep_ms = lambda ms: time.sleep(ms / 1000.0)


class AwaitSleep:
    """Awaitable sleep token matching co_await timer.sleep_ms(ms)."""
    __slots__ = ("ms",)
    def __init__(self, ms):
        self.ms = ms


class AwaitQueuePop:
    """Awaitable queue pop matching co_await queue.pop()."""
    __slots__ = ("queue",)
    def __init__(self, queue):
        self.queue = queue


def sleep_ms(ms):
    """Yieldable sleep duration for cooperative tasks."""
    return AwaitSleep(ms)


class AsyncQueue:
    """
    Fixed-capacity circular channel matching AbstractX AsyncQueue<T, N>.
    Provides lock-free try_push / try_pop and cooperative pop() awaitable.
    """
    __slots__ = ("capacity", "buf", "head", "tail", "_count")

    def __init__(self, capacity=16):
        self.capacity = capacity
        self.buf = [None] * capacity
        self.head = 0
        self.tail = 0
        self._count = 0

    def is_empty(self):
        return self._count == 0

    def is_full(self):
        return self._count == self.capacity

    def count(self):
        return self._count

    def try_push(self, item):
        """Non-blocking push. Returns True if enqueued, False if queue is full."""
        if self._count >= self.capacity:
            return False
        self.buf[self.tail] = item
        self.tail = (self.tail + 1) % self.capacity
        self._count += 1
        return True

    def try_pop(self):
        """Non-blocking pop. Returns item if available, or None if empty."""
        if self._count == 0:
            return None
        item = self.buf[self.head]
        self.buf[self.head] = None
        self.head = (self.head + 1) % self.capacity
        self._count -= 1
        return item

    def pop(self):
        """
        Cooperative awaitable pop.
        Usage: item = yield queue.pop()
        """
        return AwaitQueuePop(self)


class Dispatcher:
    """
    Cooperative coroutine dispatcher matching AbstractX Dispatcher::process().
    Pumps tasks, resolves channel waits, and puts CPU into wfi during idle spans.
    """
    def __init__(self):
        self._tasks = []       # list of [coro, wake_time_ms, waiting_queue, last_val]
        self.running = False

    def spawn(self, coro):
        """Spawns a generator coroutine task."""
        self._tasks.append([coro, _ticks_ms(), None, None])

    def step(self):
        """
        Executes one non-blocking cooperative dispatch pass.
        Returns the minimum sleep duration in milliseconds until the next scheduled task.
        """
        if not self._tasks:
            return 10

        now = _ticks_ms()
        min_sleep = 1000
        active_tasks = []

        for task in self._tasks:
            coro, wake_time, waiting_queue, last_val = task

            # Check if task is awaiting an AsyncQueue
            if waiting_queue is not None:
                item = waiting_queue.try_pop()
                if item is not None:
                    # Queue has item: resume task immediately with item
                    task[2] = None
                    waiting_queue = None
                    last_val = item
                else:
                    active_tasks.append(task)
                    continue

            # Check if task wake time has arrived
            time_diff = wake_time - now
            if time_diff <= 0:
                try:
                    if last_val is not None:
                        result = coro.send(last_val)
                        task[3] = None
                    else:
                        result = next(coro)

                    # Interpret yielded result
                    if isinstance(result, AwaitSleep):
                        task[1] = _ticks_ms() + result.ms
                        task[2] = None
                        if result.ms < min_sleep:
                            min_sleep = result.ms
                    elif isinstance(result, AwaitQueuePop):
                        task[2] = result.queue
                        # Check queue immediately
                        item = result.queue.try_pop()
                        if item is not None:
                            task[2] = None
                            task[3] = item
                            min_sleep = 0
                    elif isinstance(result, int):
                        task[1] = _ticks_ms() + result
                        task[2] = None
                        if result < min_sleep:
                            min_sleep = result
                    else:
                        task[1] = _ticks_ms()
                        min_sleep = 0

                    active_tasks.append(task)
                except StopIteration:
                    # Task completed naturally
                    pass
            else:
                if time_diff < min_sleep:
                    min_sleep = time_diff
                active_tasks.append(task)

        self._tasks = active_tasks
        return max(0, min_sleep)

    def run(self, max_epochs=None, timeout_ms=None):
        """
        Runs the cooperative event loop until tasks complete, max_epochs is reached,
        or timeout expires. Calls abstractx.sleep_ms() to sleep in hardware wfi.
        """
        self.running = True
        start_ms = _ticks_ms()
        epochs = 0

        while self.running and self._tasks:
            if timeout_ms is not None and (_ticks_ms() - start_ms >= timeout_ms):
                break
            if max_epochs is not None and epochs >= max_epochs:
                break

            sleep_duration = self.step()
            epochs += 1

            if sleep_duration > 0:
                _sleep_ms(min(sleep_duration, 50))
            else:
                _sleep_ms(1)

        self.running = False
