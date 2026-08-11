#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""AbstractX 64-Byte TLP Host Scheduler and Packet Dispatcher.

Implements:
1. Multi-Queue Transmit Scheduler (Round-Robin & Priority Scheduling).
2. Split-Transaction Tag Correlator for MemRd / CplD operations.
3. Target Address Demux Dispatcher for autonomous DMA_Stream packets (IMU, UART ESC).
"""

from __future__ import annotations

import dataclasses
import queue
import struct
import threading
import time
from typing import Callable, Dict, Optional, Tuple

# TLP Constants
TLP_SIZE = 64
TLP_TYPE_MEM_RD = 0x01
TLP_TYPE_MEM_WR = 0x02
TLP_TYPE_CPL_D = 0x03
TLP_TYPE_CPL = 0x04
TLP_TYPE_DMA_STREAM = 0x10

CMD_READ_STATUS = 0xA0
CMD_WRITE_BURST = 0xA1
CMD_READ_BURST = 0xA2

# Well-known Wishbone Device Addresses
ADDR_IMU = 0x40000100
ADDR_DSHOT = 0x40000300
ADDR_UART_ESC = 0x40000500


@dataclasses.dataclass
class TLP64:
    tlp_type: int
    flags: int
    tag: int
    channel: int
    target_address: int
    length_dw: int
    sequence: int
    timestamp_ns: int
    payload: bytes  # Up to 40 bytes
    crc32: int = 0xDEADBEEF

    def pack(self) -> bytes:
        """Packs structure into 64-byte big-endian TLP container with zero-padding."""
        valid_payload = self.payload[:40]
        padded_payload = valid_payload.ljust(40, b"\x00")
        header = struct.pack(
            ">BBBBIHHQ40sI",
            self.tlp_type,
            self.flags,
            self.tag,
            self.channel,
            self.target_address,
            self.length_dw,
            self.sequence,
            self.timestamp_ns,
            padded_payload,
            self.crc32,
        )
        return header

    @classmethod
    def unpack(cls, raw: bytes) -> TLP64:
        """Unpacks a 64-byte raw binary TLP container."""
        if len(raw) != TLP_SIZE:
            raise ValueError(f"Invalid TLP size: {len(raw)} bytes (expected 64)")
        (
            tlp_type,
            flags,
            tag,
            channel,
            target_address,
            length_dw,
            sequence,
            timestamp_ns,
            padded_payload,
            crc32,
        ) = struct.unpack(">BBBBIHHQ40sI", raw)


        # Extract only valid payload bytes as indicated by length_dw
        valid_bytes_count = min(length_dw * 4, 40)
        payload = padded_payload[:valid_bytes_count]

        return cls(
            tlp_type=tlp_type,
            flags=flags,
            tag=tag,
            channel=channel,
            target_address=target_address,
            length_dw=length_dw,
            sequence=sequence,
            timestamp_ns=timestamp_ns,
            payload=payload,
            crc32=crc32,
        )


class HostTLPScheduler:
    """Host-side Multi-Queue Priority Scheduler and Ingress Dispatcher."""

    def __init__(self, transport: Optional[object] = None):
        self.transport = transport
        self._lock = threading.Lock()

        # Priority Transmit Queues
        # Queue 0: High Priority (Control MemRd / MemWr)
        # Queue 1: Medium Priority (Serial ESC)
        # Queue 2: Low Priority (Telemetry Config / Debug)
        self.tx_queues: Dict[int, queue.Queue[TLP64]] = {
            0: queue.Queue(),
            1: queue.Queue(),
            2: queue.Queue(),
        }

        # Tag-based Completion Table for MemRd split transactions
        self._pending_reads: Dict[int, threading.Event] = {}
        self._read_results: Dict[int, TLP64] = {}
        self._next_tag = 1

        # Peripheral Subscribers for DMA_Stream (Target Address -> Callback)
        self._subscribers: Dict[int, Callable[[TLP64], None]] = {}

    def register_subscriber(self, wishbone_base_addr: int, callback: Callable[[TLP64], None]):
        """Registers a callback for autonomous DMA_Stream packets matching a device address."""
        self._subscribers[wishbone_base_addr] = callback

    def _allocate_tag(self) -> int:
        with self._lock:
            tag = self._next_tag
            self._next_tag = (self._next_tag % 255) + 1
            return tag


    def enqueue_tlp(self, tlp: TLP64, priority: int = 1):
        """Enqueues a 64-byte TLP into host transmit queues."""
        priority = max(0, min(2, priority))
        self.tx_queues[priority].put(tlp)

    def mem_rd(self, address: int, length_dw: int = 1, timeout: float = 1.0) -> Optional[TLP64]:
        """Issues a synchronous MemRd split transaction and waits for CplD response."""
        tag = self._allocate_tag()
        event = threading.Event()

        with self._lock:
            self._pending_reads[tag] = event

        req = TLP64(
            tlp_type=TLP_TYPE_MEM_RD,
            flags=0,
            tag=tag,
            channel=0x01,
            target_address=address,
            length_dw=length_dw,
            sequence=0,
            timestamp_ns=0,
            payload=b"",
        )
        self.enqueue_tlp(req, priority=0)  # High Priority Control

        if event.wait(timeout):
            with self._lock:
                return self._read_results.pop(tag, None)
        else:
            with self._lock:
                self._pending_reads.pop(tag, None)
            return None

    def mem_wr(self, address: int, data: bytes):
        """Issues a posted MemWr TLP to Wishbone target address."""
        length_dw = (len(data) + 3) // 4
        req = TLP64(
            tlp_type=TLP_TYPE_MEM_WR,
            flags=0,
            tag=0,
            channel=0x01,
            target_address=address,
            length_dw=length_dw,
            sequence=0,
            timestamp_ns=0,
            payload=data,
        )
        self.enqueue_tlp(req, priority=0)  # High Priority Control


    def schedule_next_tx_tlp(self) -> Optional[TLP64]:
        """Pops next TLP using strict priority scheduling (Control > Serial > Telemetry)."""
        for prio in (0, 1, 2):
            try:
                return self.tx_queues[prio].get_nowait()
            except queue.Empty:
                continue
        return None

    def dispatch_rx_tlp(self, raw_tlp_bytes: bytes):
        """Demuxes received 64-byte TLP to waiting thread or device subscriber."""
        tlp = TLP64.unpack(raw_tlp_bytes)

        # 1. Check if packet is a MemRd Completion (CplD)
        if tlp.tlp_type == TLP_TYPE_CPL_D:
            with self._lock:
                event = self._pending_reads.pop(tlp.tag, None)
                if event:
                    self._read_results[tlp.tag] = tlp
                    event.set()
            return

        # 2. Route by Target Address / Device ID (IMU, UART ESC, DShot)
        callback = self._subscribers.get(tlp.target_address)
        if callback:
            callback(tlp)


def demo_imu_subscriber(tlp: TLP64):
    print(f"[IMU Telemetry Event] Addr=0x{tlp.target_address:08X} TS={tlp.timestamp_ns}ns ValidBytes={len(tlp.payload)}")


if __name__ == "__main__":
    print("Testing AbstractX Host TLP Scheduler...")
    sched = HostTLPScheduler()
    sched.register_subscriber(ADDR_IMU, demo_imu_subscriber)

    # Simulate IMU Auto-DMA Stream TLP Egress
    imu_tlp = TLP64(
        tlp_type=TLP_TYPE_DMA_STREAM,
        flags=0,
        tag=0,
        channel=0x02,
        target_address=ADDR_IMU,
        length_dw=4,
        sequence=101,
        timestamp_ns=1234567890,
        payload=bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE]),
    )
    packed_bytes = imu_tlp.pack()
    print(f"Packed 64-Byte TLP Size: {len(packed_bytes)} bytes")
    sched.dispatch_rx_tlp(packed_bytes)
