#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
DShot reference (host side, [SPEC-PIO-12]): frame encoding, the ESC's GCR eRPM reply, and
decoding the raw line samples captured by rtl/pio/programs/dshot_bidir.pio. The FPGA holds no
protocol knowledge; this file is the reference the tests and host drivers follow.

Decoding follows Betaflight's decodeTelemetryPacket (21 line bits, a 1 at every transition,
GCR 5b/4b table, checksum of the 16 decoded bits xor-folded to 0xF).
"""
# @impl [SPEC-PIO-12] rtl/pio/SPECIFICATION.md

GCR_ENCODE = [0x19, 0x1B, 0x12, 0x13, 0x1D, 0x15, 0x16, 0x17,
              0x1A, 0x09, 0x0A, 0x0B, 0x1E, 0x0D, 0x0E, 0x0F]
GCR_DECODE = {code: nib for nib, code in enumerate(GCR_ENCODE)}


def dshot_frame(value: int, telemetry: bool = False, bidir: bool = True) -> int:
    """16-bit DShot frame: 11-bit value (0 = disarmed, 1..47 commands, 48..2047 throttle),
    telemetry request bit, 4-bit CRC (inverted for bidirectional DShot)."""
    v = ((value & 0x7FF) << 1) | int(telemetry)
    crc = (v ^ (v >> 4) ^ (v >> 8)) & 0xF
    if bidir:
        crc = ~crc & 0xF
    return (v << 4) | crc


def erpm_payload(period_us: int) -> int:
    """12-bit eee mmmmmmmmm (period = m << e) plus checksum: the 16 bits an ESC sends."""
    e, m = 0, period_us
    while m > 0x1FF:
        m >>= 1
        e += 1
    v = (e << 9) | m
    crc = ~(v ^ (v >> 4) ^ (v >> 8)) & 0xF
    return (v << 4) | crc


def reply_line_bits(payload16: int) -> list:
    """The 21 line levels (bit 0 first) an ESC drives for a 16-bit payload."""
    g = 0
    for shift in (12, 8, 4, 0):
        g = (g << 5) | GCR_ENCODE[(payload16 >> shift) & 0xF]
    value = (1 << 20) | g                       # a 1 marks a transition; bit 20 = start edge
    level, bits = 1, []
    for i in range(20, -1, -1):
        if (value >> i) & 1:
            level ^= 1
        bits.append(level)
    return bits


def samples_from_words(words) -> list:
    """Raw samples of dshot_bidir: word 0 first, bit 31 first."""
    return [(w >> (31 - i)) & 1 for w in words for i in range(32)]


def decode_samples(samples, samples_per_bit: float):
    """Betaflight-style decode of raw samples (starting at the first low sample).
    Returns the 12-bit eee:mmmmmmmmm value, or None if invalid / no reply."""
    if not samples or samples[0] != 0:
        return None
    runs, cur, n = [], samples[0], 0
    for s in samples:
        if s == cur:
            n += 1
        else:
            runs.append(n)
            cur, n = s, 1
    runs.append(n)                              # last run merges with the idle-high line
    value, bits = 0, 0
    for i, r in enumerate(runs):
        ln = 21 - bits if i == len(runs) - 1 else int(r / samples_per_bit + 0.5)
        if ln <= 0 or bits + ln > 21:
            return None
        value = (value << ln) | (1 << (ln - 1))
        bits += ln
        if bits == 21:
            break
    if bits != 21:
        return None
    decoded = 0
    for k in range(4):
        code = (value >> (5 * k)) & 0x1F
        if code not in GCR_DECODE:
            return None
        decoded |= GCR_DECODE[code] << (4 * k)
    c = decoded ^ (decoded >> 8)
    c ^= c >> 4
    if (c & 0xF) != 0xF:
        return None
    return decoded >> 4


def erpm_period_us(value12: int) -> int:
    return (value12 & 0x1FF) << (value12 >> 9)
