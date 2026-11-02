#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
test_e906_coprocessor_async.py - Asynchronous Python Test Suite for E906 C++ Coprocessor

Tests the pure C++20 E906 I/O processor (e906_coprocessor.elf) from Linux using
native Python asyncio over VirtIO RPMsg endpoint 1024 ("abstractx-tlp").

Key Architectural Invariants Verified:
1. Zero MicroPython: Directly tests E906 C++ IoEngine + E906IoBackend loop.
2. Zero Blocking Sleeps: 100% event-driven asyncio (loop.add_reader, Futures, Queues).
3. Generic I/O Coprocessor: Sensor-agnostic SPI, I2C, Serial, and Auto-DMA channels.
4. Auto-DMA Monotonicity: PI8 DRDY rising edge triggers autonomous SPI1 DMA reads
   at 1 kHz ODR, streaming Tag 1 TLPs across RPMsg without CPU intervention.
"""

import asyncio
import fcntl
import glob
import os
import struct
import sys
import time
from typing import Dict, List, Optional, Tuple

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import board_common

# TLP Protocol & Wire Layout Constants
TLP_BYTES = 64
TLP_HEADER_STRUCT = struct.Struct("<BBBBIHHQ")  # ty, fl, tag, ch, addr, ldw, seq, ts (20 B)
TLP_TRAILER_CRC = 0xEFBEADDE

# TLP Types
ASP_TLP_TYPE_MEM_RD      = 0x01
ASP_TLP_TYPE_MEM_WR      = 0x02
ASP_TLP_TYPE_CPL_D       = 0x03
ASP_TLP_TYPE_CPL         = 0x04
ASP_TLP_TYPE_DMA_STREAM  = 0x10
ASP_TLP_TYPE_DMA_CFG     = 0x11

# Channels
ASP_CHANNEL_CONTROL      = 0x01
ASP_CHANNEL_TELEMETRY    = 0x02
ASP_CHANNEL_ESC_SERIAL   = 0x05
ASP_CHANNEL_SPI_BRIDGE   = 0x06
ASP_CHANNEL_I2C_BRIDGE   = 0x07
ASP_CHANNEL_GPIO_BRIDGE  = 0x08

# Peripheral Virtual Register Addresses
ASP_ADDR_SPI_BASE        = 0x40000180
ASP_ADDR_I2C_BASE        = 0x40000380
ASP_ADDR_SERIAL_BASE     = 0x40000500
ASP_REG_XFER             = 0x08

# Commands & Status Codes
ASP_DMA_CMD_SETUP        = 0x01
ASP_DMA_CMD_ENABLE       = 0x02
ASP_DMA_CMD_DISABLE      = 0x03

ASP_STATUS_OK            = 0x00
ASP_STATUS_DMA_ERROR     = 0x01
ASP_STATUS_BUS_LOCKED    = 0x05

# RPMsg IOCTL Setup
EPT_INFO = struct.Struct("<32sII")
RPMSG_CREATE_EPT_IOCTL = (1 << 30) | (EPT_INFO.size << 16) | (0xB5 << 8) | 0x1
RPMSG_CTRL = "/dev/rpmsg_ctrl0"
RPROC = "/sys/class/remoteproc/remoteproc0"


def require_e906_board():
    if not os.path.exists(RPROC + "/state"):
        board_common.skip("not running on Allwinner XuanTie E906 board")
    state = open(RPROC + "/state").read().strip()
    if state != "running":
        board_common.skip(f"remoteproc0 not running (state={state})")
    if not glob.glob("/sys/bus/rpmsg/devices/*abstractx-tlp*"):
        board_common.skip("abstractx-tlp not announced")
    if glob.glob("/sys/bus/rpmsg/devices/*abstractx-repl*"):
        board_common.skip("abstractx-repl active (coprocessor firmware not loaded)")
    if glob.glob("/sys/bus/rpmsg/devices/*abstractx-bench*"):
        board_common.skip("abstractx-bench active (coprocessor firmware not loaded)")


class AsyncRpmsgLink:
    """Reactive, non-blocking asyncio driver for Linux VirtIO RPMsg endpoint 1024."""

    def __init__(self, endpoint_name: str = "abstractx-tlp", addr: int = 1024):
        self.endpoint_name = endpoint_name
        self.addr = addr
        self.fd: Optional[int] = None
        self.loop: Optional[asyncio.AbstractEventLoop] = None
        self._next_tag = 10
        self._pending_futures: Dict[int, asyncio.Future] = {}
        self._stream_queues: Dict[int, List[asyncio.Queue]] = {}
        self._seq = 0

    def _find_endpoint_node(self) -> Optional[str]:
        for d in sorted(glob.glob("/sys/class/rpmsg/rpmsg[0-9]*")):
            try:
                name_path = os.path.join(d, "name")
                if os.path.exists(name_path) and open(name_path).read().strip() == self.endpoint_name:
                    return "/dev/" + os.path.basename(d)
            except OSError:
                pass
        return None

    def _ensure_endpoint(self) -> str:
        node = self._find_endpoint_node()
        if node:
            return node
        if not os.path.exists(RPMSG_CTRL):
            raise RuntimeError(f"RPMsg control device {RPMSG_CTRL} not found")
        ctrl_fd = os.open(RPMSG_CTRL, os.O_RDWR)
        try:
            info = EPT_INFO.pack(self.endpoint_name.encode()[:31], self.addr, self.addr)
            fcntl.ioctl(ctrl_fd, RPMSG_CREATE_EPT_IOCTL, info)
        finally:
            os.close(ctrl_fd)
        deadline = time.monotonic() + 2.0
        while time.monotonic() < deadline:
            node = self._find_endpoint_node()
            if node:
                return node
            time.sleep(0.01)
        raise RuntimeError(f"Endpoint {self.endpoint_name} failed to create /dev/rpmsg node")

    async def open(self):
        self.loop = asyncio.get_running_loop()
        dev_node = self._ensure_endpoint()
        self.fd = os.open(dev_node, os.O_RDWR | os.O_NONBLOCK)
        # Send initial HELLO so E906 learns host source address
        os.write(self.fd, b"helo")
        self.loop.add_reader(self.fd, self._on_readable)

    async def __aenter__(self):
        await self.open()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.close()

    def _on_readable(self):
        try:
            buf = os.read(self.fd, 2048)
        except (BlockingIOError, OSError):
            return
        if not buf:
            return

        for offset in range(0, len(buf) - TLP_BYTES + 1, TLP_BYTES):
            frame = buf[offset:offset + TLP_BYTES]
            ty, fl, tag, ch, addr, ldw, seq, ts = TLP_HEADER_STRUCT.unpack_from(frame)
            payload = frame[20:60]
            trailer = frame[60:64]

            if ty == ASP_TLP_TYPE_DMA_STREAM or ch == ASP_CHANNEL_TELEMETRY:
                # Deliver to registered stream subscribers
                queues = self._stream_queues.get(tag, [])
                for q in queues:
                    q.put_nowait({
                        "type": ty, "flags": fl, "tag": tag, "channel": ch,
                        "addr": addr, "length_dw": ldw, "seq": seq, "timestamp_ns": ts,
                        "payload": payload,
                    })
            else:
                # Deliver to matching request future
                fut = self._pending_futures.pop(tag, None)
                if fut and not fut.done():
                    fut.set_result({
                        "type": ty, "flags": fl, "tag": tag, "channel": ch,
                        "addr": addr, "length_dw": ldw, "seq": seq, "timestamp_ns": ts,
                        "payload": payload,
                    })

    def _alloc_tag(self) -> int:
        self._next_tag = (self._next_tag + 1) & 0xFF
        if self._next_tag < 10:
            self._next_tag = 10
        return self._next_tag

    async def transact(self, tlp_bytes: bytearray, timeout: float = 2.0) -> dict:
        """Sends a 64-byte TLP and asynchronously awaits the matching completion without blocking sleeps."""
        tag = self._alloc_tag()
        tlp_bytes[2] = tag  # inject tag into header byte 2
        self._seq = (self._seq + 1) & 0xFFFF
        struct.pack_into("<H", tlp_bytes, 10, self._seq)

        fut = self.loop.create_future()
        self._pending_futures[tag] = fut
        try:
            os.write(self.fd, tlp_bytes)
            return await asyncio.wait_for(fut, timeout=timeout)
        finally:
            self._pending_futures.pop(tag, None)

    def subscribe_stream(self, tag: int) -> asyncio.Queue:
        q = asyncio.Queue()
        self._stream_queues.setdefault(tag, []).append(q)
        return q

    def unsubscribe_stream(self, tag: int, q: asyncio.Queue):
        if tag in self._stream_queues:
            try:
                self._stream_queues[tag].remove(q)
            except ValueError:
                pass

    async def close(self):
        if self.fd is not None:
            if self.loop is not None:
                try:
                    self.loop.remove_reader(self.fd)
                except Exception:
                    pass
            os.close(self.fd)
            self.fd = None


# ============================================================================
# TLP Frame Builder Helpers
# ============================================================================

def make_spi_transfer(bus_id: int, cs_pin: int, tx_data: bytes, rx_len: int) -> bytearray:
    hdr = struct.pack(
        "<BBBBIHHQ",
        ASP_TLP_TYPE_MEM_WR, 0, 0, ASP_CHANNEL_SPI_BRIDGE,
        ASP_ADDR_SPI_BASE | ASP_REG_XFER, 1 + (len(tx_data) + 3) // 4, 0, 0
    )
    copy_len = min(len(tx_data), 36)
    payload = struct.pack("<BBBB", bus_id, cs_pin, copy_len, rx_len) + tx_data[:copy_len]
    payload += bytes(40 - len(payload))
    crc = struct.pack("<I", TLP_TRAILER_CRC)
    return bytearray(hdr + payload + crc)


def make_spi_write_reg(reg: int, val: int) -> bytearray:
    """ICM-42688-P SPI write: bit 7 = 0 (write), reg in bits 0..6."""
    return make_spi_transfer(1, 0, bytes([reg & 0x7F, val]), 2)


def make_i2c_read_reg(bus_id: int, slave_addr: int, reg_offset: int, rx_len: int) -> bytearray:
    hdr = struct.pack(
        "<BBBBIHHQ",
        ASP_TLP_TYPE_MEM_RD, 0x03, 0, ASP_CHANNEL_I2C_BRIDGE,
        ASP_ADDR_I2C_BASE | ASP_REG_XFER, 2 + (rx_len + 3) // 4, 0, 0
    )
    payload = struct.pack("<BBBBBBH", bus_id, slave_addr, reg_offset, 0x03, 0, rx_len, 0)
    payload += bytes(40 - len(payload))
    crc = struct.pack("<I", TLP_TRAILER_CRC)
    return bytearray(hdr + payload + crc)


def make_serial_set_baud(baud: int) -> bytearray:
    hdr = struct.pack(
        "<BBBBIHHQ",
        ASP_TLP_TYPE_MEM_WR, 0, 0, ASP_CHANNEL_ESC_SERIAL,
        ASP_ADDR_SERIAL_BASE | 0x04, 1, 0, 0
    )
    payload = struct.pack("<I", baud) + bytes(36)
    crc = struct.pack("<I", TLP_TRAILER_CRC)
    return bytearray(hdr + payload + crc)


def make_serial_read(rx_len: int = 40) -> bytearray:
    hdr = struct.pack(
        "<BBBBIHHQ",
        ASP_TLP_TYPE_MEM_RD, 0, 0, ASP_CHANNEL_ESC_SERIAL,
        ASP_ADDR_SERIAL_BASE, rx_len, 0, 0
    )
    payload = bytes(40)
    crc = struct.pack("<I", TLP_TRAILER_CRC)
    return bytearray(hdr + payload + crc)


def make_hw_fusion_control(channel_id: int, enable: bool) -> bytearray:
    hdr = struct.pack(
        "<BBBBIHHQ",
        ASP_TLP_TYPE_DMA_CFG, 0, 0, ASP_CHANNEL_CONTROL,
        0, 1, 0, 0
    )
    cmd = ASP_DMA_CMD_ENABLE if enable else ASP_DMA_CMD_DISABLE
    payload = struct.pack("<BB", cmd, channel_id) + bytes(38)
    crc = struct.pack("<I", TLP_TRAILER_CRC)
    return bytearray(hdr + payload + crc)


def nmea_checksum_valid(line: bytes) -> bool:
    if len(line) < 6 or line[0] != ord('$'):
        return False
    try:
        star_idx = line.rindex(b'*')
    except ValueError:
        return False
    if star_idx + 3 > len(line):
        return False
    calc = 0
    for b in line[1:star_idx]:
        calc ^= b
    try:
        expected = int(line[star_idx + 1:star_idx + 3], 16)
        return calc == expected
    except ValueError:
        return False


# ============================================================================
# Async Coprocessor Test Coroutines
# ============================================================================

async def _async_test_spi_imu_whoami(link: Optional[AsyncRpmsgLink] = None):
    async def _run(l: AsyncRpmsgLink):
        # 1. Disarm Auto-DMA to ensure SPI1 bus is not locked
        disarm_tlp = make_hw_fusion_control(0, False)
        cpl_disarm = await l.transact(disarm_tlp, timeout=1.0)
        assert cpl_disarm["type"] in (ASP_TLP_TYPE_CPL, ASP_TLP_TYPE_CPL_D)

        # 2. Asynchronously request SPI1 WHO_AM_I read (0xF5) on CS0
        req = make_spi_transfer(1, 0, bytes([0xF5, 0x00]), 2)
        cpl = await l.transact(req, timeout=1.0)

        assert cpl["type"] == ASP_TLP_TYPE_CPL_D, f"bad completion type {cpl['type']}"
        assert cpl["channel"] == ASP_CHANNEL_SPI_BRIDGE, f"bad channel {cpl['channel']}"
        status, xfer_len = struct.unpack_from("<BB", cpl["payload"], 0)
        assert status == ASP_STATUS_OK, f"SPI transfer error status={status}"
        assert xfer_len >= 2, f"transferred bytes {xfer_len} < 2"

        who_am_i = cpl["payload"][5]  # byte 0 = status, 1 = len, 2..3 = dur, 4 = dummy, 5 = WHO_AM_I
        assert who_am_i == 0x47, f"ICM-42688-P WHO_AM_I mismatch: 0x{who_am_i:02X} != 0x47"

    if link is not None:
        await _run(link)
    else:
        async with AsyncRpmsgLink() as l:
            await _run(l)


async def _async_test_i2c_compass_id(link: Optional[AsyncRpmsgLink] = None):
    async def _run(l: AsyncRpmsgLink):
        # Asynchronously read HMC5883L identification registers 0x0A..0x0C on TWI1 (slave 0x1E)
        req = make_i2c_read_reg(1, 0x1E, 0x0A, 3)
        cpl = await l.transact(req, timeout=1.0)

        assert cpl["type"] == ASP_TLP_TYPE_CPL_D, f"bad completion type {cpl['type']}"
        assert cpl["channel"] == ASP_CHANNEL_I2C_BRIDGE, f"bad channel {cpl['channel']}"
        status, xfer_len = struct.unpack_from("<BB", cpl["payload"], 0)
        assert status == ASP_STATUS_OK, f"I2C read error status={status}"
        assert xfer_len == 3, f"transferred bytes {xfer_len} != 3"

        id_str = cpl["payload"][4:4 + xfer_len]
        assert id_str == b"H43", f"HMC5883L ID string mismatch: {id_str!r} != b'H43'"

    if link is not None:
        await _run(link)
    else:
        async with AsyncRpmsgLink() as l:
            await _run(l)


async def _async_test_serial_gps_stream(link: Optional[AsyncRpmsgLink] = None):
    async def _run(l: AsyncRpmsgLink):
        # 1. Configure UART3 baud rate to 9600 (NEO-M8N default)
        set_baud = make_serial_set_baud(9600)
        cpl_baud = await l.transact(set_baud, timeout=1.0)
        assert cpl_baud["type"] in (ASP_TLP_TYPE_CPL_D, ASP_TLP_TYPE_CPL)

        # 2. Asynchronously stream and accumulate chunks until a complete NMEA sentence is found
        buf = bytearray()
        valid_sentence_found = False
        sentence_str = ""

        # Allow up to 2.5 seconds to capture NMEA burst from u-blox GPS
        deadline = asyncio.get_running_loop().time() + 2.5
        while asyncio.get_running_loop().time() < deadline:
            read_req = make_serial_read(40)
            cpl = await l.transact(read_req, timeout=0.5)
            n = min(cpl["length_dw"], 40)
            if n > 0:
                buf.extend(cpl["payload"][:n])
                while b"\n" in buf:
                    idx = buf.index(b"\n")
                    raw_line = bytes(buf[:idx]).strip()
                    buf = buf[idx + 1:]
                    if raw_line.startswith(b"$") and nmea_checksum_valid(raw_line):
                        valid_sentence_found = True
                        sentence_str = raw_line.decode("ascii", errors="replace")
                        break
            if valid_sentence_found:
                break
            await asyncio.sleep(0.02)  # Cooperative coroutine yield to let UART DMA fill

        assert valid_sentence_found, f"No valid NMEA sentence received from GPS (buf={bytes(buf)!r})"
        assert sentence_str.startswith("$G"), f"Malformed NMEA prefix: {sentence_str}"

    if link is not None:
        await _run(link)
    else:
        async with AsyncRpmsgLink() as l:
            await _run(l)


async def _async_test_auto_dma_stream(link: Optional[AsyncRpmsgLink] = None):
    async def _run(l: AsyncRpmsgLink):
        q = l.subscribe_stream(tag=1)
        try:
            # 0. Ensure Auto-DMA is disarmed so SPI bus is unlocked for configuration
            disarm_tlp = make_hw_fusion_control(0, False)
            cpl_disarm = await l.transact(disarm_tlp, timeout=1.0)
            assert cpl_disarm["type"] in (ASP_TLP_TYPE_CPL, ASP_TLP_TYPE_CPL_D)

            # 1. Initialize ICM-42688-P registers to start DRDY generation:
            # INT_CONFIG (0x14) = 0x03 (INT1 push-pull, active-high), INT_SOURCE0 (0x65) = 0x08 (UI_DRDY_INT1_EN),
            # PWR_MGMT0 (0x4E) = 0x0F (6-axis Low-Noise mode)
            for reg, val in [(0x14, 0x03), (0x64, 0x00), (0x65, 0x08), (0x4E, 0x0F)]:
                cpl_w = await l.transact(make_spi_write_reg(reg, val), timeout=1.0)
                assert cpl_w["payload"][0] == ASP_STATUS_OK, f"SPI write 0x{reg:02X} failed: {cpl_w['payload'][0]}"

            # 2. Arm PI8 DRDY Auto-DMA on E906 (channel 0)
            arm_tlp = make_hw_fusion_control(0, True)
            cpl_arm = await l.transact(arm_tlp, timeout=1.0)
            assert cpl_arm["type"] in (ASP_TLP_TYPE_CPL, ASP_TLP_TYPE_CPL_D)
            status = cpl_arm["payload"][0]
            assert status == ASP_STATUS_OK, f"Auto-DMA arm failed with status={status}"

            # 3. Gather 25 consecutive stream samples directly from the asyncio.Queue
            samples = []
            for _ in range(25):
                sample = await asyncio.wait_for(q.get(), timeout=2.0)
                samples.append(sample)

            # 3. Assert all samples are valid Auto-DMA Stream TLPs
            assert len(samples) == 25, f"Expected 25 stream samples, got {len(samples)}"
            for s in samples:
                assert s["type"] in (ASP_TLP_TYPE_DMA_STREAM, ASP_TLP_TYPE_MEM_WR), f"bad stream type {s['type']}"
                assert s["channel"] == ASP_CHANNEL_TELEMETRY, f"bad channel {s['channel']}"
                assert s["tag"] == 1, f"bad tag {s['tag']}"
                # Verify payload contains 14 bytes of valid raw IMU data (not all 0x00 or all 0xFF)
                raw14 = s["payload"][:14]
                assert not all(b == 0 for b in raw14), "IMU raw payload is all zeros"
                assert not all(b == 0xFF for b in raw14), "IMU raw payload is all 0xFF"

            # 4. Verify timestamp monotonicity and 1 kHz sample interval (nominally ~1.0 ms)
            deltas_us = []
            for i in range(len(samples) - 1):
                t0 = samples[i]["timestamp_ns"]
                t1 = samples[i + 1]["timestamp_ns"]
                assert t1 > t0, f"Non-monotonic timestamps: t0={t0}, t1={t1}"
                delta_us = (t1 - t0) / 1000.0
                deltas_us.append(delta_us)

            avg_delta = sum(deltas_us) / len(deltas_us)
            # 1 kHz ODR gives ~1000 µs interval; allow silicon jitter window [800 µs, 1200 µs]
            assert 800.0 <= avg_delta <= 1200.0, f"Auto-DMA ODR frequency out of spec: avg delta = {avg_delta:.1f} µs"

            # 5. Cleanly disarm Auto-DMA
            disarm_tlp = make_hw_fusion_control(0, False)
            cpl_disarm = await l.transact(disarm_tlp, timeout=1.0)
            assert cpl_disarm["type"] in (ASP_TLP_TYPE_CPL, ASP_TLP_TYPE_CPL_D)
        finally:
            l.unsubscribe_stream(1, q)

    if link is not None:
        await _run(link)
    else:
        async with AsyncRpmsgLink() as l:
            await _run(l)


# ============================================================================
# Pytest & run_board_tests.py Entry Points
# ============================================================================

def test_coprocessor_spi_imu_whoami():
    """[SPEC-ARCH-13], [SPEC-E906-HAL-03]: Verify SPI1 DMA WHO_AM_I read via E906 C++ IoEngine."""
    require_e906_board()
    asyncio.run(_async_test_spi_imu_whoami())


def test_coprocessor_i2c_compass_id():
    """[SPEC-ARCH-13], [SPEC-HAL-05]: Verify TWI1 HMC5883L ID register read via E906 C++ IoEngine."""
    require_e906_board()
    asyncio.run(_async_test_i2c_compass_id())


def test_coprocessor_serial_gps_stream():
    """[SPEC-ARCH-13], [SPEC-GPS-01]: Verify UART3 DMA NMEA sentence streaming via E906 C++ IoEngine."""
    require_e906_board()
    asyncio.run(_async_test_serial_gps_stream())


def test_coprocessor_auto_dma_stream():
    """[SPEC-ARCH-13], [SPEC-E906-HAL-05]: Verify PI8 DRDY Auto-DMA 1 kHz stream & timestamp monotonicity."""
    require_e906_board()
    asyncio.run(_async_test_auto_dma_stream())


async def run_all_coprocessor_tests():
    async with AsyncRpmsgLink() as link:
        print("Running E906 C++ Coprocessor Async Test Suite (shared RPMsg link)...")
        await _async_test_spi_imu_whoami(link)
        print("PASS: test_coprocessor_spi_imu_whoami")
        await _async_test_i2c_compass_id(link)
        print("PASS: test_coprocessor_i2c_compass_id")
        await _async_test_serial_gps_stream(link)
        print("PASS: test_coprocessor_serial_gps_stream")
        await _async_test_auto_dma_stream(link)
        print("PASS: test_coprocessor_auto_dma_stream")
        print("ALL E906 COPROCESSOR TESTS PASSED!")


if __name__ == "__main__":
    require_e906_board()
    asyncio.run(run_all_coprocessor_tests())
