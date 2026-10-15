#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Linux side of the E906 RPMsg endpoints ([SPEC-E906-IPC-04]): bridge an RPMsg
character device to UDP.

  trace : E906 "abstractx-trace" (1025) -> one UDP datagram per CTF packet
          (default 127.0.0.1:9870, AbstractX Studio)
  tlp   : E906 "abstractx-tlp" (1024) <-> UDP. Messages from the E906 (whole
          64-byte TLPs, up to 7 per message) go to 127.0.0.1:9871; datagrams
          received on port 9872 (whole TLPs, at most 7) are sent to the E906.

Endpoint setup follows cubie-a5e's proven ping_rpmsg: RPMSG_CREATE_EPT_IOCTL on
/dev/rpmsg_ctrl0, open the new /dev/rpmsgN, then send a short hello so the
E906 learns this endpoint's address (it cannot transmit before that).

Usage (on the Cubie A5E):
  python3 e906_rpmsg_bridge.py trace
  python3 e906_rpmsg_bridge.py tlp --dev /dev/rpmsg1
"""
# @impl [SPEC-E906-IPC-04] targets/allwinner_e906/SPECIFICATION.md

import argparse
import fcntl
import glob
import os
import select
import socket
import struct
import sys
import time

TLP_BYTES = 64
MAX_TLPS_PER_MSG = 7  # 496-byte RPMsg payload / 64
HELLO = b"helo"

ENDPOINTS = {
    "trace": {"name": "abstractx-trace", "addr": 1025},
    "tlp": {"name": "abstractx-tlp", "addr": 1024},
}

# struct rpmsg_endpoint_info { char name[32]; u32 src; u32 dst; }  (linux/rpmsg.h)
EPT_INFO = struct.Struct("<32sII")
RPMSG_CREATE_EPT_IOCTL = (1 << 30) | (EPT_INFO.size << 16) | (0xB5 << 8) | 0x1  # _IOW(0xb5, 1, ...)


def create_endpoint(ctrl_path: str, name: str, addr: int, settle_s: float = 0.05) -> str:
    """Create an endpoint through the rpmsg control device; return the new /dev/rpmsgN."""
    before = set(glob.glob("/dev/rpmsg[0-9]*"))
    info = EPT_INFO.pack(name.encode()[:31], addr, addr)
    fd = os.open(ctrl_path, os.O_RDWR)
    try:
        fcntl.ioctl(fd, RPMSG_CREATE_EPT_IOCTL, info)
    finally:
        os.close(fd)
    deadline = time.monotonic() + 2.0
    while time.monotonic() < deadline:
        new = sorted(set(glob.glob("/dev/rpmsg[0-9]*")) - before)
        if new:
            time.sleep(settle_s)  # devtmpfs node permissions settle
            return new[0]
        time.sleep(0.01)
    raise RuntimeError(f"no new /dev/rpmsgN appeared after creating '{name}'")


class Bridge:
    """Moves messages between an RPMsg device fd and UDP. I/O is injected so
    the logic is testable without hardware."""

    def __init__(self, mode, dev_fd, out_sock, out_addr, in_sock=None):
        self.mode = mode
        self.dev_fd = dev_fd
        self.out_sock = out_sock
        self.out_addr = out_addr
        self.in_sock = in_sock
        self.stats = {"to_udp": 0, "to_e906": 0, "rejected": 0}

    def hello(self):
        os.write(self.dev_fd, HELLO)

    @staticmethod
    def valid_tlp_batch(data: bytes) -> bool:
        return 0 < len(data) <= TLP_BYTES * MAX_TLPS_PER_MSG and len(data) % TLP_BYTES == 0

    def pump(self, timeout_s: float) -> int:
        """Handle whatever is ready within timeout_s; returns messages moved."""
        fds = [self.dev_fd] + ([self.in_sock] if self.in_sock else [])
        ready, _, _ = select.select(fds, [], [], timeout_s)
        moved = 0
        if self.dev_fd in ready:
            data = os.read(self.dev_fd, 512)
            if self.mode == "tlp" and not self.valid_tlp_batch(data):
                self.stats["rejected"] += 1
            elif data:
                self.out_sock.sendto(data, self.out_addr)
                self.stats["to_udp"] += 1
                moved += 1
        if self.in_sock is not None and self.in_sock in ready:
            data, _ = self.in_sock.recvfrom(2048)
            if self.valid_tlp_batch(data):
                os.write(self.dev_fd, data)
                self.stats["to_e906"] += 1
                moved += 1
            else:
                self.stats["rejected"] += 1
        return moved


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("mode", choices=sorted(ENDPOINTS))
    ap.add_argument("--ctrl", default="/dev/rpmsg_ctrl0", help="rpmsg control device")
    ap.add_argument("--dev", help="use an existing /dev/rpmsgN instead of creating one")
    ap.add_argument("--udp-host", default="127.0.0.1")
    ap.add_argument("--udp-port", type=int, help="destination port (trace 9870, tlp 9871)")
    ap.add_argument("--listen-port", type=int, default=9872, help="tlp: UDP port for TLPs to the E906")
    args = ap.parse_args()

    ep = ENDPOINTS[args.mode]
    dev = args.dev or create_endpoint(args.ctrl, ep["name"], ep["addr"])
    dev_fd = os.open(dev, os.O_RDWR)
    out = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    port = args.udp_port or (9870 if args.mode == "trace" else 9871)
    in_sock = None
    if args.mode == "tlp":
        in_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        in_sock.bind(("0.0.0.0", args.listen_port))
    bridge = Bridge(args.mode, dev_fd, out, (args.udp_host, port), in_sock)
    bridge.hello()
    print(f"[bridge] {args.mode}: {dev} (endpoint {ep['addr']}) -> udp {args.udp_host}:{port}", file=sys.stderr)
    try:
        while True:
            bridge.pump(1.0)
    except KeyboardInterrupt:
        pass
    finally:
        print(f"[bridge] {bridge.stats}", file=sys.stderr)
        os.close(dev_fd)
    return 0


if __name__ == "__main__":
    sys.exit(main())
