#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

"""Linux userspace and AbstractX FPGA Auto-DMA transport adapters."""
# @impl [SPEC-ZYNQ-IMU-POC-02] hw/zynq7000/testApps/imu_backend_validation_poc/backends.py
# @impl [SPEC-ZYNQ-IMU-POC-03] hw/zynq7000/testApps/imu_backend_validation_poc/backends.py

from __future__ import annotations

from collections import deque
from datetime import timedelta
import mmap
import os
import select
import struct
import time

from imu_validation import BackendError, SampleRecord


SPI_READ = 0x80
IMU_TEMP_DATA1 = 0x1D
IMU_BURST_LENGTH = 14
IMU_CSR_BASE = 0x100
IMU_WB_BASE = 0x40000100
IMU_REG_CTRL = IMU_CSR_BASE + 0x00
IMU_REG_ADDR = IMU_CSR_BASE + 0x04
IMU_REG_LEN = IMU_CSR_BASE + 0x08
IMU_REG_WDATA = IMU_CSR_BASE + 0x0C
IMU_REG_RDATA = IMU_CSR_BASE + 0x10
IMU_REG_STATUS = IMU_CSR_BASE + 0x1C
IMU_REG_END_TIME_HI = IMU_CSR_BASE + 0x30
IMU_REG_END_TIME_LO = IMU_CSR_BASE + 0x34
IMU_REG_SPI_HALF_PERIOD = IMU_CSR_BASE + 0x38
IMU_REG_DRDY_OVERRUN_COUNT = IMU_CSR_BASE + 0x3C
IMU_REG_DATA_BASE = IMU_CSR_BASE + 0x20

DMA_CONTROL = 0x00
DMA_STATUS = 0x04
DMA_IRQ_STATUS = 0x08
DMA_IRQ_ENABLE = 0x0C
DMA_RX_BASE = 0x10
DMA_RX_CAPACITY = 0x14
DMA_RX_HEAD = 0x18
DMA_RX_TAIL = 0x1C
DMA_HARDWARE_ID = 0x40
DMA_HARDWARE_MAGIC = 0x41535036
DMA_PHYSICAL_BASE = 0x1E000000
DMA_MAP_SIZE = 0x02000000
DMA_RING_CAPACITY = 256
DMA_SLOT_SIZE = 64
DMA_CSR_SIZE = 0x10000
PL_CLOCK_HZ = 100_000_000


def normalize_dma_beat_order(memory_record: bytes) -> bytes:
    if len(memory_record) != DMA_SLOT_SIZE:
        raise BackendError(f"expected 64-byte DMA slot, got {len(memory_record)}")
    return b"".join(
        memory_record[offset : offset + 8][::-1]
        for offset in range(0, DMA_SLOT_SIZE, 8)
    )


def ring_pending_count(head: int, tail: int, capacity: int) -> int:
    if capacity < 2 or not 0 <= head < capacity or not 0 <= tail < capacity:
        raise BackendError("invalid DMA ring index or capacity")
    return (tail - head) % capacity


def ring_is_full(head: int, tail: int, capacity: int) -> bool:
    if capacity < 2 or not 0 <= head < capacity or not 0 <= tail < capacity:
        raise BackendError("invalid DMA ring index or capacity")
    return (tail + 1) % capacity == head


def parse_dma_tlp(memory_record: bytes) -> tuple[int, int, int, bytes]:
    frame = normalize_dma_beat_order(memory_record)
    if frame[0:4] != bytes((0x10, 0x00, 0x00, 0x02)):
        raise BackendError(f"unexpected IMU TLP type/channel: {frame[0:4].hex()}")
    if int.from_bytes(frame[4:8], "big") != IMU_WB_BASE:
        raise BackendError("IMU TLP target address mismatch")
    if int.from_bytes(frame[8:10], "big") != 4:
        raise BackendError("IMU TLP payload length is not four DWORDs")
    if frame[34:36] != b"\x00\x00":
        raise BackendError("IMU TLP sensor-payload padding is nonzero")
    if frame[44:60] != bytes(16):
        raise BackendError("IMU TLP reserved padding is nonzero")
    if int.from_bytes(frame[60:64], "big") != 0xDEADBEEF:
        raise BackendError("IMU TLP reserved/CRC word mismatch")

    sequence = int.from_bytes(frame[10:12], "big")
    start_ns = int.from_bytes(frame[12:20], "big")
    end_ns = int.from_bytes(frame[36:44], "big")
    if end_ns < start_ns:
        raise BackendError("IMU TLP completion timestamp precedes its DRDY timestamp")
    transfer_ns = end_ns - start_ns
    return sequence, start_ns, transfer_ns, frame[20:34]


class LinuxUserspaceBackend:
    name = "linux-userspace"
    effective_spi_hz: int | None = None

    def __init__(
        self,
        spi_device: str,
        gpiochip: str,
        gpio_line: int,
        requested_spi_hz: int,
    ) -> None:
        self.spi_device = spi_device
        self.gpiochip = gpiochip
        self.gpio_line = gpio_line
        self.requested_spi_hz = requested_spi_hz
        self.spi = None
        self.gpio_request = None
        self.spi_rate_source = "unavailable-from-spidev"

    def open(self) -> None:
        try:
            import gpiod
            from gpiod.line import Clock, Direction, Edge
            import spidev
        except ImportError as exc:
            raise BackendError(f"required Python hardware module is missing: {exc}") from exc

        try:
            self.spi = spidev.SpiDev()
            bus, device = map(
                int, self.spi_device.removeprefix("/dev/spidev").split(".")
            )
            self.spi.open(bus, device)
            self.spi.mode = 0
            self.spi.bits_per_word = 8
            self.spi.max_speed_hz = self.requested_spi_hz
            settings = gpiod.LineSettings(
                direction=Direction.INPUT,
                edge_detection=Edge.RISING,
                event_clock=Clock.MONOTONIC,
            )
            self.gpio_request = gpiod.request_lines(
                self.gpiochip,
                consumer="abstractx-imu-validation",
                config={self.gpio_line: settings},
                event_buffer_size=4096,
            )
        except (OSError, ValueError, RuntimeError) as exc:
            self.close()
            raise BackendError(f"cannot open Linux SPI/GPIO backend: {exc}") from exc

    def read_register(self, address: int) -> int:
        if self.spi is None:
            raise BackendError("Linux SPI backend is not open")
        response = self.spi.xfer2([address | SPI_READ, 0])
        if len(response) != 2:
            raise BackendError("short SPI register read")
        return response[1]

    def write_register(self, address: int, value: int) -> None:
        if self.spi is None:
            raise BackendError("Linux SPI backend is not open")
        response = self.spi.xfer2([address & 0x7F, value & 0xFF])
        if len(response) != 2:
            raise BackendError("short SPI register write")

    def start_samples(self) -> None:
        if self.gpio_request is None:
            raise BackendError("Linux GPIO event source is not open")

    def next_sample(self, timeout_seconds: float) -> SampleRecord:
        if self.spi is None or self.gpio_request is None:
            raise BackendError("Linux userspace backend is not open")

        wait_start_ns = time.monotonic_ns()
        if not self.gpio_request.wait_edge_events(
            timeout=timedelta(seconds=timeout_seconds)
        ):
            raise TimeoutError("timed out waiting for ICM-42688-P DRDY GPIO event")
        events = self.gpio_request.read_edge_events(max_events=1)
        if not events:
            raise BackendError("GPIO event wait completed without an event")

        event = events[0]
        response = self.spi.xfer2([IMU_TEMP_DATA1 | SPI_READ] + [0] * IMU_BURST_LENGTH)
        complete_ns = time.monotonic_ns()
        if len(response) != IMU_BURST_LENGTH + 1:
            raise BackendError("short ICM-42688-P userspace SPI burst")
        return SampleRecord(
            payload=bytes(response[1:]),
            sequence=int(event.line_seqno),
            sequence_bits=32,
            event_timestamp_ns=int(event.timestamp_ns),
            event_clock="CLOCK_MONOTONIC",
            host_wait_start_ns=wait_start_ns,
            host_complete_ns=complete_ns,
        )

    def stop_samples(self) -> None:
        return

    def close(self) -> None:
        close_error: OSError | None = None
        if self.gpio_request is not None:
            try:
                self.gpio_request.release()
            except OSError as exc:
                close_error = exc
            self.gpio_request = None
        if self.spi is not None:
            try:
                self.spi.close()
            except OSError as exc:
                close_error = close_error or exc
            self.spi = None
        if close_error is not None:
            raise BackendError(f"failed to close Linux SPI/GPIO resources: {close_error}") from close_error

    def read_overrun_count(self) -> int | None:
        return None

    def pending_sample_count(self) -> int:
        return 0


class AbstractXDmaBackend:
    name = "abstractx-auto-dma"

    def __init__(self, uio_device: str, requested_spi_hz: int) -> None:
        if not 100_000 <= requested_spi_hz <= 24_000_000:
            raise ValueError("requested SPI speed must be between 100 kHz and 24 MHz")
        self.uio_device = uio_device
        self.requested_spi_hz = requested_spi_hz
        self.fd: int | None = None
        self.csr: mmap.mmap | None = None
        self.dma: mmap.mmap | None = None
        self.pending: deque[tuple[int, int, int, bytes]] = deque()
        self.irq_masked = False
        self.dma_started = False
        self.samples_started = False
        self.ring_full_observations = 0
        self.half_period = max(
            2, (PL_CLOCK_HZ + 2 * requested_spi_hz - 1) // (2 * requested_spi_hz)
        )
        self.effective_spi_hz = PL_CLOCK_HZ // (2 * self.half_period)

    def open(self) -> None:
        try:
            self.fd = os.open(self.uio_device, os.O_RDWR | os.O_SYNC | os.O_NONBLOCK)
            page_size = os.sysconf("SC_PAGE_SIZE")
            self.csr = mmap.mmap(
                self.fd,
                DMA_CSR_SIZE,
                flags=mmap.MAP_SHARED,
                prot=mmap.PROT_READ | mmap.PROT_WRITE,
                offset=0,
            )
            self.dma = mmap.mmap(
                self.fd,
                DMA_MAP_SIZE,
                flags=mmap.MAP_SHARED,
                prot=mmap.PROT_READ | mmap.PROT_WRITE,
                offset=page_size,
            )
        except (OSError, ValueError) as exc:
            self.close()
            raise BackendError(f"cannot map AbstractX UIO and DMA resources: {exc}") from exc

        if self.read32(DMA_HARDWARE_ID) != DMA_HARDWARE_MAGIC:
            self.close()
            raise BackendError("AbstractX hardware ID mismatch (expected ASP6)")
        half_period = max(
            2,
            (PL_CLOCK_HZ + 2 * self.requested_spi_hz - 1)
            // (2 * self.requested_spi_hz),
        )
        self.write32(IMU_REG_SPI_HALF_PERIOD, half_period)
        if self.read32(IMU_REG_SPI_HALF_PERIOD) != half_period:
            self.close()
            raise BackendError("PL SPI half-period readback mismatch")
        self.half_period = half_period
        self.effective_spi_hz = PL_CLOCK_HZ // (2 * half_period)
        self.spi_rate_source = "100MHz-fabric-divider"

    def read32(self, offset: int) -> int:
        if self.csr is None or offset < 0 or offset + 4 > DMA_CSR_SIZE or offset % 4:
            raise BackendError(f"invalid or unmapped CSR read at 0x{offset:X}")
        return struct.unpack_from("<I", self.csr, offset)[0]

    def write32(self, offset: int, value: int) -> None:
        if self.csr is None or offset < 0 or offset + 4 > DMA_CSR_SIZE or offset % 4:
            raise BackendError(f"invalid or unmapped CSR write at 0x{offset:X}")
        struct.pack_into("<I", self.csr, offset, value & 0xFFFFFFFF)

    def _direct_transaction(self, address: int, *, value: int | None) -> int:
        if self.csr is None:
            raise BackendError("AbstractX backend is not open")

        self.write32(IMU_REG_STATUS, 0x02)
        self.write32(IMU_REG_ADDR, address & 0x7F)
        self.write32(IMU_REG_LEN, 1)
        is_write = value is not None
        if is_write:
            self.write32(IMU_REG_WDATA, value & 0xFF)
        control = 0x06 | (0x08 if is_write else 0)
        self.write32(IMU_REG_CTRL, control)

        deadline = time.monotonic() + 0.100
        while time.monotonic() < deadline:
            status = self.read32(IMU_REG_STATUS)
            if status & 0x02 and not status & 0x01:
                return 0 if is_write else self.read32(IMU_REG_RDATA) & 0xFF
            time.sleep(0.00001)
        raise TimeoutError(f"PL direct SPI register 0x{address:02X} timed out")

    def read_register(self, address: int) -> int:
        return self._direct_transaction(address, value=None)

    def write_register(self, address: int, value: int) -> None:
        self._direct_transaction(address, value=value)

    def start_samples(self) -> None:
        if self.fd is None or self.csr is None or self.dma is None:
            raise BackendError("AbstractX backend is not open")

        self.write32(DMA_CONTROL, 0)
        self.write32(DMA_RX_BASE, DMA_PHYSICAL_BASE)
        self.write32(DMA_RX_CAPACITY, DMA_RING_CAPACITY)
        self.write32(DMA_RX_HEAD, self.read32(DMA_RX_TAIL))
        self.write32(DMA_IRQ_STATUS, 1)
        self.write32(DMA_IRQ_ENABLE, 1)
        self.write32(DMA_CONTROL, 1)
        self._unmask_irq()
        self.dma_started = True

        self.write32(IMU_REG_ADDR, IMU_TEMP_DATA1)
        self.write32(IMU_REG_LEN, IMU_BURST_LENGTH)
        self.write32(IMU_REG_CTRL, 0x05)
        deadline = time.monotonic() + 0.100
        while time.monotonic() < deadline:
            if self.read32(IMU_REG_STATUS) & 0x04:
                self.samples_started = True
                return
            time.sleep(0.00001)
        raise TimeoutError("PL Auto-DMA did not become active")

    def _unmask_irq(self) -> None:
        if self.fd is None:
            raise BackendError("AbstractX UIO descriptor is closed")
        written = os.write(self.fd, struct.pack("<I", 1))
        if written != 4:
            raise BackendError("failed to re-enable AbstractX UIO interrupt")
        self.irq_masked = False

    def _wait_irq(self, timeout_seconds: float) -> None:
        if self.fd is None:
            raise BackendError("AbstractX UIO descriptor is closed")
        poller = select.poll()
        poller.register(self.fd, select.POLLIN | select.POLLERR)
        ready = poller.poll(max(1, math_ceil_ms(timeout_seconds)))
        if not ready:
            raise TimeoutError("timed out waiting for AbstractX DMA interrupt")
        if ready[0][1] & select.POLLERR:
            raise BackendError("AbstractX UIO interrupt reported an error")
        try:
            data = os.read(self.fd, 4)
        except BlockingIOError as exc:
            raise BackendError("UIO interrupt was signaled without an event count") from exc
        if len(data) != 4:
            raise BackendError("short read from AbstractX UIO interrupt")
        self.irq_masked = True

    def _drain_ring(self) -> None:
        if self.dma is None:
            raise BackendError("AbstractX DMA ring is not mapped")
        head = self.read32(DMA_RX_HEAD)
        tail = self.read32(DMA_RX_TAIL)
        if head >= DMA_RING_CAPACITY or tail >= DMA_RING_CAPACITY:
            raise BackendError("AbstractX DMA ring index is outside configured capacity")
        if ring_is_full(head, tail, DMA_RING_CAPACITY):
            self.ring_full_observations += 1

        while ring_pending_count(head, tail, DMA_RING_CAPACITY) > 0:
            start = head * DMA_SLOT_SIZE
            memory_record = bytes(self.dma[start : start + DMA_SLOT_SIZE])
            self.pending.append(parse_dma_tlp(memory_record))
            head = (head + 1) % DMA_RING_CAPACITY
            self.write32(DMA_RX_HEAD, head)
            tail = self.read32(DMA_RX_TAIL)
            if tail >= DMA_RING_CAPACITY:
                raise BackendError("AbstractX DMA tail is outside configured capacity")

        if self.irq_masked:
            self.write32(DMA_IRQ_STATUS, 1)
            self._unmask_irq()

    def next_sample(self, timeout_seconds: float) -> SampleRecord:
        wait_start_ns = time.monotonic_ns()
        deadline = time.monotonic() + timeout_seconds
        while not self.pending:
            self._drain_ring()
            if self.pending:
                break
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("timed out waiting for an AbstractX IMU DMA sample")
            self._wait_irq(remaining)
            self._drain_ring()

        sequence, start_ns, transfer_ns, payload = self.pending.popleft()
        return SampleRecord(
            payload=payload,
            sequence=sequence,
            sequence_bits=16,
            event_timestamp_ns=start_ns,
            event_clock="ABSTRACTX_PL",
            host_wait_start_ns=wait_start_ns,
            host_complete_ns=time.monotonic_ns(),
            hardware_transfer_ns=transfer_ns,
        )

    def stop_samples(self) -> None:
        if self.csr is None:
            return
        self.write32(IMU_REG_CTRL, 0)
        deadline = time.monotonic() + 0.100
        while time.monotonic() < deadline:
            if (
                not self.read32(IMU_REG_STATUS) & 0x0C
                and not self.read32(DMA_STATUS) & 0x01
            ):
                break
            time.sleep(0.00001)
        else:
            raise TimeoutError("PL Auto-DMA or its DMA write did not stop")
        if self.dma_started:
            self._drain_ring()
        if self.dma_started:
            self.write32(DMA_IRQ_ENABLE, 0)
            self.write32(DMA_CONTROL, 0)
            self.dma_started = False
        self.samples_started = False

    def close(self) -> None:
        close_error: Exception | None = None
        try:
            if self.samples_started or self.dma_started:
                self.stop_samples()
        except (BackendError, OSError, TimeoutError) as exc:
            close_error = exc
        finally:
            for mapping_name in ("dma", "csr"):
                mapping = getattr(self, mapping_name)
                if mapping is not None:
                    try:
                        mapping.close()
                    except OSError as exc:
                        close_error = close_error or exc
                    setattr(self, mapping_name, None)
            if self.fd is not None:
                try:
                    os.close(self.fd)
                except OSError as exc:
                    close_error = close_error or exc
                self.fd = None
        if close_error is not None:
            raise BackendError(f"failed to close AbstractX resources: {close_error}") from close_error

    def read_overrun_count(self) -> int | None:
        if self.csr is None:
            raise BackendError("AbstractX backend is not open")
        return self.read32(IMU_REG_DRDY_OVERRUN_COUNT)

    def pending_sample_count(self) -> int:
        return len(self.pending)


def math_ceil_ms(seconds: float) -> int:
    return int(seconds * 1000.0 + 0.999)
