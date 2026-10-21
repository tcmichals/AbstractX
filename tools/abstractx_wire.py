#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
abstractx_wire.py - AbstractX wire formats for host tools (stdlib only, runs on the board).

  TLP64 : 20-byte header + 40-byte payload + 4-byte trailer (include/asp_tlp64.h)
  CTF   : 40-byte CtfPacketHeaderWire + packed events (include/abstractx/trace/tracer.hpp)
"""
# @impl [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md

import struct

TLP_BYTES = 64
TLP_TYPE_DATA = 0x10
TLP_HEADER = struct.Struct("<BBBBIHHQ")  # type flags tag channel addr length_dw seq timestamp_ns

CTF_MAGIC = 0xC1FC1FC1
CTF_HEADER = struct.Struct("<IB3xIIQQII")  # CtfPacketHeaderWire, 40 B
CORO_EVENT = struct.Struct("<BQIIBB8s11s")  # CoroEventPayload, 38 B
CORO_STATES = {0: "SPAWN", 1: "SUSPEND", 2: "RESUME", 3: "DONE"}


def split_tlps(data: bytes) -> list:
    """Whole 64-byte TLPs in one datagram / RPMsg message (up to 7 per message)."""
    return [data[i:i + TLP_BYTES] for i in range(0, len(data) - TLP_BYTES + 1, TLP_BYTES)]


def decode_tlp(frame: bytes) -> dict:
    if len(frame) != TLP_BYTES:
        raise ValueError(f"TLP must be {TLP_BYTES} bytes, got {len(frame)}")
    ty, flags, tag, ch, addr, length_dw, seq, ts = TLP_HEADER.unpack_from(frame)
    return {"type": ty, "flags": flags, "tag": tag, "channel": ch, "addr": addr,
            "length_dw": length_dw, "seq": seq, "timestamp_ns": ts,
            "payload": frame[TLP_HEADER.size:TLP_HEADER.size + 40], "trailer": frame[60:64]}


def is_ctf_packet(data: bytes) -> bool:
    return len(data) >= CTF_HEADER.size and struct.unpack_from("<I", data)[0] == CTF_MAGIC


def _cstr(raw: bytes) -> str:
    return raw.split(b"\x00", 1)[0].decode("utf-8", "replace")


def decode_ctf_packet(data: bytes) -> dict:
    """Decode one CTF packet; raises ValueError when the framing is inconsistent."""
    if len(data) < CTF_HEADER.size:
        raise ValueError(f"CTF packet shorter than its {CTF_HEADER.size}-byte header: {len(data)}")
    magic, stream_id, pkt_bits, content_bits, t_begin, t_end, discarded, _pad = CTF_HEADER.unpack_from(data)
    if magic != CTF_MAGIC:
        raise ValueError(f"bad CTF magic 0x{magic:08X}")
    content = content_bits // 8
    if content_bits % 8 or content != len(data):
        raise ValueError(f"content_size_bits={content_bits} does not match datagram length {len(data)}")
    if pkt_bits < content_bits:
        raise ValueError(f"packet_size_bits={pkt_bits} < content_size_bits={content_bits}")
    body = data[CTF_HEADER.size:content]
    if stream_id != 0 or len(body) % CORO_EVENT.size:
        raise ValueError(f"stream {stream_id}: {len(body)} event bytes is not a whole number of "
                         f"{CORO_EVENT.size}-byte coroutine events")
    events = []
    for off in range(0, len(body), CORO_EVENT.size):
        ev_id, ts_us, task_id, handle, state, reason, name, token = CORO_EVENT.unpack_from(body, off)
        events.append({"event_id": ev_id, "timestamp_us": ts_us, "task_id": task_id, "handle": handle,
                       "state": state, "reason": reason, "name": _cstr(name), "token": _cstr(token)})
    return {"stream_id": stream_id, "packet_size_bits": pkt_bits, "content_size_bits": content_bits,
            "timestamp_begin_us": t_begin, "timestamp_end_us": t_end,
            "events_discarded": discarded, "events": events}

