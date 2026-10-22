#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Off-board checks for the E906 board suite ([SPEC-E906-REPL-15]): wire decoders match the
C++ layouts, raw-REPL reply parsing, and the runner's exit codes. Plain python3 or pytest.
"""
# @verifies [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md

import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tests" / "board"))

import abstractx_wire as wire  # noqa: E402
from e906_link import parse_raw_repl  # noqa: E402


def _ctf(events, magic=wire.CTF_MAGIC, length_fix=0):
    body = b"".join(wire.CORO_EVENT.pack(1, 100 + i, tid, 0, state, 0, name.encode(), b"repl")
                    for i, (tid, state, name) in enumerate(events))
    n = wire.CTF_HEADER.size + len(body)
    return wire.CTF_HEADER.pack(magic, 0, 1024 * 8, (n + length_fix) * 8, 100, 200, 0, 0) + body


def test_layouts_match_cpp_sources():
    tracer = (ROOT / "include/abstractx/trace/tracer.hpp").read_text()
    assert "static_assert(sizeof(CoroEventPayload) == 38" in tracer
    assert wire.CORO_EVENT.size == 38
    fields = re.search(r"struct CtfPacketHeaderWire \{(.*?)\};", tracer, re.S).group(1)
    sizes = {"uint32_t": 4, "uint8_t": 1, "uint64_t": 8}
    total = 0
    for ty, name in re.findall(r"(uint\d+_t)\s+(\w+(?:\[\d+\])?);", fields):
        count = int(re.search(r"\[(\d+)\]", name).group(1)) if "[" in name else 1
        total += sizes[ty] * count
    assert total == wire.CTF_HEADER.size == 40, (total, wire.CTF_HEADER.size)
    tlp = (ROOT / "include/asp_tlp64.h").read_text()
    assert "#define ASP_TLP64_SIZE          64" in tlp and "#define ASP_TLP64_PAYLOAD_SIZE  40" in tlp
    assert wire.TLP_HEADER.size == 20 and wire.TLP_HEADER.size + 40 + 4 == wire.TLP_BYTES


def test_ctf_decoder_accepts_good_and_rejects_bad():
    pkt = _ctf([(0, 1, "systest"), (1, 1, "systest"), (2, 2, "other")])
    d = wire.decode_ctf_packet(pkt)
    assert [e["task_id"] for e in d["events"]] == [0, 1, 2]
    assert d["events"][0]["name"] == "systest" and d["events"][0]["token"] == "repl"
    for bad in (_ctf([(0, 1, "x")], magic=0x12345678), _ctf([(0, 1, "x")], length_fix=4), pkt[:-3]):
        try:
            wire.decode_ctf_packet(bad)
        except ValueError:
            continue
        raise AssertionError("decoder accepted a malformed packet")
    assert wire.is_ctf_packet(pkt) and not wire.is_ctf_packet(b"\x10" * 64)


def test_tlp_split_and_decode():
    frame = wire.TLP_HEADER.pack(0x10, 0, 7, 2, 0x40000100, 16, 5, 99) + bytes(range(40)) + b"\xde\xad\xbe\xef"
    frames = wire.split_tlps(frame * 3 + b"\x00" * 10)
    assert len(frames) == 3
    d = wire.decode_tlp(frames[0])
    assert (d["type"], d["tag"], d["seq"], d["payload"][0]) == (0x10, 7, 5, 0)


def test_raw_repl_reply_parsing():
    assert parse_raw_repl(b"raw REPL; CTRL-B to exit\r\n>OKhello\r\n\x04\x04>") == ("hello\r\n", "")
    assert parse_raw_repl(b">OK\x04Traceback...\r\nKeyboardInterrupt:\r\n\x04>")[1].endswith("KeyboardInterrupt:\r\n")
    assert parse_raw_repl(b">OKpartial") is None


def test_runner_reports_could_not_run_off_board():
    r = subprocess.run([sys.executable, str(ROOT / "tests/board/run_board_tests.py")],
                       capture_output=True, text=True)
    assert r.returncode == 2, r.stdout + r.stderr
    n = int(re.search(r"(\d+) tests discovered", r.stdout).group(1))
    assert n >= 16, r.stdout


def test_compass_bytes_decode_on_the_host():
    """The E906 sends compass registers as read; the app's host side decodes per chip."""
    sys.path.insert(0, str(ROOT / "apps" / "e906_repl_app"))
    import sensor_contract as wire  # the app's format, not AbstractX's
    hmc = bytes([0xFF, 0xB7, 0x02, 0x2A, 0xFF, 0xF7])  # X=-73, Z=554, Y=-9 (board, 2026-10-21)
    assert wire.decode_mag(hmc, "HMC5883L") == (-73, -9, 554)
    qmc = struct.pack("<hhh", 100, -200, 300)
    assert wire.decode_mag(qmc, "QMC5883L") == (100, -200, 300)
    assert wire.decode_mag(qmc) == (100, -200, 300), "untagged lines are QMC5883L"
    # bridge (encode) and Studio plugin (decode) must agree on the tag-3 payload
    sample = wire.decode_mag_payload(wire.encode_mag_payload(hmc, "HMC5883L") + bytes(26))
    assert sample["chip"] == "HMC5883L" and sample["xyz"] == (-73, -9, 554) and sample["raw"] == hmc
    assert 50 < sample["field_ut"] < 52, sample["field_ut"]  # |v| = 559 LSB / 1090 LSB/Ga
    tampered = bytearray(wire.encode_mag_payload(hmc, "HMC5883L"))
    tampered[8] ^= 1
    try:
        wire.decode_mag_payload(bytes(tampered))
    except ValueError:
        pass
    else:
        raise AssertionError("decode_mag_payload accepted X/Y/Z that disagree with the raw bytes")
    for bad in ((hmc[:5], "HMC5883L"), (hmc, "XYZ")):
        try:
            wire.decode_mag(*bad)
        except ValueError:
            continue
        raise AssertionError(f"decode_mag accepted {bad!r}")


def test_nmea_checksum_gate():
    """Only checksummed NMEA reaches the GPS packet (a garbled sentence killed the stream)."""
    sys.path.insert(0, str(ROOT / "apps" / "e906_repl_app"))
    import sensor_contract as sc
    good = "$GNGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*" 
    body = good[1:-1]
    cs = 0
    for ch in body:
        cs ^= ord(ch)
    good += f"{cs:02X}"
    assert sc.nmea_sentence_ok(good)
    assert not sc.nmea_sentence_ok(good.replace("545.4", "9e99"))   # garbled field
    assert not sc.nmea_sentence_ok(good[:-3])                        # no checksum
    assert not sc.nmea_sentence_ok("GNGGA,1*00")                     # no '$'
    assert sc.clamp_i32(10 ** 12) == 2147483647 and sc.clamp_i32(-10 ** 12) == -2147483648


def test_ctf_decoder_takes_status_reports():
    """Stream 3 is a runtime status report ([SPEC-TRACE-08]), not malformed coroutine events."""
    sys.path.insert(0, str(ROOT / "tools" / "visualizer"))
    import runtime_stats as rs
    pkt = rs.encode({"timestamp_us": 1, "cycle_hz": 200_000_000, "cycles": 2, "idle_cycles": 1, "steps": 1,
                     "pool_used": 0, "pool_high_water": 0, "pool_capacity": 4096, "dropped_resumes": 0,
                     "queue_depth": 0, "queue_capacity": 32, "trace_discarded": 0, "tasks": [], "drivers": []})
    d = wire.decode_ctf_packet(pkt)
    assert d["stream_id"] == 3 and d["events"] == [] and rs.decode(d["status"])["pool_capacity"] == 4096
    short = pkt[:44]
    short = short[:12] + struct.pack("<I", len(short) * 8) + short[16:]
    try:
        wire.decode_ctf_packet(short)
    except ValueError:
        return
    raise AssertionError("decoder accepted a truncated status report")


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
    print("[PASS] board suite (off-board checks)")
