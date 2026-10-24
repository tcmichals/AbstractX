#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
E906 on-board self-test ([SPEC-E906-REPL-15]). Runs on the Cubie A5E:
  python3 run_board_tests.py          (no pytest needed)
  python3 -m pytest test_e906_planes.py   (when the image has pytest)
Every check reads the real endpoints; nothing here is synthesised on the host.
"""
# @verifies [SPEC-E906-REPL-05] [SPEC-E906-REPL-06] [SPEC-E906-REPL-07] [SPEC-E906-REPL-08] [SPEC-E906-REPL-10] [SPEC-E906-REPL-14] [SPEC-E906-REPL-15] apps/e906_repl_app/SPECIFICATION.md

import glob
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from board_common import skip  # noqa: E402
from e906_link import RPROC, Plane, on_e906_board, read_text, repl_exec  # noqa: E402
import abstractx_wire as wire  # noqa: E402

N = 10
EXPECTED_FIRMWARE = os.environ.get("E906_FIRMWARE", "e906_repl_app.elf")
# [SPEC-E906-REPL-06] and [SPEC-E906-REPL-10]: the abstractx API the spec promises.
SPEC_API = ("peek", "peek_hex", "poke", "version", "cycles", "alive", "uart_init", "uart_read",
            "uart_read_into", "uart_dma", "uart_write", "uart_status", "i2c_init", "i2c_scan",
            "i2c_read", "i2c_read_into", "i2c_write", "sleep_ms", "ticks_ms", "console_read",
            "console_read_into", "tlp_send", "tlp_push", "tlp_poll_into", "tlp_dropped", "trace",
            "irq_stats", "dma_bench")
# Listed under 'Not implemented yet' in [SPEC-E906-REPL-10]; not required here.
COMPASS_ADDRS = (0x0D, 0x1E)
# RPMsg framing ([SPEC-E906-IPC-04], engineering log 2026-10-18 #3): 512 B buffer - 16 B header.
RPMSG_MAX_PAYLOAD = 496
RPMSG_MAX_TLPS = RPMSG_MAX_PAYLOAD // 64
TLP_TAG = 7


def _require_board():
    if not on_e906_board():
        skip("not an E906 board (no remoteproc0)")


def _ok(code: str, timeout_s: float = 10.0) -> str:
    out, err = repl_exec(code, timeout_s)
    assert not err.strip(), f"E906 raised: {err.strip()[-300:]}"
    return out


def _no_recipient_count() -> int:
    log = subprocess.run(["dmesg"], capture_output=True, text=True).stdout
    return log.count("no recipient")


def test_remoteproc_runs_expected_firmware():
    _require_board()
    assert read_text(RPROC + "/state") == "running", "remoteproc0 is not running"
    fw = read_text(RPROC + "/firmware")
    real = os.path.basename(os.path.realpath("/lib/firmware/" + fw))
    assert EXPECTED_FIRMWARE in (fw, real), f"remoteproc0 runs {fw!r} (-> {real!r}), expected {EXPECTED_FIRMWARE!r}"


def test_three_channels_announced():
    _require_board()
    names = " ".join(os.path.basename(p) for p in glob.glob("/sys/bus/rpmsg/devices/*"))
    missing = [n for n in ("abstractx-repl", "abstractx-tlp", "abstractx-trace") if n not in names]
    assert not missing, f"channels not announced by the E906: {missing} (have: {names or 'none'})"


def test_repl_round_trip():
    _require_board()
    out = _ok("import abstractx\nprint('ALIVE', abstractx.alive())\nprint(abstractx.version())")
    assert "ALIVE True" in out, out


def test_abstractx_api_matches_spec():
    _require_board()
    # getattr, not dir(): the E906 build has no dir() ([SPEC-E906-REPL-02] SRAM budget)
    out = _ok("import abstractx\nfor n in %r:\n    try:\n        getattr(abstractx, n)\n"
              "    except AttributeError:\n        print('MISSING', n)\nprint('DONE')" % (SPEC_API,))
    assert "DONE" in out, out
    missing = [ln.split()[1] for ln in out.splitlines() if ln.startswith("MISSING ")]
    assert not missing, f"abstractx lacks spec'd functions: {missing}"


def test_tlp_plane_delivers_every_frame():
    _require_board()
    code = (f"import abstractx\nb = bytearray(64)\nb[0] = 0x10; b[2] = {TLP_TAG}; b[3] = 2\n"
            f"sent = 0\nfor i in range({N}):\n    b[20] = i\n    if abstractx.tlp_send(b): sent += 1\n"
            f"    abstractx.sleep_ms(20)\nprint('SENT', sent, 'DROPPED', abstractx.tlp_dropped())")
    with Plane("tlp") as tlp:
        out = _ok(code)
        msgs = tlp.read_messages(1.5)
    assert f"SENT {N} DROPPED 0" in out, out
    frames = [wire.decode_tlp(f) for m in msgs for f in wire.split_tlps(m)]
    sizes = [len(m) for m in msgs]
    assert all(0 < n <= RPMSG_MAX_TLPS * 64 and n % 64 == 0 for n in sizes), \
        f"TLP messages must hold 1..{RPMSG_MAX_TLPS} whole TLPs: sizes {sizes}"
    ours = [f for f in frames if f["type"] == wire.TLP_TYPE_DATA and f["tag"] == TLP_TAG]
    assert sorted(f["payload"][0] for f in ours) == list(range(N)), \
        f"got {len(ours)}/{N} TLPs, payload seq {[f['payload'][0] for f in ours]}"


def test_trace_plane_ctf_decodes():
    _require_board()
    code = (f"import abstractx\nfor i in range({N}):\n    abstractx.trace(i, 1, 'systest')\n"
            f"    abstractx.sleep_ms(10)\nprint('TRACED')")
    with Plane("trace") as trc:
        out = _ok(code)
        msgs = trc.read_messages(1.5)
    assert "TRACED" in out, out
    assert msgs, "no datagrams on abstractx-trace"
    assert max(len(m) for m in msgs) <= RPMSG_MAX_PAYLOAD, f"trace message over {RPMSG_MAX_PAYLOAD} B"
    bad = [m[:8].hex() for m in msgs if not wire.is_ctf_packet(m)]
    assert not bad, f"{len(bad)} trace messages lack CTF magic, first bytes {bad[:3]}"
    events = [e for m in msgs for e in wire.decode_ctf_packet(m)["events"]]
    ours = [e for e in events if e["name"] == "systest"]
    assert sorted(e["task_id"] for e in ours) == list(range(N)), \
        f"decoded {len(events)} events, systest task ids {[e['task_id'] for e in ours]}"
    assert all(e["state"] == 1 for e in ours)


def test_repl_text_has_no_binary_planes():
    _require_board()
    out = _ok("import abstractx\nb = bytearray(64)\nb[0] = 0x10; b[2] = 7\nabstractx.tlp_send(b)\n"
              "abstractx.trace(1, 1, 'iso')\nprint('ISO-DONE')")
    raw = out.encode("latin-1", "replace")
    assert "ISO-DONE" in out
    assert b"\xc1\x1f\xfc\xc1" not in raw, "CTF magic leaked into REPL text"
    assert b"\x10\x00\x07" not in raw, "TLP header leaked into REPL text"


def test_compass_answers_and_changes():
    """The data registers move between samples (log 2026-10-21: scripts read the HMC5883L's
    config registers as if it were a QMC5883L, so the heading never changed)."""
    _require_board()
    out = _ok("import abstractx\nabstractx.i2c_init(400000)\nprint('SCAN', abstractx.i2c_scan())\n"
              "print('ID', list(abstractx.i2c_read(0x1E, 0x0A, 3)))")
    found = [a for a in COMPASS_ADDRS if f"{a}" in out.split("SCAN", 1)[-1].split("ID")[0]]
    assert found, f"no compass at {[hex(a) for a in COMPASS_ADDRS]} on I2C1: {out.strip()}"
    hmc = "[72, 52, 51]" in out  # b"H43"
    if hmc:
        cfg = ("abstractx.i2c_write(0x1E, 0x00, b'\\x70')\nabstractx.i2c_write(0x1E, 0x01, b'\\x20')\n"
               "abstractx.i2c_write(0x1E, 0x02, b'\\x00')\n")
        read = "abstractx.i2c_read(0x1E, 0x03, 6)"
    else:
        cfg = "abstractx.i2c_write(0x0D, 0x0B, b'\\x01')\nabstractx.i2c_write(0x0D, 0x09, b'\\x1d')\n"
        read = "abstractx.i2c_read(0x0D, 0x00, 6)"
    out = _ok("import abstractx\nabstractx.i2c_init(400000)\n" + cfg +
              "for i in range(8):\n    abstractx.sleep_ms(80)\n    print('MAG', list(" + read + "))")
    # list(), not .hex(): bytes.hex() is not in the E906 build
    samples = [bytes(eval(ln[4:], {"__builtins__": {}})).hex() for ln in out.splitlines() if ln.startswith("MAG ")]
    assert len(samples) == 8, out
    assert len(set(samples)) > 1, f"compass readings never change: {samples[0]}"
    assert not all(s in ("000000000000", "ffffffffffff") for s in samples), samples


def test_gps_bytes_arrive():
    _require_board()
    seen = {}
    for baud in (115200, 9600):
        out = _ok(f"import abstractx\nabstractx.uart_init({baud})\nacc = b''\nt = abstractx.ticks_ms()\n"
                  f"while abstractx.ticks_ms() - t < 1500 and len(acc) < 600:\n"
                  f"    d = abstractx.uart_read(128)\n    if d: acc += d\n    abstractx.sleep_ms(10)\n"
                  f"print('GPS', list(acc))", timeout_s=8)
        line = [ln for ln in out.splitlines() if ln.startswith("GPS ")][-1]
        data = bytes(eval(line[4:], {"__builtins__": {}}))
        seen[baud] = len(data)
        if b"$G" in data or b"\xb5\x62" in data:
            return
    assert False, f"no NMEA or UBX on UART3 at 115200 or 9600 (bytes seen per baud: {seen})"


def test_streamer_stops_without_heartbeat():
    _require_board()
    path = os.path.join(HERE, "raw_sensor_streamer.py")
    if not os.path.exists(path):
        skip("raw_sensor_streamer.py not copied next to the tests")
    with open(path) as f:
        code = f.read()
    t0 = time.monotonic()
    out, err = repl_exec(code, timeout_s=12)
    dt = time.monotonic() - t0
    assert "[E906_WATCHDOG_TIMEOUT_SHUTDOWN]" in out, f"streamer did not stop on its own: {out[-200:]} {err[-200:]}"
    assert dt < 10, f"watchdog took {dt:.1f}s (limit 3 s plus sensor init)"


def test_streamer_keeps_running_with_heartbeat():
    """Studio's live link: board_studio_bridge.py pings, the streamer must not time out.
    (2026-10-21: the VM hook drained the console queue looking for Ctrl-C and ate every PING,
    so Studio connected, showed data for ~3 s and disconnected.)"""
    _require_board()
    bridge = "/root/board_studio_bridge.py"
    if not os.path.exists(bridge):
        skip("board_studio_bridge.py not in /root (README §8.1 step 4)")
    import socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("127.0.0.1", 0))
    sock.settimeout(0.5)
    env = dict(os.environ, STUDIO_HOST_IP="127.0.0.1", STUDIO_PORT=str(sock.getsockname()[1]),
               STREAM_DURATION="25")  # past the old 15 s runner limit
    proc = subprocess.Popen(["python3", "-u", bridge], env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    t0 = time.monotonic()
    last = t0
    count = 0
    while time.monotonic() - t0 < 30:
        try:
            sock.recvfrom(4096)
            count += 1
            last = time.monotonic()
        except socket.timeout:
            if proc.poll() is not None:
                break
    out = proc.communicate(timeout=10)[0]
    sock.close()
    assert "WATCHDOG_TIMEOUT" not in out, f"streamer timed out while the bridge pinged: {out[-300:]}"
    assert "Traceback" not in out, f"bridge crashed: {out[-400:]}"
    # GPS bytes must arrive whole: a slow script drain overflowed the UART ring and
    # ~7 NMEA sentences/s failed their checksum (2026-10-21). One partial at start is normal.
    bad = [int(w.split("=")[1]) for ln in out.splitlines() for w in ln.split() if w.startswith("NMEA_bad=")]
    assert bad and bad[-1] <= 2, f"{bad[-1] if bad else '?'} NMEA sentences failed their checksum"
    assert last - t0 > 22, f"packets stopped after {last - t0:.1f} s of 25 ({count} received)"


def test_kernel_log_quiet_when_idle():
    _require_board()
    before = _no_recipient_count()
    time.sleep(3)
    after = _no_recipient_count()
    assert after == before, f"{after - before} 'no recipient' messages while idle"


# ---- Message protocol: each check below is a failure the engineering log recorded ----

def test_repl_output_is_line_buffered():
    """Log #13: one RPMsg message per character flooded the vring 60x. Expect about one per line."""
    _require_board()
    lines = 20
    sizes = []
    out, err = repl_exec(f"for i in range({lines}):\n    print('L%02d' % i, 'x' * 36)", msg_sizes=sizes)
    assert not err.strip(), err
    assert out.count("\n") >= lines, out
    assert max(sizes) <= RPMSG_MAX_PAYLOAD, f"REPL message over {RPMSG_MAX_PAYLOAD} B: {max(sizes)}"
    assert len(sizes) <= lines + 6, f"{len(sizes)} messages for {lines} lines (char-by-char flushing?)"


def test_large_script_survives_chunking():
    """Scripts go in 256-byte chunks; a lost or reordered chunk must show up."""
    _require_board()
    blob = "".join(chr(48 + (i * 7) % 75) for i in range(1800))
    expected = 0
    for i, ch in enumerate(blob):
        expected = (expected * 31 + ord(ch) + i) % 65521
    out = _ok(f"s = {blob!r}\nh = 0\nfor i in range(len(s)):\n    h = (h * 31 + ord(s[i]) + i) % 65521\n"
              f"print('HASH', len(s), h)", timeout_s=20)
    assert f"HASH {len(blob)} {expected}" in out, f"expected HASH {len(blob)} {expected}, got {out.strip()[-80:]}"


def test_ctrl_c_interrupts_running_script():
    """Log #13: Ctrl-C was scheduled but not dispatched, so loops never stopped."""
    _require_board()
    t0 = time.monotonic()
    out, err = repl_exec("import abstractx\nwhile True:\n    abstractx.sleep_ms(1)", timeout_s=5,
                         interrupt_after_s=0.5)
    assert "KeyboardInterrupt" in err, f"no KeyboardInterrupt after Ctrl-C: out={out[-100:]!r} err={err[-100:]!r}"
    assert time.monotonic() - t0 < 3, "Ctrl-C took more than 2.5 s to stop the loop"


def test_reopened_tlp_endpoint_drops_nothing():
    """Log #15: a stale endpoint left the E906 without a host address; the first run dropped 10/10."""
    _require_board()
    with Plane("tlp"):
        pass  # leave a closed, stale node behind like a crashed bridge would
    with Plane("tlp") as tlp:
        out = _ok("import abstractx\nb = bytearray(64)\nb[0] = 0x10; b[2] = 8\nn = 0\n"
                  "for i in range(5):\n    n += 1 if abstractx.tlp_send(b) else 0\n"
                  "print('SENT', n, 'DROPPED', abstractx.tlp_dropped())")
        got = [f for m in tlp.read_messages(1.0) for f in wire.split_tlps(m) if f[2] == 8]
    assert "DROPPED 0" in out and len(got) == 5, f"{out.strip()} received {len(got)}/5"


def test_kernel_log_tail_bounded_after_run():
    """Log #15: closing the host fd leaves a short 'no recipient' tail (6 measured); it must stay bounded."""
    _require_board()
    before = _no_recipient_count()
    _ok("print('tail')")
    time.sleep(1)
    tail = _no_recipient_count() - before
    assert tail < 16, f"{tail} 'no recipient' messages after one REPL run"
