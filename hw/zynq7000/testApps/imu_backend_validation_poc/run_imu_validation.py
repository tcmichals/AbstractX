#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later

"""Run or compare the shared Zynq ICM-42688-P backend validation suite."""
# @impl [SPEC-ZYNQ-IMU-POC-01] hw/zynq7000/testApps/imu_backend_validation_poc/run_imu_validation.py

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

from backends import AbstractXDmaBackend, LinuxUserspaceBackend
from imu_validation import BackendError, TestOptions, ValidationError, compare_results, run_suite


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--backend",
        choices=("linux-userspace", "abstractx-auto-dma"),
        help="hardware acquisition path to test",
    )
    parser.add_argument(
        "--compare",
        nargs=2,
        metavar=("FIRST_JSON", "SECOND_JSON"),
        help="compare two successful result files",
    )
    parser.add_argument("--duration-seconds", type=float, default=10.0)
    parser.add_argument("--warmup", type=int, default=100)
    parser.add_argument("--odr-hz", type=int, default=8000)
    parser.add_argument("--speed-hz", type=int, default=10_000_000)
    parser.add_argument("--spi-device", default="/dev/spidev1.0")
    parser.add_argument("--gpiochip", default="/dev/gpiochip0")
    parser.add_argument("--gpio-line", type=int)
    parser.add_argument("--uio", default="/dev/uio0")
    parser.add_argument("--board", default="qmtech-zynq7020")
    parser.add_argument("--bitstream-id", default="unspecified")
    parser.add_argument("--software-id", default="working-tree")
    parser.add_argument("--json", dest="json_path", default="imu-validation.json")
    return parser


def load_result(path: str) -> dict:
    try:
        with open(path, "r", encoding="utf-8") as result_file:
            value = json.load(result_file)
    except (OSError, json.JSONDecodeError) as exc:
        raise ValidationError(f"cannot read result file {path}: {exc}") from exc
    if not isinstance(value, dict):
        raise ValidationError(f"result file {path} must contain a JSON object")
    return value


def write_result(path: str, result: dict) -> None:
    try:
        output_path = Path(path)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with output_path.open("w", encoding="utf-8") as result_file:
            json.dump(result, result_file, indent=2, sort_keys=True)
            result_file.write("\n")
    except OSError as exc:
        raise BackendError(f"cannot write result file {path}: {exc}") from exc


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.compare:
        try:
            comparison = compare_results(
                load_result(args.compare[0]),
                load_result(args.compare[1]),
            )
        except ValidationError as exc:
            print(f"Comparison failed: {exc}", file=sys.stderr)
            return 1
        print(json.dumps(comparison, indent=2, sort_keys=True))
        return 0

    if args.backend is None:
        parser.error("select --backend or provide --compare")
    if not math.isfinite(args.duration_seconds) or args.duration_seconds <= 0:
        parser.error("--duration-seconds must be greater than zero")
    if args.warmup < 0:
        parser.error("--warmup cannot be negative")
    if args.odr_hz != 8000:
        parser.error("this POC's shared sensor configuration is fixed at --odr-hz 8000")
    if not 100_000 <= args.speed_hz <= 24_000_000:
        parser.error("--speed-hz must be between 100 kHz and 24 MHz")
    if args.backend == "linux-userspace" and args.gpio_line is None:
        parser.error("--gpio-line is required for --backend linux-userspace")
    if args.gpio_line is not None and args.gpio_line < 0:
        parser.error("--gpio-line must be a non-negative line offset")

    options = TestOptions(
        duration_seconds=args.duration_seconds,
        warmup_samples=args.warmup,
        odr_hz=args.odr_hz,
        requested_spi_hz=args.speed_hz,
    )
    if args.backend == "linux-userspace":
        backend = LinuxUserspaceBackend(
            args.spi_device,
            args.gpiochip,
            args.gpio_line,
            args.speed_hz,
        )
    else:
        backend = AbstractXDmaBackend(args.uio, args.speed_hz)

    try:
        result = run_suite(backend, options)
        result["hardware"] = {
            "board": args.board,
            "bitstream_id": args.bitstream_id,
            "software_id": args.software_id,
        }
        write_result(args.json_path, result)
    except (BackendError, ValidationError, OSError, ValueError) as exc:
        print(f"IMU validation failed: {exc}", file=sys.stderr)
        return 1

    metrics = result["metrics"]
    print(
        f"{result['backend']}: {result['status']}; "
        f"{metrics['received_samples']} samples in "
        f"{metrics['elapsed_ns'] / 1e9:.3f}s; "
        f"{metrics['samples_per_second']:.1f} samples/s; "
        f"{metrics['sequence_gaps']} sequence gaps"
    )
    print(f"Result saved to {args.json_path}")
    if result["status"] != "passed":
        print(result.get("error") or "one or more test cases failed", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
