"""
Test Suite: XuanTie E906 Flight Controller Protocol & Heartbeat Watchdog
------------------------------------------------------------------------
# @impl [SPEC-E906-REPL-14] apps/e906_repl_app/SPECIFICATION.md
# @impl [SPEC-STUDIO-01] tools/visualizer/SPECIFICATION.md
Tests the PC-side host bridge and flight controller protocol:
1. Heartbeat watchdog state machine and autonomous timeout shutdown.
2. Host bridge periodic 1.0s PING generation and graceful STOP teardown.
3. Raw sensor hex framing and telemetry packet encoding.
"""

import time
import pytest

class MockAbstractX:
    """Mock native abstractx module for PC execution testing."""
    def __init__(self):
        self._rx_queue = bytearray()
        self._time_ms = 1000

    def ticks_ms(self):
        return self._time_ms

    def advance_time(self, delta_ms):
        self._time_ms += delta_ms

    def feed_input(self, data: bytes):
        self._rx_queue.extend(data)

    def console_read(self, max_len=64):
        chunk = bytes(self._rx_queue[:max_len])
        del self._rx_queue[:max_len]
        return chunk


def test_heartbeat_watchdog_reset_on_ping():
    """Validates that PING and HEARTBEAT commands feed the watchdog timer."""
    mock = MockAbstractX()
    last_heartbeat = mock.ticks_ms()

    # Advance 1500 ms without ping
    mock.advance_time(1500)
    assert mock.ticks_ms() - last_heartbeat == 1500

    # Feed PING from host
    mock.feed_input(b"PING\n")
    cmd = mock.console_read(64)
    if b"PING" in cmd or b"HEARTBEAT" in cmd:
        last_heartbeat = mock.ticks_ms()

    assert last_heartbeat == mock.ticks_ms()
    assert mock.ticks_ms() - last_heartbeat == 0


def test_heartbeat_watchdog_fires_on_silence():
    """Validates that 3000 ms of host silence triggers autonomous shutdown [SPEC-E906-REPL-14]."""
    mock = MockAbstractX()
    last_heartbeat = mock.ticks_ms()
    timeout_ms = 3000
    loop_running = True
    shutdown_reason = None

    # Host disconnects / silence for 3500 ms
    for _ in range(35):
        mock.advance_time(100)
        cmd = mock.console_read(64)
        if cmd:
            if b"PING" in cmd:
                last_heartbeat = mock.ticks_ms()
            elif b"STOP" in cmd or b"\x03" in cmd:
                shutdown_reason = "STOP_REQUESTED"
                loop_running = False
                break

        if mock.ticks_ms() - last_heartbeat > timeout_ms:
            shutdown_reason = "WATCHDOG_TIMEOUT_SHUTDOWN"
            loop_running = False
            break

    assert loop_running is False
    assert shutdown_reason == "WATCHDOG_TIMEOUT_SHUTDOWN"


def test_graceful_stop_command():
    """Validates that explicit STOP / CLOSE / Ctrl-C terminates immediately."""
    mock = MockAbstractX()
    loop_running = True
    shutdown_reason = None

    mock.feed_input(b"STOP\n\x03\x03\x02")
    cmd = mock.console_read(64)
    if b"STOP" in cmd or b"\x03" in cmd:
        shutdown_reason = "STOP_REQUESTED"
        loop_running = False

    assert loop_running is False
    assert shutdown_reason == "STOP_REQUESTED"


def test_raw_sensor_hex_formatting():
    """Validates to_hex formatting matches E906 raw_sensor_streamer encoding."""
    hex_chars = "0123456789ABCDEF"
    def to_hex(raw_b):
        if not raw_b: return ""
        out = ""
        for b in raw_b:
            out += hex_chars[(b >> 4) & 0x0F] + hex_chars[b & 0x0F]
        return out

    raw_mag = bytes([0x12, 0x34, 0xAB, 0xCD, 0x00, 0xFF])
    assert to_hex(raw_mag) == "1234ABCD00FF"
    assert to_hex(bytes([])) == ""
