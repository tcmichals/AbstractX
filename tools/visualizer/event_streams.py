# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-TRACE-09] [SPEC-TRACE-10] [SPEC-STUDIO-11] [SPEC-STUDIO-17] tools/visualizer/SPECIFICATION.md
"""Decode AbstractX profiler samples (CTF stream 4) and timeline spans (CTF stream 5).

Layouts are [SPEC-TRACE-09] / [SPEC-TRACE-10] in docs/DESIGN_SPECIFICATION.md and mirror
include/abstractx/trace/event_trace.hpp; tests/test_event_streams_decode.py checks them
against packets the C++ code produced.
"""
import struct

CTF_MAGIC = 0xC1FC1FC1
CTF_HEADER = struct.Struct("<IB3xIIQQII")    # 40 bytes (CtfPacketHeaderWire)
PROFILE_STREAM, PROFILE_EVENT = 4, 0x30
TIMELINE_STREAM, TIMELINE_EVENT = 5, 0x31
PROFILE_HEADER = struct.Struct("<BBHIII")    # 16 bytes
SAMPLE = struct.Struct("<II")                # 8 bytes: pc, owner
TIMELINE_HEADER = struct.Struct("<BBHIQII")  # 24 bytes
SPAN = struct.Struct("<IIIB3x")              # 16 bytes: start, duration, id, kind
assert (PROFILE_HEADER.size, SAMPLE.size, TIMELINE_HEADER.size, SPAN.size) == (16, 8, 24, 16)

OWNER_NONE, OWNER_IDLE = 0, 1
KIND_RESUME, KIND_ISR, KIND_IDLE = 1, 2, 3
KIND_NAMES = {KIND_RESUME: "resume", KIND_ISR: "isr", KIND_IDLE: "idle"}


def _stream(data: bytes):
    if len(data) < CTF_HEADER.size or struct.unpack_from("<I", data)[0] != CTF_MAGIC:
        return None
    return data[4]


def decode_profile(data: bytes):
    """Stream-4 packet -> {"sample_hz", "dropped", "samples": [(pc, owner), ...]} or None."""
    if _stream(data) != PROFILE_STREAM or len(data) < CTF_HEADER.size + PROFILE_HEADER.size:
        return None
    ev, ver, n, hz, dropped, _ = PROFILE_HEADER.unpack_from(data, CTF_HEADER.size)
    off = CTF_HEADER.size + PROFILE_HEADER.size
    if ev != PROFILE_EVENT or ver != 1 or len(data) < off + n * SAMPLE.size:
        return None
    return {"sample_hz": hz, "dropped": dropped,
            "samples": [SAMPLE.unpack_from(data, off + i * SAMPLE.size) for i in range(n)]}


def decode_timeline(data: bytes):
    """Stream-5 packet -> {"cycle_hz", "cycles_now", "dropped", "spans": [dict]} or None.
    Each span's 32-bit start is rebuilt to 64 bits from cycles_now (starts are < 2^32 old)."""
    if _stream(data) != TIMELINE_STREAM or len(data) < CTF_HEADER.size + TIMELINE_HEADER.size:
        return None
    ev, ver, n, hz, now, dropped, _ = TIMELINE_HEADER.unpack_from(data, CTF_HEADER.size)
    off = CTF_HEADER.size + TIMELINE_HEADER.size
    if ev != TIMELINE_EVENT or ver != 1 or len(data) < off + n * SPAN.size:
        return None
    spans = []
    for i in range(n):
        start, dur, sid, kind = SPAN.unpack_from(data, off + i * SPAN.size)
        start64 = now - ((now - start) & 0xFFFFFFFF)
        spans.append({"start": start64, "duration": dur, "id": sid, "kind": kind})
    return {"cycle_hz": hz, "cycles_now": now, "dropped": dropped, "spans": spans}


def encode_profile(samples, sample_hz=1000, dropped=0, ts_us=0) -> bytes:
    """Inverse of decode_profile() (tests and --sim)."""
    body = PROFILE_HEADER.pack(PROFILE_EVENT, 1, len(samples), sample_hz, dropped, 0)
    body += b"".join(SAMPLE.pack(pc, owner) for pc, owner in samples)
    total = CTF_HEADER.size + len(body)
    return CTF_HEADER.pack(CTF_MAGIC, PROFILE_STREAM, total * 8, total * 8, ts_us, ts_us, 0, 0) + body


def encode_timeline(spans, cycle_hz, cycles_now, dropped=0, ts_us=0) -> bytes:
    """Inverse of decode_timeline() (tests and --sim)."""
    body = TIMELINE_HEADER.pack(TIMELINE_EVENT, 1, len(spans), cycle_hz, cycles_now, dropped, 0)
    body += b"".join(SPAN.pack(s["start"] & 0xFFFFFFFF, s["duration"], s["id"], s["kind"]) for s in spans)
    total = CTF_HEADER.size + len(body)
    return CTF_HEADER.pack(CTF_MAGIC, TIMELINE_STREAM, total * 8, total * 8, ts_us, ts_us, 0, 0) + body
