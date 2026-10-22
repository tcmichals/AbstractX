#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
e906_link.py - Talk to the E906 from Linux on the Cubie A5E (stdlib only).

Runs a MicroPython snippet through raw REPL on endpoint 1026 and reads the
TLP (1024) and trace (1025) endpoints directly. Endpoint helpers come from
tools/e906_rpmsg_bridge.py, copied next to this file on the board.
"""
# @impl [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md

import os
import select
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
for p in (HERE, os.path.join(HERE, "..", "..", "tools")):
    if p not in sys.path:
        sys.path.insert(0, p)

from e906_rpmsg_bridge import ENDPOINTS, HELLO, create_endpoint, find_node  # noqa: E402

RPROC = "/sys/class/remoteproc/remoteproc0"
RPMSG_CTRL = "/dev/rpmsg_ctrl0"
REPL = {"name": "abstractx-repl", "addr": 1026}


def on_e906_board() -> bool:
    return os.path.exists(RPROC + "/state")


def read_text(path: str) -> str:
    with open(path) as f:
        return f.read().strip()


def parse_raw_repl(reply: bytes):
    """(stdout, stderr) from a raw-REPL reply: 'OK' stdout \\x04 stderr \\x04 '>'. None if incomplete."""
    i = reply.find(b"OK")
    if i < 0:
        return None
    parts = reply[i + 2:].split(b"\x04")
    if len(parts) < 3:
        return None
    return parts[0].decode("utf-8", "replace"), parts[1].decode("utf-8", "replace")


def _write_all(fd: int, data: bytes) -> None:
    while data:
        try:
            n = os.write(fd, data)
            data = data[n:]
        except BlockingIOError:
            time.sleep(0.005)


def _drain(fd: int, quiet_s: float = 0.1, max_s: float = 1.0) -> bytes:
    """Read until quiet_s passes with nothing new, or max_s in total: the trace endpoint is
    never quiet now that profiler samples stream continuously ([SPEC-TRACE-09])."""
    out = b""
    deadline = time.monotonic() + max_s
    while time.monotonic() < deadline and select.select([fd], [], [], quiet_s)[0]:
        try:
            chunk = os.read(fd, 2048)
        except BlockingIOError:
            break
        if not chunk:
            break
        out += chunk
    return out


def repl_exec(code: str, timeout_s: float = 10.0, interrupt_after_s: float = None, msg_sizes: list = None):
    """Run code on the E906; return (stdout, stderr). Raises TimeoutError if no complete reply.

    interrupt_after_s: send Ctrl-C that long after the script was sent.
    msg_sizes: if given, receives the size of every RPMsg message of the reply.
    """
    node = create_endpoint(RPMSG_CTRL, REPL["name"], REPL["addr"])
    fd = os.open(node, os.O_RDWR | os.O_NONBLOCK)
    try:
        _write_all(fd, b"\x03\x03\x02")  # stop anything running, normal REPL
        _drain(fd)
        _write_all(fd, b"\x01")  # raw REPL
        _drain(fd)
        payload = code.encode("utf-8") + b"\x04"
        for i in range(0, len(payload), 256):  # one RPMsg buffer per chunk
            _write_all(fd, payload[i:i + 256])
            time.sleep(0.02)
        reply = b""
        sent_at = time.monotonic()
        deadline = sent_at + timeout_s
        while time.monotonic() < deadline:
            if interrupt_after_s is not None and time.monotonic() - sent_at >= interrupt_after_s:
                _write_all(fd, b"\x03")
                interrupt_after_s = None
            if select.select([fd], [], [], 0.05)[0]:
                try:
                    chunk = os.read(fd, 2048)  # one read = one RPMsg message on an rpmsg char device
                except BlockingIOError:
                    continue
                reply += chunk
                if msg_sizes is not None:
                    msg_sizes.append(len(chunk))
                result = parse_raw_repl(reply)
                if result is not None:
                    return result
        raise TimeoutError(f"no complete raw-REPL reply in {timeout_s}s; got {reply[-200:]!r}")
    finally:
        try:
            _write_all(fd, b"\x03\x03\x02")
        finally:
            os.close(fd)


class Plane:
    """An open TLP or trace endpoint; the hello tells the E906 our address."""

    def __init__(self, mode: str):
        ep = ENDPOINTS[mode]
        self.node = create_endpoint(RPMSG_CTRL, ep["name"], ep["addr"])
        self.fd = os.open(self.node, os.O_RDWR | os.O_NONBLOCK)
        os.write(self.fd, HELLO)
        _drain(self.fd, 0.05)  # discard anything queued before this test

    def read_messages(self, duration_s: float) -> list:
        msgs = []
        deadline = time.monotonic() + duration_s
        next_hello = time.monotonic() + 1.0
        while time.monotonic() < deadline:
            if time.monotonic() >= next_hello:  # renew the trace lease [SPEC-E906-IPC-05]
                os.write(self.fd, HELLO)
                next_hello = time.monotonic() + 1.0
            if select.select([self.fd], [], [], 0.05)[0]:
                try:
                    msgs.append(os.read(self.fd, 4096))
                except BlockingIOError:
                    pass
        return msgs

    def close(self) -> None:
        os.close(self.fd)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def endpoint_exists(name: str) -> bool:
    return find_node(name) is not None
