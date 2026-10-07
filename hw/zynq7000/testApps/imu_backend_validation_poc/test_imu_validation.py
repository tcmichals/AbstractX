#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

"""Hardware-free regression tests for the common Zynq IMU validation suite."""
# @impl [SPEC-ZYNQ-IMU-POC-07] hw/zynq7000/testApps/imu_backend_validation_poc/test_imu_validation.py

from __future__ import annotations

import unittest

from backends import (
    DMA_RING_CAPACITY,
    DMA_SLOT_SIZE,
    normalize_dma_beat_order,
    parse_dma_tlp,
    ring_is_full,
    ring_pending_count,
)
from imu_validation import (
    BackendError,
    SampleRecord,
    TestOptions,
    ValidationError,
    compare_results,
    parse_imu_sample,
    percentile,
    run_suite,
    summarize,
    _count_sequence_gap,
)


def make_payload() -> bytes:
    values = (0, 2048, -2048, 0, 164, -164, 0)
    return b"".join(value.to_bytes(2, "big", signed=True) for value in values)


class FakeClock:
    def __init__(self) -> None:
        self.now_ns = 1_000_000_000

    def __call__(self) -> int:
        return self.now_ns


class FakeBackend:
    effective_spi_hz = 10_000_000

    def __init__(self, name: str, clock: FakeClock, whoami: int = 0x47) -> None:
        self.name = name
        self.clock = clock
        self.whoami = whoami
        self.sequence = 0
        self.register_writes: list[tuple[int, int]] = []
        self.closed = False
        self.started = False

    def open(self) -> None:
        return

    def read_register(self, address: int) -> int:
        if address != 0x75:
            raise BackendError(f"unexpected register read: 0x{address:02X}")
        return self.whoami

    def write_register(self, address: int, value: int) -> None:
        self.register_writes.append((address, value))

    def start_samples(self) -> None:
        self.started = True

    def next_sample(self, timeout_seconds: float) -> SampleRecord:
        del timeout_seconds
        self.clock.now_ns += 1_000_000
        self.sequence += 1
        return SampleRecord(
            payload=make_payload(),
            sequence=self.sequence,
            sequence_bits=16,
            event_timestamp_ns=self.clock.now_ns - 100_000,
            event_clock="CLOCK_MONOTONIC",
            host_wait_start_ns=self.clock.now_ns - 100_000,
            host_complete_ns=self.clock.now_ns,
            hardware_transfer_ns=50_000,
        )

    def stop_samples(self) -> None:
        self.started = False

    def read_overrun_count(self) -> int | None:
        return None

    def pending_sample_count(self) -> int:
        return 0

    def close(self) -> None:
        self.closed = True


class ImuValidationTests(unittest.TestCase):
    def test_sample_parser_decodes_signed_big_endian_values(self) -> None:
        sample = parse_imu_sample(make_payload())
        self.assertAlmostEqual(sample["temperature_c"], 25.0)
        self.assertEqual(sample["accel_g"], (1.0, -1.0, 0.0))
        self.assertEqual(sample["gyro_dps"], (10.0, -10.0, 0.0))

    def test_sample_parser_rejects_wrong_payload_length(self) -> None:
        with self.assertRaises(ValidationError):
            parse_imu_sample(b"\x00" * 13)

    def test_both_backend_selections_use_the_same_suite(self) -> None:
        results = []
        for backend_name in ("linux-userspace", "abstractx-auto-dma"):
            clock = FakeClock()
            backend = FakeBackend(backend_name, clock)
            result = run_suite(
                backend,
                TestOptions(
                    duration_seconds=0.003,
                    warmup_samples=1,
                    odr_hz=8000,
                    requested_spi_hz=10_000_000,
                    stabilization_seconds=0,
                ),
                clock_ns=clock,
                cpu_clock_ns=clock,
                sleep=lambda _seconds: None,
            )
            results.append(result)
            self.assertEqual(result["status"], "passed")
            self.assertTrue(all(value == "passed" for value in result["cases"].values()))
            self.assertEqual(result["metrics"]["received_samples"], 3)
            self.assertEqual(result["metrics"]["pending_samples_at_shutdown"], 0)
            self.assertTrue(backend.closed)
            self.assertEqual(
                backend.register_writes,
                [(0x4E, 0x0F), (0x4F, 0x03), (0x50, 0x03), (0x14, 0x12), (0x65, 0x08)],
            )
        self.assertEqual(
            results[0]["metrics"]["sample_sequences"],
            results[1]["metrics"]["sample_sequences"],
        )

    def test_wrong_sensor_identity_fails_and_closes_backend(self) -> None:
        clock = FakeClock()
        backend = FakeBackend("fake", clock, whoami=0x00)
        result = run_suite(
            backend,
            TestOptions(duration_seconds=0.001, warmup_samples=0, stabilization_seconds=0),
            clock_ns=clock,
            cpu_clock_ns=clock,
            sleep=lambda _seconds: None,
        )
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["cases"]["identity"], "failed")
        self.assertTrue(backend.closed)
        self.assertIn("WHO_AM_I mismatch", result["error"])

    def test_dma_memory_beat_normalization_and_tlp_parser(self) -> None:
        frame = bytearray(DMA_SLOT_SIZE)
        frame[0:4] = bytes((0x10, 0x00, 0x00, 0x02))
        frame[4:8] = (0x40000100).to_bytes(4, "big")
        frame[8:10] = (4).to_bytes(2, "big")
        frame[10:12] = (42).to_bytes(2, "big")
        frame[12:20] = (1_000_000).to_bytes(8, "big")
        frame[20:34] = make_payload()
        frame[36:44] = (1_050_000).to_bytes(8, "big")
        frame[60:64] = (0xDEADBEEF).to_bytes(4, "big")
        memory = b"".join(frame[index : index + 8][::-1] for index in range(0, 64, 8))
        self.assertEqual(normalize_dma_beat_order(memory), bytes(frame))
        sequence, start_ns, duration_ns, payload = parse_dma_tlp(memory)
        self.assertEqual((sequence, start_ns, duration_ns), (42, 1_000_000, 50_000))
        self.assertEqual(payload, make_payload())

    def test_statistics_use_nearest_rank_percentiles(self) -> None:
        self.assertEqual(percentile([1, 2, 3, 4, 5], 95), 5)
        summary = summarize([10, 20, 30, 40])
        self.assertEqual(summary["median"], 20)
        self.assertEqual(summary["p95"], 40)
        self.assertIsNone(percentile([], 95))

    def test_sequence_gap_accounting_handles_duplicates_and_wraparound(self) -> None:
        self.assertEqual(_count_sequence_gap(10, 13, 16), (2, 0))
        self.assertEqual(_count_sequence_gap(13, 13, 16), (0, 1))
        self.assertEqual(_count_sequence_gap(0xFFFF, 0, 16), (0, 0))

    def test_cleanup_runs_after_sample_timeout(self) -> None:
        class TimeoutBackend(FakeBackend):
            def next_sample(self, timeout_seconds: float) -> SampleRecord:
                raise TimeoutError("test timeout")

        clock = FakeClock()
        backend = TimeoutBackend("fake", clock)
        result = run_suite(
            backend,
            TestOptions(duration_seconds=0.001, warmup_samples=0, stabilization_seconds=0),
            clock_ns=clock,
            cpu_clock_ns=clock,
            sleep=lambda _seconds: None,
        )
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["cases"]["single_sample"], "failed")
        self.assertEqual(result["cases"]["shutdown"], "passed")
        self.assertTrue(backend.closed)

    def test_invalid_sample_is_counted_and_fails_the_run(self) -> None:
        class InvalidBackend(FakeBackend):
            def next_sample(self, timeout_seconds: float) -> SampleRecord:
                record = super().next_sample(timeout_seconds)
                return SampleRecord(
                    payload=record.payload[:-1],
                    sequence=record.sequence,
                    sequence_bits=record.sequence_bits,
                    event_timestamp_ns=record.event_timestamp_ns,
                    event_clock=record.event_clock,
                    host_wait_start_ns=record.host_wait_start_ns,
                    host_complete_ns=record.host_complete_ns,
                )

        clock = FakeClock()
        result = run_suite(
            InvalidBackend("fake", clock),
            TestOptions(duration_seconds=0.001, warmup_samples=0, stabilization_seconds=0),
            clock_ns=clock,
            cpu_clock_ns=clock,
            sleep=lambda _seconds: None,
        )
        self.assertEqual(result["status"], "failed")
        self.assertEqual(result["metrics"]["invalid_samples"], 1)

    def test_malformed_dma_tlp_is_rejected(self) -> None:
        with self.assertRaises(BackendError):
            parse_dma_tlp(bytes(DMA_SLOT_SIZE))

    def test_dma_ring_empty_wraparound_and_full_states(self) -> None:
        self.assertEqual(ring_pending_count(7, 7, 8), 0)
        self.assertEqual(ring_pending_count(6, 1, 8), 3)
        self.assertFalse(ring_is_full(6, 1, 8))
        self.assertTrue(ring_is_full(1, 0, 8))
        self.assertEqual(
            ring_pending_count(1, 0, DMA_RING_CAPACITY),
            DMA_RING_CAPACITY - 1,
        )

    def test_comparison_rejects_different_workloads_and_warns_on_unknown_clock(self) -> None:
        base = {
            "schema_version": 1,
            "status": "passed",
            "backend": "one",
            "sensor_id": 0x47,
            "configuration": [],
            "effective_spi_hz": None,
            "test_config": {
                "duration_seconds": 10,
                "warmup_samples": 100,
                "odr_hz": 8000,
                "requested_spi_hz": 10_000_000,
                "payload_length": 14,
                "spi_mode": 0,
            },
            "metrics": {"samples_per_second": 1000},
        }
        other = {**base, "backend": "two"}
        comparison = compare_results(base, other)
        self.assertFalse(comparison["effective_spi_rates_matched"])
        self.assertTrue(comparison["warnings"])

        incompatible = {
            **other,
            "test_config": {**other["test_config"], "odr_hz": 1000},
        }
        with self.assertRaises(ValidationError):
            compare_results(base, incompatible)
        malformed = {**base, "metrics": None}
        with self.assertRaises(ValidationError):
            compare_results(malformed, other)


if __name__ == "__main__":
    unittest.main()
