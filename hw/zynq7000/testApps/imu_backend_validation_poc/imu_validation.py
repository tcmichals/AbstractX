#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

"""Shared ICM-42688-P validation suite and backend-neutral result handling."""
# @impl [SPEC-ZYNQ-IMU-POC-01] hw/zynq7000/testApps/imu_backend_validation_poc/imu_validation.py
# @impl [SPEC-ZYNQ-IMU-POC-04] hw/zynq7000/testApps/imu_backend_validation_poc/imu_validation.py

from __future__ import annotations

from dataclasses import dataclass
import math
import resource
import time
from typing import Protocol


class ValidationError(RuntimeError):
    """A sensor or sample violated the shared validation contract."""


class BackendError(RuntimeError):
    """A selected hardware backend could not complete an operation."""


@dataclass(frozen=True)
class SampleRecord:
    payload: bytes
    sequence: int
    sequence_bits: int
    event_timestamp_ns: int | None
    event_clock: str
    host_wait_start_ns: int
    host_complete_ns: int
    hardware_transfer_ns: int | None = None


class ImuBackend(Protocol):
    name: str
    effective_spi_hz: int | None

    def open(self) -> None: ...

    def read_register(self, address: int) -> int: ...

    def write_register(self, address: int, value: int) -> None: ...

    def start_samples(self) -> None: ...

    def next_sample(self, timeout_seconds: float) -> SampleRecord: ...

    def stop_samples(self) -> None: ...

    def read_overrun_count(self) -> int | None: ...

    def close(self) -> None: ...


@dataclass(frozen=True)
class TestOptions:
    duration_seconds: float = 10.0
    warmup_samples: int = 100
    odr_hz: int = 8000
    requested_spi_hz: int = 10_000_000
    stabilization_seconds: float = 0.010


SENSOR_CONFIGURATION = (
    (0x4E, 0x0F),
    (0x4F, 0x03),
    (0x50, 0x03),
    (0x14, 0x12),
    (0x65, 0x08),
)


def parse_imu_sample(payload: bytes) -> dict[str, float | tuple[float, float, float]]:
    if len(payload) != 14:
        raise ValidationError(f"expected 14 sensor bytes, got {len(payload)}")

    def signed_be16(offset: int) -> int:
        return int.from_bytes(payload[offset : offset + 2], "big", signed=True)

    temperature = signed_be16(0) / 132.48 + 25.0
    accel = tuple(signed_be16(i) / 2048.0 for i in (2, 4, 6))
    gyro = tuple(signed_be16(i) / 16.4 for i in (8, 10, 12))
    return {"temperature_c": temperature, "accel_g": accel, "gyro_dps": gyro}


def percentile(values: list[int], percent: float) -> int | None:
    if not values:
        return None
    if not 0.0 <= percent <= 100.0:
        raise ValueError("percentile must be in [0, 100]")
    ordered = sorted(values)
    rank = max(1, math.ceil((percent / 100.0) * len(ordered)))
    return ordered[rank - 1]


def summarize(values: list[int]) -> dict[str, int | float | None]:
    if not values:
        return {
            "count": 0,
            "min": None,
            "mean": None,
            "median": None,
            "p95": None,
            "p99": None,
            "max": None,
        }
    return {
        "count": len(values),
        "min": min(values),
        "mean": sum(values) / len(values),
        "median": percentile(values, 50.0),
        "p95": percentile(values, 95.0),
        "p99": percentile(values, 99.0),
        "max": max(values),
    }


def _count_sequence_gap(
    previous: int | None, current: int, bits: int
) -> tuple[int, int]:
    if previous is None:
        return 0, 0
    mask = (1 << bits) - 1
    delta = (current - previous) & mask
    if delta == 0:
        return 0, 1
    if delta >= (1 << (bits - 1)):
        return 0, 0
    return max(delta - 1, 0), 0


def run_suite(
    backend: ImuBackend,
    options: TestOptions,
    *,
    clock_ns=time.monotonic_ns,
    cpu_clock_ns=time.process_time_ns,
    sleep=time.sleep,
) -> dict:
    if not math.isfinite(options.duration_seconds) or options.duration_seconds <= 0:
        raise ValueError("duration_seconds must be greater than zero")
    if options.warmup_samples < 0:
        raise ValueError("warmup_samples cannot be negative")
    if options.odr_hz <= 0:
        raise ValueError("odr_hz must be greater than zero")
    if options.odr_hz != 8000:
        raise ValueError("the current shared sensor configuration fixes ODR at 8000 Hz")
    if not 100_000 <= options.requested_spi_hz <= 24_000_000:
        raise ValueError("requested SPI speed must be between 100 kHz and 24 MHz")

    cases = {
        "preflight": "pending",
        "identity": "pending",
        "configuration": "pending",
        "single_sample": "pending",
        "repeated_run": "pending",
        "shutdown": "pending",
    }
    raw_host_wait_ns: list[int] = []
    raw_linux_event_to_data_ns: list[int] = []
    raw_pl_transfer_ns: list[int] = []
    sequences: list[int] = []
    raw_payloads: list[str] = []
    duplicate_sequences = 0
    sequence_gaps = 0
    status = "failed"
    error: str | None = None
    current_case = "preflight"
    sensor_id: int | None = None
    received = 0
    invalid_samples = 0
    expected_events = round(options.duration_seconds * options.odr_hz)
    run_start_ns: int | None = None
    run_end_ns: int | None = None
    overrun_start: int | None = None
    overrun_end: int | None = None
    pending_samples_at_shutdown = 0
    cpu_start = resource.getrusage(resource.RUSAGE_SELF)
    cpu_wall_start_ns = cpu_clock_ns()
    cpu_end = cpu_start
    cpu_wall_end_ns = cpu_wall_start_ns

    try:
        backend.open()
        cases["preflight"] = "passed"

        current_case = "identity"
        sensor_id = backend.read_register(0x75)
        if sensor_id != 0x47:
            raise ValidationError(
                f"ICM-42688-P WHO_AM_I mismatch: expected 0x47, got 0x{sensor_id:02X}"
            )
        cases["identity"] = "passed"

        current_case = "configuration"
        for address, value in SENSOR_CONFIGURATION:
            backend.write_register(address, value)
        cases["configuration"] = "passed"
        sleep(options.stabilization_seconds)

        current_case = "single_sample"
        overrun_start = backend.read_overrun_count()
        backend.start_samples()
        smoke = backend.next_sample(timeout_seconds=2.0)
        try:
            parse_imu_sample(smoke.payload)
        except ValidationError:
            invalid_samples += 1
            raise
        cases["single_sample"] = "passed"

        previous_sequence = smoke.sequence
        for _ in range(options.warmup_samples):
            warmup = backend.next_sample(timeout_seconds=2.0)
            try:
                parse_imu_sample(warmup.payload)
            except ValidationError:
                invalid_samples += 1
                raise
            previous_sequence = warmup.sequence

        current_case = "repeated_run"
        run_start_ns = clock_ns()
        deadline_ns = run_start_ns + int(options.duration_seconds * 1_000_000_000)
        cpu_start = resource.getrusage(resource.RUSAGE_SELF)
        cpu_wall_start_ns = cpu_clock_ns()

        while clock_ns() < deadline_ns:
            remaining_s = (deadline_ns - clock_ns()) / 1_000_000_000
            try:
                record = backend.next_sample(
                    timeout_seconds=min(1.0, max(remaining_s, 0.001))
                )
            except TimeoutError:
                if clock_ns() >= deadline_ns:
                    break
                raise
            try:
                parse_imu_sample(record.payload)
            except ValidationError:
                invalid_samples += 1
                raise

            missing, duplicate = _count_sequence_gap(
                previous_sequence, record.sequence, record.sequence_bits
            )
            sequence_gaps += missing
            duplicate_sequences += duplicate
            previous_sequence = record.sequence

            raw_host_wait_ns.append(
                max(record.host_complete_ns - record.host_wait_start_ns, 0)
            )
            if record.event_timestamp_ns is not None:
                if record.event_clock == "CLOCK_MONOTONIC":
                    raw_linux_event_to_data_ns.append(
                        record.host_complete_ns - record.event_timestamp_ns
                    )
            if record.hardware_transfer_ns is not None:
                raw_pl_transfer_ns.append(record.hardware_transfer_ns)
            sequences.append(record.sequence)
            raw_payloads.append(record.payload.hex())
            received += 1

        run_end_ns = clock_ns()
        cpu_end = resource.getrusage(resource.RUSAGE_SELF)
        cpu_wall_end_ns = cpu_clock_ns()
        if received == 0:
            raise ValidationError("no samples were received during the measurement window")
        cases["repeated_run"] = "passed"
        status = "passed"
    except (BackendError, ValidationError, TimeoutError, OSError, ValueError) as exc:
        error = str(exc)
        cases[current_case] = "failed"
    finally:
        try:
            backend.stop_samples()
            cases["shutdown"] = "passed"
            pending_samples_at_shutdown = backend.pending_sample_count()
        except (BackendError, OSError, TimeoutError) as exc:
            error = f"{error}; shutdown failed: {exc}" if error else f"shutdown failed: {exc}"
            status = "failed"
            cases["shutdown"] = "failed"
        if overrun_start is not None:
            try:
                overrun_end = backend.read_overrun_count()
            except (BackendError, OSError) as exc:
                error = f"{error}; overrun read failed: {exc}" if error else f"overrun read failed: {exc}"
                status = "failed"
                cases["shutdown"] = "failed"
        try:
            backend.close()
        except (BackendError, OSError, TimeoutError) as exc:
            error = f"{error}; close failed: {exc}" if error else f"close failed: {exc}"
            status = "failed"
            cases["shutdown"] = "failed"

    elapsed_ns = (
        max(run_end_ns - run_start_ns, 0)
        if run_start_ns is not None and run_end_ns is not None
        else 0
    )
    elapsed_seconds = elapsed_ns / 1_000_000_000

    return {
        "schema_version": 1,
        "status": status,
        "backend": backend.name,
        "sensor_id": sensor_id,
        "configuration": [{"address": a, "value": v} for a, v in SENSOR_CONFIGURATION],
        "test_config": {
            "duration_seconds": options.duration_seconds,
            "warmup_samples": options.warmup_samples,
            "odr_hz": options.odr_hz,
            "requested_spi_hz": options.requested_spi_hz,
            "payload_length": 14,
            "spi_mode": 0,
        },
        "effective_spi_hz": backend.effective_spi_hz,
        "effective_spi_rate_source": getattr(backend, "spi_rate_source", "unknown"),
        "cases": cases,
        "error": error,
        "metrics": {
            "expected_events": expected_events,
            "received_samples": received,
            "invalid_samples": invalid_samples,
            "sequence_gaps": sequence_gaps,
            "duplicate_sequences": duplicate_sequences,
            "pl_drdy_busy_edge_overruns": (
                (overrun_end - overrun_start) & 0xFFFFFFFF
                if overrun_start is not None and overrun_end is not None
                else None
            ),
            "dma_ring_full_observations": getattr(
                backend, "ring_full_observations", None
            ),
            "pending_samples_at_shutdown": pending_samples_at_shutdown,
            "estimated_shortfall_vs_odr": max(expected_events - received, 0),
            "elapsed_ns": elapsed_ns,
            "samples_per_second": received / elapsed_seconds if elapsed_seconds else 0.0,
            "host_wait_to_sample_ns": summarize(raw_host_wait_ns),
            "linux_event_to_data_ns": summarize(raw_linux_event_to_data_ns),
            "pl_drdy_to_spi_complete_ns": summarize(raw_pl_transfer_ns),
            "process_cpu_ns": max(cpu_wall_end_ns - cpu_wall_start_ns, 0),
            "process_user_cpu_ns": round((cpu_end.ru_utime - cpu_start.ru_utime) * 1e9),
            "process_system_cpu_ns": round((cpu_end.ru_stime - cpu_start.ru_stime) * 1e9),
            "sample_sequences": sequences,
            "raw_payloads_hex": raw_payloads,
        },
        "timestamps": {
            "measurement_start_monotonic_ns": run_start_ns,
            "measurement_end_monotonic_ns": run_end_ns,
        },
    }


def compare_results(first: dict, second: dict) -> dict:
    def validate_result(result: dict, label: str) -> None:
        if type(result.get("schema_version")) is not int or result["schema_version"] != 1:
            raise ValidationError(f"{label} has an unsupported result schema version")
        if result.get("status") != "passed":
            raise ValidationError("comparison requires two successful hardware runs")
        if not isinstance(result.get("backend"), str) or not result["backend"]:
            raise ValidationError(f"{label} is missing its backend name")
        if type(result.get("sensor_id")) is not int:
            raise ValidationError(f"{label} is missing its integer sensor ID")
        configuration = result.get("configuration")
        if not isinstance(configuration, list) or any(
            not isinstance(item, dict)
            or type(item.get("address")) is not int
            or type(item.get("value")) is not int
            or not 0 <= item["address"] <= 0x7F
            or not 0 <= item["value"] <= 0xFF
            for item in configuration
        ):
            raise ValidationError(f"{label} has a malformed sensor configuration")
        test_config = result.get("test_config")
        required_settings = {
            "duration_seconds",
            "warmup_samples",
            "odr_hz",
            "requested_spi_hz",
            "payload_length",
            "spi_mode",
        }
        if not isinstance(test_config, dict) or not required_settings.issubset(test_config):
            raise ValidationError(f"{label} is missing required test settings")
        duration = test_config["duration_seconds"]
        if (
            not isinstance(duration, (int, float))
            or not math.isfinite(duration)
            or duration <= 0
        ):
            raise ValidationError(f"{label} has an invalid duration")
        for setting in (
            "warmup_samples",
            "odr_hz",
            "requested_spi_hz",
            "payload_length",
            "spi_mode",
        ):
            if type(test_config[setting]) is not int or test_config[setting] < 0:
                raise ValidationError(f"{label} has an invalid {setting}")
        if (
            test_config["odr_hz"] != 8000
            or not 100_000 <= test_config["requested_spi_hz"] <= 24_000_000
            or test_config["payload_length"] != 14
            or test_config["spi_mode"] != 0
        ):
            raise ValidationError(f"{label} uses an unsupported test configuration")
        metrics = result.get("metrics")
        if not isinstance(metrics, dict):
            raise ValidationError(f"{label} is missing metrics")
        rate = metrics.get("samples_per_second")
        if not isinstance(rate, (int, float)) or not math.isfinite(rate) or rate < 0:
            raise ValidationError(f"{label} has an invalid sample rate")
        effective_hz = result.get("effective_spi_hz")
        if effective_hz is not None and (
            type(effective_hz) is not int or effective_hz <= 0
        ):
            raise ValidationError(f"{label} has an invalid effective SPI rate")

    validate_result(first, "first result")
    validate_result(second, "second result")

    comparable_fields = ("sensor_id", "configuration")
    differences = [
        name
        for name in comparable_fields
        if first[name] != second[name]
    ]
    differences.extend(
        name
        for name in (
            "duration_seconds",
            "warmup_samples",
            "odr_hz",
            "requested_spi_hz",
            "payload_length",
            "spi_mode",
        )
        if first["test_config"][name] != second["test_config"][name]
    )
    if differences:
        raise ValidationError(
            "incompatible result configurations: " + ", ".join(differences)
        )

    first_hz = first.get("effective_spi_hz")
    second_hz = second.get("effective_spi_hz")
    warnings: list[str] = []
    clock_matched = False
    if first_hz is None or second_hz is None:
        warnings.append("effective SPI rate is unknown for at least one backend")
    else:
        relative_delta = abs(first_hz - second_hz) / max(first_hz, second_hz)
        clock_matched = relative_delta <= 0.05
        if not clock_matched:
            warnings.append("effective SPI rates differ by more than 5%")

    first_rate = first["metrics"]["samples_per_second"]
    second_rate = second["metrics"]["samples_per_second"]
    return {
        "first_backend": first["backend"],
        "second_backend": second["backend"],
        "first_samples_per_second": first_rate,
        "second_samples_per_second": second_rate,
        "second_over_first_throughput_ratio": (
            second_rate / first_rate if first_rate else None
        ),
        "effective_spi_rates_matched": clock_matched,
        "warnings": warnings,
    }
