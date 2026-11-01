#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
sensor_contract.py - The e906_repl_app's own sensor format, shared by the MicroPython
scripts' host partners (board_studio_bridge.py, the Studio flight plugin).

AbstractX only moves bytes (REPL text, TLP, CTF); it does not define sensor formats. The
application does: raw_sensor_streamer.py prints "!RAW:M=<hex>:C=<chip>[:G=<hex>]" with the
compass registers exactly as read, and everything on the PC decodes them with this module.
Plain python3, no dependencies (also copied to /root on the board).
"""
# @impl [SPEC-E906-REPL-11] apps/e906_repl_app/SPECIFICATION.md

import math
import struct

# Compass bytes as the E906 reads them: no reordering on the target.
MAG_CHIPS = ("HMC5883L", "QMC5883L")
IMU_CHIPS = ("ICM42688P",)


def decode_mag(raw: bytes, chip: str = "QMC5883L"):
    """Signed (x, y, z) from 6 data-register bytes. HMC5883L: regs 0x03..0x08 = X, Z, Y
    big-endian. QMC5883L: regs 0x00..0x05 = X, Y, Z little-endian (also the untagged default)."""
    if len(raw) != 6:
        raise ValueError(f"compass sample is {len(raw)} bytes, expected 6")
    if chip == "HMC5883L":
        x, z, y = struct.unpack(">hhh", raw)
    elif chip == "QMC5883L":
        x, y, z = struct.unpack("<hhh", raw)
    else:
        raise ValueError(f"unknown compass chip {chip!r}")
    return x, y, z


# Tag-3 TLP payload: the compass sample as the E906 read it. One definition for the board
# bridge (encode) and Studio (decode), so the two cannot drift apart.
MAG_TAG = 3
MAG_CHIP_CODE = {"HMC5883L": 1, "QMC5883L": 2}
# LSB per gauss at the range the scripts configure (HMC CRB 0x20 = 1.3 Ga; QMC RNG 8 G)
MAG_LSB_PER_GAUSS = {"HMC5883L": 1090.0, "QMC5883L": 3000.0}


def encode_mag_payload(raw: bytes, chip: str) -> bytes:
    """6 raw register bytes, chip code, pad, decoded <hhh x y z> = 14 bytes."""
    x, y, z = decode_mag(raw, chip)
    return bytes(raw) + bytes([MAG_CHIP_CODE[chip], 0]) + struct.pack("<hhh", x, y, z)


def decode_mag_payload(payload: bytes) -> dict:
    """Inverse of encode_mag_payload; re-decodes the raw bytes and checks they agree."""
    if len(payload) < 14:
        raise ValueError(f"compass payload is {len(payload)} bytes, expected >= 14")
    chips = {v: k for k, v in MAG_CHIP_CODE.items()}
    chip = chips.get(payload[6])
    if chip is None:
        raise ValueError(f"unknown compass chip code {payload[6]}")
    raw = bytes(payload[:6])
    xyz = struct.unpack("<hhh", payload[8:14])
    if decode_mag(raw, chip) != xyz:
        raise ValueError("compass payload: decoded X/Y/Z do not match the raw bytes")
    field_ut = (sum(v * v for v in xyz) ** 0.5) / MAG_LSB_PER_GAUSS[chip] * 100.0
    return {"raw": raw, "chip": chip, "xyz": xyz, "field_ut": field_ut}


# Tag-1 TLP payload: ICM-42688-P 14-byte register burst (regs 0x1D..0x2A: TEMP, ACCEL XYZ, GYRO XYZ
# big-endian). Shared between board_studio_bridge.py and Studio.
IMU_TAG = 1
IMU_ADDR = 0x40001000
IMU_CHIP_CODE = {"ICM42688P": 1}
IMU_ACCEL_LSB_PER_G = 2048.0   # +/-16g full-scale
IMU_GYRO_LSB_PER_DPS = 16.4    # +/-2000 dps full-scale
IMU_TEMP_SENSITIVITY = 132.48
IMU_TEMP_OFFSET_C = 25.0


def decode_imu(raw: bytes, chip: str = "ICM42688P") -> dict:
    """Decodes 14-byte ICM-42688-P burst (0x1D..0x2A: Temp, Accel X/Y/Z, Gyro X/Y/Z big-endian)."""
    if len(raw) != 14:
        raise ValueError(f"IMU sample is {len(raw)} bytes, expected 14")
    if chip not in IMU_CHIP_CODE:
        raise ValueError(f"unknown IMU chip {chip!r}")
    temp_raw, ax, ay, az, gx, gy, gz = struct.unpack(">hhhhhhh", raw)
    temp_c = (temp_raw / IMU_TEMP_SENSITIVITY) + IMU_TEMP_OFFSET_C
    ax_g = ax / IMU_ACCEL_LSB_PER_G
    ay_g = ay / IMU_ACCEL_LSB_PER_G
    az_g = az / IMU_ACCEL_LSB_PER_G
    gx_dps = gx / IMU_GYRO_LSB_PER_DPS
    gy_dps = gy / IMU_GYRO_LSB_PER_DPS
    gz_dps = gz / IMU_GYRO_LSB_PER_DPS
    if abs(ax_g) > 1e-4 or abs(ay_g) > 1e-4 or abs(az_g) > 1e-4:
        roll_deg = math.degrees(math.atan2(ay_g, az_g if az_g != 0.0 else 1e-6))
        pitch_deg = math.degrees(math.atan2(-ax_g, math.sqrt(ay_g * ay_g + az_g * az_g)))
    else:
        roll_deg = 0.0
        pitch_deg = 0.0
    return {
        "raw": bytes(raw),
        "chip": chip,
        "temp_raw": temp_raw,
        "accel_raw": (ax, ay, az),
        "gyro_raw": (gx, gy, gz),
        "temp_c": temp_c,
        "accel_g": (ax_g, ay_g, az_g),
        "gyro_dps": (gx_dps, gy_dps, gz_dps),
        "roll_deg": roll_deg,
        "pitch_deg": pitch_deg,
    }


def encode_imu_payload(raw: bytes, chip: str = "ICM42688P") -> bytes:
    """14 raw register bytes, chip code, pad, decoded <hhhhhhh temp ax ay az gx gy gz> = 30 bytes."""
    d = decode_imu(raw, chip)
    ax, ay, az = d["accel_raw"]
    gx, gy, gz = d["gyro_raw"]
    return (
        bytes(raw)
        + bytes([IMU_CHIP_CODE[chip], 0])
        + struct.pack("<hhhhhhh", d["temp_raw"], ax, ay, az, gx, gy, gz)
    )


def decode_imu_payload(payload: bytes) -> dict:
    """Inverse of encode_imu_payload; re-decodes the raw bytes and verifies agreement."""
    if len(payload) < 30:
        raise ValueError(f"IMU payload is {len(payload)} bytes, expected >= 30")
    chips = {v: k for k, v in IMU_CHIP_CODE.items()}
    chip = chips.get(payload[14])
    if chip is None:
        raise ValueError(f"unknown IMU chip code {payload[14]}")
    raw = bytes(payload[:14])
    unpacked = struct.unpack("<hhhhhhh", payload[16:30])
    d = decode_imu(raw, chip)
    expected = (d["temp_raw"], *d["accel_raw"], *d["gyro_raw"])
    if unpacked != expected:
        raise ValueError("IMU payload: decoded values do not match the raw bytes")
    return d


# GPS: NMEA sentences arrive as raw UART bytes. A sentence counts only if its *hh checksum
# matches; garbled sentences (baud change, overrun) once put a value too large for int32
# into the bridge's GPS packet and killed the live stream.
def nmea_sentence_ok(sentence: str) -> bool:
    """True for '$...*hh' whose XOR checksum over the text between '$' and '*' matches."""
    if not sentence.startswith("$") or "*" not in sentence:
        return False
    body, _, given = sentence[1:].partition("*")
    if len(given) < 2:
        return False
    calc = 0
    for ch in body:
        calc ^= ord(ch)
    try:
        return calc == int(given[:2], 16)
    except ValueError:
        return False


def clamp_i32(v: int) -> int:
    return max(-2147483648, min(2147483647, int(v)))
