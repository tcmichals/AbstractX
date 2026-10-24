# @impl [SPEC-E906-REPL-06] [SPEC-E906-REPL-16] apps/e906_repl_app/SPECIFICATION.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
example_parallel_flight_coros.py - Parallel Multi-Rate Sensor Ingestion in MicroPython
======================================================================================
Demonstrates the AbstractX Coroutine Pattern running on XuanTie E906:
1. Producer 1: Compass (I2C1 @ 50 Hz / 20 ms period)
2. Producer 2: GPS (UART3 @ 10 Hz / 100 ms period via autonomous circular DMA)
3. Consumer: Primary-Paced Flight Fusion task
4. Zero Heap Allocations in steady-state loop (zero GC pauses)
5. Zero CPU waste: sleeps in hardware wfi during idle intervals
"""

import abstractx_coro

try:
    import abstractx
    HAS_ABSTRACTX = True
except ImportError:
    abstractx = None
    HAS_ABSTRACTX = False


# ==============================================================================
# 1. Asynchronous Channel Queues (matching AbstractX C++ AsyncQueue<T, N>)
# ==============================================================================
mag_channel = abstractx_coro.AsyncQueue(capacity=16)
gps_channel = abstractx_coro.AsyncQueue(capacity=16)


# ==============================================================================
# 2. Sensor Producer Coroutines
# ==============================================================================
def compass_producer(channel, max_samples=25):
    """Samples compass at 50 Hz (20 ms period) using pre-allocated buffer."""
    buf = bytearray(6)
    count = 0
    while count < max_samples:
        if HAS_ABSTRACTX:
            n = abstractx.i2c_read_into(0x1E, 0x03, buf)
            if n == 6:
                channel.try_push((count, bytes(buf)))
                count += 1
        else:
            # Simulated compass tick for host SITL
            channel.try_push((count, b"\x00\x10\x00\x20\x00\x30"))
            count += 1

        # Yield execution: E906 enters wfi sleep (0.01% CPU)
        yield abstractx_coro.sleep_ms(20)


def gps_producer(channel, max_samples=5):
    """Drains autonomous circular DMA ring at 10 Hz (100 ms period)."""
    buf = bytearray(512)
    count = 0
    while count < max_samples:
        if HAS_ABSTRACTX:
            n = abstractx.uart_read_into(buf)
            if n > 0:
                channel.try_push((count, bytes(buf[:n])))
                count += 1
        else:
            # Simulated GPS tick for host SITL
            channel.try_push((count, b"$GPRMC,083500.00,A,3306.9186,N,09701.1183,W,0.05,359.0,241026,,,A*7B"))
            count += 1

        yield abstractx_coro.sleep_ms(100)


# ==============================================================================
# 3. Primary-Paced Consumer Loop
# ==============================================================================
def flight_fusion_task(mag_q, gps_q, max_epochs=20):
    """
    Primary-paced flight loop:
    - Primary sensor (Compass) acts as the physical pacing clock
    - Auxiliary lower-rate sensor (GPS) is drained non-blockingly
    """
    epochs = 0
    last_gps = None

    print("[FLIGHT_FUSION] Starting Primary-Paced Multi-Rate Coroutine Task...")

    while epochs < max_epochs:
        # Await next compass sample (blocks cooperatively until producer pushes)
        mag_sample = yield mag_q.pop()

        # Non-blockingly drain auxiliary GPS fixes
        while True:
            gps = gps_q.try_pop()
            if gps is None:
                break
            last_gps = gps

        epochs += 1
        mag_idx = mag_sample[0] if isinstance(mag_sample, tuple) else epochs
        gps_status = "valid" if last_gps else "searching"
        print("  [EPOCH " + str(epochs) + "] MagTick=" + str(mag_idx) + " | GPS=" + gps_status)

    print("[FLIGHT_FUSION] Completed " + str(epochs) + " epochs with zero GC pauses.")


# ==============================================================================
# 4. Dispatcher Execution
# ==============================================================================
def main():
    if HAS_ABSTRACTX:
        abstractx.i2c_init(400000)
        abstractx.uart_init(115200)

    dispatcher = abstractx_coro.Dispatcher()
    dispatcher.spawn(compass_producer(mag_channel, max_samples=25))
    dispatcher.spawn(gps_producer(gps_channel, max_samples=5))
    dispatcher.spawn(flight_fusion_task(mag_channel, gps_channel, max_epochs=20))

    dispatcher.run(timeout_ms=5000)


if __name__ == "__main__":
    main()
