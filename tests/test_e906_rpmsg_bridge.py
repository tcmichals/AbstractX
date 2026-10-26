#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for tools/e906_rpmsg_bridge.py without hardware.
A SOCK_SEQPACKET socketpair stands in for /dev/rpmsgN: like the RPMsg character
device it preserves message boundaries (one read = one RPMsg message).
@verifies [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md
"""

import os
import socket
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import e906_rpmsg_bridge as br  # noqa: E402


def udp_pair():
    rx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    rx.bind(("127.0.0.1", 0))
    rx.settimeout(1.0)
    tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    return tx, rx


def test_ioctl_encoding_matches_linux():
    # _IOW(0xb5, 0x1, struct rpmsg_endpoint_info) with a 40-byte struct
    assert br.EPT_INFO.size == 40
    assert br.RPMSG_CREATE_EPT_IOCTL == 0x4028B501


def test_trace_messages_become_udp_datagrams():
    e906, host = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    out, studio = udp_pair()
    b = br.Bridge("trace", host.fileno(), out, studio.getsockname())
    b.hello()
    assert e906.recv(64) == br.HELLO, "hello lets the E906 learn our address"
    pkt = struct.pack("<I", 0xC1FC1FC1) + bytes(range(60))  # CTF magic + body
    e906.send(pkt)
    e906.send(b"second")
    assert b.pump(0.5) == 1 and b.pump(0.5) == 1
    assert studio.recv(2048) == pkt and studio.recv(2048) == b"second", "one datagram per message"


def test_tlp_bridge_both_ways_and_validation():
    e906, host = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    out, app = udp_pair()
    inbound = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    inbound.bind(("127.0.0.1", 0))
    b = br.Bridge("tlp", host.fileno(), out, app.getsockname(), inbound)

    batch = bytes(i % 256 for i in range(3 * br.TLP_BYTES))
    e906.send(batch)
    b.pump(0.5)
    assert app.recv(2048) == batch, "E906 batch forwarded intact"

    e906.send(b"x" * 100)  # not whole TLPs
    b.pump(0.5)
    assert b.stats["rejected"] == 1

    sender = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    tlps = bytes(range(2 * br.TLP_BYTES))
    sender.sendto(tlps, inbound.getsockname())
    b.pump(0.5)
    assert e906.recv(2048) == tlps, "UDP TLPs written to the E906"
    sender.sendto(bytes(8 * br.TLP_BYTES), inbound.getsockname())  # > 7 TLPs
    b.pump(0.5)
    assert b.stats == {"to_udp": 1, "to_e906": 1, "rejected": 2, "dropped": 0}


def test_async_bridge_event_driven_dispatch():
    """Verifies that AsyncBridge dispatches datagrams using asyncio epoll readers."""
    import asyncio

    async def run_test():
        e906, host = socket.socketpair(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        out, studio = udp_pair()
        loop = asyncio.get_running_loop()
        b = br.AsyncBridge("trace", host.fileno(), out, studio.getsockname(), loop=loop)
        
        # Start async run in a task
        task = asyncio.create_task(b.run())
        await asyncio.sleep(0.05)  # allow epoll registration

        assert e906.recv(64) == br.HELLO

        pkt = struct.pack("<I", 0xC1FC1FC1) + bytes(range(60))
        e906.send(pkt)
        await asyncio.sleep(0.05)

        assert studio.recv(2048) == pkt
        assert b.stats["to_udp"] == 1

        b.stop()
        task.cancel()
        try:
            await task
        except asyncio.CancelledError:
            pass

    asyncio.run(run_test())


if __name__ == "__main__":
    test_ioctl_encoding_matches_linux()
    test_trace_messages_become_udp_datagrams()
    test_tlp_bridge_both_ways_and_validation()
    print("[PASS] e906_rpmsg_bridge")
