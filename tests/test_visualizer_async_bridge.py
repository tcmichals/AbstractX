"""
Copyright (C) 2026 Tim Michals
SPDX-License-Identifier: GPL-3.0-or-later

@verifies [SPEC-STUDIO-08] [SPEC-STUDIO-04] tools/visualizer/SPECIFICATION.md

Unit Tests for AbstractX Studio AsyncIO Bridge Engine
----------------------------------------------------
Verifies:
1. TelemetryDatagramProtocol processes 64B TLPs and updates TelemetryState.
2. StudioAsyncEngine starts an event-driven UDP listener and receives datagrams.
3. AsyncBridgeController drains lines asynchronously and dispatches CLI commands.
"""

import asyncio
import socket
import struct
import time
import pytest

from tools.visualizer.async_bridge import (
    TelemetryDatagramProtocol,
    AsyncBridgeController,
    StudioAsyncEngine,
)
from tools.visualizer.abstractx_studio import TelemetryState
import tools.visualizer.runtime_stats as rs


def test_telemetry_datagram_protocol_tlp_ingestion():
    """Verifies that TelemetryDatagramProtocol unpacks 64B TLPs and commits to TelemetryState."""
    state = TelemetryState()
    protocol = TelemetryDatagramProtocol(state, runtime_stats_module=rs)

    # Construct a valid 64-byte TLP frame (Type=0x10, Flags=0, Tag=2, Ch=1, Addr=0, Len=4, Seq=42, Ts=1000)
    hdr = struct.pack("<BBBBIHHQ", 0x10, 0x00, 0x02, 0x01, 0x00000000, 4, 42, 1000)
    payload = b"\xAA" * 40
    trailer = struct.pack("<I", 0)  # zero footer / CRC
    frame = hdr + payload + trailer
    assert len(frame) == 64

    # Feed datagram to protocol
    protocol.datagram_received(frame, ("127.0.0.1", 9870))

    with state.lock:
        assert state.connected is True
        assert state.packet_count == 1
        assert state.bytes_received == 64
        assert len(state.recent_tlp_packets) == 1
        pkt = state.recent_tlp_packets[0]
        assert pkt["seq"] == 42
        assert pkt["type"] == 0x10
        assert pkt["tag"] == 2
        assert pkt["channel"] == 1


def test_studio_async_engine_udp_loopback():
    """Verifies that StudioAsyncEngine runs a live background asyncio UDP server receiving packets."""
    state = TelemetryState()
    engine = StudioAsyncEngine(state, runtime_stats_module=rs)

    test_port = 19876
    try:
        ok = engine.start_udp_listener(port=test_port)
        assert ok is True

        # Send test TLP over socket to loopback
        sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        hdr = struct.pack("<BBBBIHHQ", 0x20, 0x00, 0x05, 0x02, 0x00000000, 4, 99, 5000)
        frame = hdr + (b"\x55" * 40) + struct.pack("<I", 0)
        sender.sendto(frame, ("127.0.0.1", test_port))
        sender.close()

        # Wait up to 1 second for packet arrival
        deadline = time.time() + 1.0
        while time.time() < deadline:
            with state.lock:
                if state.packet_count > 0:
                    break
            time.sleep(0.01)

        with state.lock:
            assert state.packet_count == 1
            assert state.recent_tlp_packets[0]["seq"] == 99
            assert state.recent_tlp_packets[0]["tag"] == 5
    finally:
        engine.shutdown()


def test_async_bridge_controller_mock_streams():
    """Verifies that AsyncBridgeController asynchronously routes !CLI: lines and logs."""
    async def run_test():
        state = TelemetryState()
        cli_lines = []
        controller = AsyncBridgeController(state, cli_handler=cli_lines.append)

        # Mock StreamReader feeding lines
        reader = asyncio.StreamReader()
        reader.feed_data(b"[LOG] System initializing...\n!CLI:10:BEGIN\n!CLI:10:OUT:42\n!CLI:10:END:OK\n")
        reader.feed_eof()

        await controller._drain_stream(reader)

        assert cli_lines == ["!CLI:10:BEGIN", "!CLI:10:OUT:42", "!CLI:10:END:OK"]
        assert controller.log_deque[-1] == "[LOG] System initializing..."

    asyncio.run(run_test())
