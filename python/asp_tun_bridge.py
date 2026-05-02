#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""AbstractX unified TUN bridge (SPI + DMA backends).

This is the canonical forward-looking userspace bridge for Zynq bring-up.
It keeps one TUN loop and swaps transport backends via CLI.
"""

from __future__ import annotations

import argparse
import errno
import os
import struct
import time
from collections import deque
from fcntl import ioctl
from typing import Optional

TUNSETIFF = 0x400454CA
IFF_TUN = 0x0001
IFF_NO_PI = 0x1000

CMD_WRITE_DATA = 0x80
CMD_READ_STATUS = 0x01
CMD_READ_DATA = 0x02

ASP_SYNC = 0xA5
ASP_HEADER_LEN = 8
ASP_CRC_LEN = 2
ASP_MIN_FRAME_LEN = ASP_HEADER_LEN + ASP_CRC_LEN


class AspTransport:
    def read_status(self) -> tuple[int, int, int]:
        raise NotImplementedError

    def read_data(self, rx_len: int) -> bytes:
        raise NotImplementedError

    def write_data(self, payload: bytes) -> None:
        raise NotImplementedError

    def is_serial_link(self) -> bool:
        return False


class SpiTransport(AspTransport):
    def __init__(self, spi_bus: int = 0, spi_device: int = 0, spi_speed_hz: int = 5_000_000):
        try:
            import spidev as _spidev
        except ModuleNotFoundError as exc:
            raise RuntimeError("Install python package 'spidev' for SPI backend.") from exc

        self.spi = _spidev.SpiDev()
        self.spi.open(spi_bus, spi_device)
        self.spi.max_speed_hz = spi_speed_hz
        self.spi.mode = 0

    def read_status(self) -> tuple[int, int, int]:
        resp = self.spi.xfer2([CMD_READ_STATUS, 0x00, 0x00, 0x00, 0x00])
        version = resp[1]
        status = resp[2]
        rx_len = (resp[3] << 8) | resp[4]
        return version, status, rx_len

    def read_data(self, rx_len: int) -> bytes:
        if rx_len <= 0:
            return b""
        resp = self.spi.xfer2([CMD_READ_DATA] + [0x00] * rx_len)
        return bytes(resp[1:])

    def write_data(self, payload: bytes) -> None:
        self.spi.xfer2([CMD_WRITE_DATA] + list(payload))

    def is_serial_link(self) -> bool:
        return True


class AspFrameParser:
    def __init__(self, max_payload_len: int = 2048):
        self.max_payload_len = max_payload_len
        self._buffer = bytearray()

    def feed(self, chunk: bytes) -> None:
        self._buffer.extend(chunk)

    def pop(self) -> Optional[bytes]:
        while True:
            if len(self._buffer) < ASP_MIN_FRAME_LEN:
                return None

            if self._buffer[0] != ASP_SYNC:
                idx = self._buffer.find(ASP_SYNC)
                if idx < 0:
                    self._buffer.clear()
                    return None
                del self._buffer[:idx]
                if len(self._buffer) < ASP_MIN_FRAME_LEN:
                    return None

            payload_len = (self._buffer[6] << 8) | self._buffer[7]
            if payload_len > self.max_payload_len:
                del self._buffer[0]
                continue

            frame_len = ASP_HEADER_LEN + payload_len + ASP_CRC_LEN
            if len(self._buffer) < frame_len:
                return None

            frame = bytes(self._buffer[:frame_len])
            del self._buffer[:frame_len]
            return frame


class DmaTransport(AspTransport):
    def __init__(
        self,
        dma_h2c: str = "/dev/xdma0_h2c_0",
        dma_c2h: str = "/dev/xdma0_c2h_0",
        dma_read_chunk: int = 4096,
        max_payload_len: int = 2048,
    ):
        if not (os.path.exists(dma_h2c) and os.path.exists(dma_c2h)):
            raise FileNotFoundError(f"DMA nodes missing: {dma_h2c}, {dma_c2h}")
        self.h2c_fd = os.open(dma_h2c, os.O_WRONLY)
        self.c2h_fd = os.open(dma_c2h, os.O_RDONLY | os.O_NONBLOCK)
        self.dma_read_chunk = dma_read_chunk
        self.parser = AspFrameParser(max_payload_len=max_payload_len)
        self.queue: deque[bytes] = deque()

    def _drain(self, max_reads: int = 32) -> None:
        for _ in range(max_reads):
            try:
                chunk = os.read(self.c2h_fd, self.dma_read_chunk)
                if not chunk:
                    break
                self.parser.feed(chunk)
                while True:
                    frame = self.parser.pop()
                    if frame is None:
                        break
                    self.queue.append(frame)
            except BlockingIOError:
                break
            except OSError as exc:
                if exc.errno in (errno.EAGAIN, errno.EWOULDBLOCK):
                    break
                raise

    def read_status(self) -> tuple[int, int, int]:
        self._drain()
        if self.queue:
            return 1, 0x01, len(self.queue[0])
        return 1, 0x00, 0

    def read_data(self, rx_len: int) -> bytes:
        self._drain()
        if not self.queue:
            return b""
        return self.queue.popleft()

    def write_data(self, payload: bytes) -> None:
        total = 0
        view = memoryview(payload)
        while total < len(payload):
            written = os.write(self.h2c_fd, view[total:])
            if written <= 0:
                raise RuntimeError("DMA write made no progress")
            total += written


def crc16_xmodem(data: bytes, seed: int = 0x0000) -> int:
    crc = seed
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


class TunBridge:
    def __init__(
        self,
        transport: AspTransport,
        tun_name: str,
        tun_cidr: str,
        crc_mode: str,
        loop_sleep_ms: float,
    ):
        self.transport = transport
        self.loop_sleep_sec = loop_sleep_ms / 1000.0
        self.crc_enabled = self._crc_enabled(crc_mode)
        self.tun_fd = os.open("/dev/net/tun", os.O_RDWR | os.O_NONBLOCK)
        ifr = struct.pack("16sH", tun_name.encode("utf-8"), IFF_TUN | IFF_NO_PI)
        ioctl(self.tun_fd, TUNSETIFF, ifr)
        os.system(f"ip link set {tun_name} up")
        os.system(f"ip addr replace {tun_cidr} dev {tun_name}")

    def _crc_enabled(self, mode: str) -> bool:
        mode = mode.lower()
        if mode == "on":
            return True
        if mode == "off":
            return False
        if mode == "auto":
            return self.transport.is_serial_link()
        raise ValueError(f"Unsupported crc mode: {mode}")

    def _build_frame(self, payload: bytes, seq: int) -> bytes:
        header = struct.pack(">BBBBHH", ASP_SYNC, 1, 0, 2, seq, len(payload))
        frame = header + payload
        crc = crc16_xmodem(frame) if self.crc_enabled else 0x0000
        return frame + struct.pack(">H", crc)

    def _payload_from_frame(self, frame: bytes) -> Optional[bytes]:
        if len(frame) < ASP_MIN_FRAME_LEN or frame[0] != ASP_SYNC:
            return None
        payload_len = (frame[6] << 8) | frame[7]
        expected = ASP_HEADER_LEN + payload_len + ASP_CRC_LEN
        if len(frame) < expected:
            return None
        if self.crc_enabled:
            rx_crc = (frame[expected - 2] << 8) | frame[expected - 1]
            calc = crc16_xmodem(frame[: expected - ASP_CRC_LEN])
            if rx_crc != calc:
                return None
        return frame[ASP_HEADER_LEN : ASP_HEADER_LEN + payload_len]

    def run(self) -> None:
        seq = 0
        while True:
            try:
                pkt = os.read(self.tun_fd, 2048)
                if pkt:
                    self.transport.write_data(self._build_frame(pkt, seq))
                    seq = (seq + 1) & 0xFFFF
            except BlockingIOError:
                pass

            _, _, rx_len = self.transport.read_status()
            if rx_len > 0:
                frame = self.transport.read_data(rx_len)
                payload = self._payload_from_frame(frame)
                if payload is not None:
                    os.write(self.tun_fd, payload)

            time.sleep(self.loop_sleep_sec)


def select_transport(args: argparse.Namespace) -> AspTransport:
    if args.backend == "spi":
        return SpiTransport(args.spi_bus, args.spi_device, args.spi_speed_hz)
    if args.backend == "dma":
        return DmaTransport(args.dma_h2c, args.dma_c2h, args.dma_read_chunk, args.max_payload_len)
    # auto
    if os.path.exists(args.dma_h2c) and os.path.exists(args.dma_c2h):
        return DmaTransport(args.dma_h2c, args.dma_c2h, args.dma_read_chunk, args.max_payload_len)
    return SpiTransport(args.spi_bus, args.spi_device, args.spi_speed_hz)


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="AbstractX unified TUN bridge (SPI/DMA)")
    p.add_argument("--backend", choices=["auto", "spi", "dma"], default="auto")
    p.add_argument("--spi-bus", type=int, default=0)
    p.add_argument("--spi-device", type=int, default=0)
    p.add_argument("--spi-speed-hz", type=int, default=5_000_000)
    p.add_argument("--dma-h2c", default="/dev/xdma0_h2c_0")
    p.add_argument("--dma-c2h", default="/dev/xdma0_c2h_0")
    p.add_argument("--dma-read-chunk", type=int, default=4096)
    p.add_argument("--max-payload-len", type=int, default=2048)
    p.add_argument("--tun-name", default="tun0")
    p.add_argument("--tun-cidr", default="10.0.0.1/24")
    p.add_argument("--crc-mode", choices=["auto", "on", "off"], default="auto")
    p.add_argument("--loop-sleep-ms", type=float, default=5.0)
    return p.parse_args()


def main() -> int:
    args = parse_args()
    bridge = TunBridge(
        transport=select_transport(args),
        tun_name=args.tun_name,
        tun_cidr=args.tun_cidr,
        crc_mode=args.crc_mode,
        loop_sleep_ms=args.loop_sleep_ms,
    )
    try:
        bridge.run()
    except KeyboardInterrupt:
        print("\nExiting bridge")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
