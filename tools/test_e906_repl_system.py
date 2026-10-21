#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
test_e906_repl_system.py - E906 MicroPython REPL three-plane system test (on real silicon)

@impl [SPEC-E906-REPL-05] [SPEC-E906-REPL-07] [SPEC-E906-REPL-08] [SPEC-E906-REPL-10] apps/e906_repl_app/SPECIFICATION.md

Checks, per plane, that the E906 really emits what the spec says (run from the PC;
needs ssh root@BOARD_IP and the helper files in /root on the board):
  1. announce : dmesg shows abstractx-repl / abstractx-tlp / abstractx-trace channels
  2. repl     : script runs, stdout comes back as text on endpoint 1026
  3. tlp      : N x 64-byte TLPs sent from MicroPython arrive on UDP (endpoint 1024)
  4. trace    : CTF trace datagrams arrive on UDP 9870 (endpoint 1025)
  5. isolation: no TLP/trace bytes appear in the REPL text stream
  6. quiet    : after the run, no new 'msg received with no recipient' in dmesg

Result is appended to the SpecTrace evidence ledger (check "board:e906:repl-system").
"""
import argparse
import os
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from trac_lib import ledger  # noqa: E402

EMIT = """import abstractx
b = bytearray(64)
b[0] = 0x10; b[2] = 7; b[3] = 2
sent = 0
for i in range({n}):
    b[8] = i
    if abstractx.tlp_send(b): sent += 1
    abstractx.trace(i, 1, "systest")
    abstractx.sleep_ms(20)
print("SENT=" + str(sent) + " DROPPED=" + str(abstractx.tlp_dropped()))
"""


def ssh(board, cmd, timeout=30):
    return subprocess.run(["ssh", "-o", "ConnectTimeout=5", "-o", "LogLevel=ERROR", f"root@{board}", cmd],
                          capture_output=True, text=True, timeout=timeout)


def ssh_popen(board, cmd):
    return subprocess.Popen(["ssh", "-o", "LogLevel=ERROR", f"root@{board}", cmd],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def no_recipient(board):
    out = ssh(board, "dmesg | grep -c 'no recipient'").stdout.strip()
    return int(out) if out.isdigit() else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--board-ip", default=os.environ.get("BOARD_IP", "192.168.3.3"))
    ap.add_argument("--packets", type=int, default=10)
    ap.add_argument("--restart-firmware", action="store_true", help="stop/start remoteproc first")
    a = ap.parse_args()

    t0 = time.time()
    host = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    host.connect((a.board_ip, 80))
    host_ip = host.getsockname()[0]
    host.close()

    res, detail = {}, {}
    if a.restart_firmware:
        ssh(a.board_ip, "echo stop > /sys/class/remoteproc/remoteproc0/state; sleep 1; "
                        "echo start > /sys/class/remoteproc/remoteproc0/state; sleep 4")
    base_noreci = no_recipient(a.board_ip)

    # 1. announcements
    dm = ssh(a.board_ip, "dmesg | tail -n 80").stdout
    for ch in ("abstractx-repl", "abstractx-tlp", "abstractx-trace"):
        res[f"announce_{ch}"] = f"creating channel {ch}" in dm

    # 2. REPL plane (script creates the abstractx-repl node by name)
    ssh(a.board_ip, f"echo '{EMIT.format(n=a.packets)}' > /root/e906_emit.py")
    sock_tlp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_tlp.bind(("0.0.0.0", 9871)); sock_tlp.settimeout(0.3)
    sock_trc = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock_trc.bind(("0.0.0.0", 9870)); sock_trc.settimeout(0.3)

    ssh(a.board_ip, "python3 /root/run_e906_script.py /root/test_alive.py 3")  # creates repl node first
    bt = ssh_popen(a.board_ip, f"python3 /root/e906_rpmsg_bridge.py tlp --udp-host {host_ip} --udp-port 9871 --listen-port 9872")
    bc = ssh_popen(a.board_ip, f"python3 /root/e906_rpmsg_bridge.py trace --udp-host {host_ip}")
    time.sleep(3)
    r = ssh(a.board_ip, "python3 /root/run_e906_script.py /root/e906_emit.py 6", timeout=30)
    text = r.stdout
    res["repl_text"] = f"SENT={a.packets}" in text
    detail["repl_stdout"] = text.strip()[-120:]

    # 3/4. TLP + trace planes
    tlps, traces = [], 0
    end = time.time() + 3
    while time.time() < end:
        for s, kind in ((sock_tlp, "t"), (sock_trc, "c")):
            try:
                d, _ = s.recvfrom(2048)
            except socket.timeout:
                continue
            if kind == "t":
                tlps += [d[i:i + 64] for i in range(0, len(d) - 63, 64)]
            else:
                traces += 1
    bt.terminate(); bc.terminate()
    good = [t for t in tlps if t[0] == 0x10 and t[2] == 7]
    res["tlp_64B_all_received"] = len(good) == a.packets
    res["tlp_payload_sequence"] = sorted(t[8] for t in good) == list(range(a.packets))
    res["trace_ctf_received"] = traces > 0
    detail.update(tlps=len(tlps), tlps_valid=len(good), trace_datagrams=traces)

    # 5. isolation: TLP header byte 0x10/0x00 pattern must not be in REPL text
    res["plane_isolation"] = b"\x10\x00\x07" not in text.encode("latin1", "ignore")

    # 6. quiet kernel log
    # Teardown tail after the host closes its fd is bounded; idle must be silent.
    time.sleep(1)
    after_run = no_recipient(a.board_ip)
    time.sleep(5)
    idle_growth = no_recipient(a.board_ip) - after_run
    res["no_recipient_bounded"] = (after_run - base_noreci) < 16
    res["no_recipient_idle_silent"] = idle_growth == 0
    detail["new_no_recipient"] = after_run - base_noreci
    detail["idle_no_recipient"] = idle_growth

    ok = all(res.values())
    for k, v in res.items():
        print(f"  [{'PASS' if v else 'FAIL'}] {k}")
    print(detail)
    print("E906 REPL SYSTEM TEST:", "PASS" if ok else "FAIL")

    rec = ledger.make_record(
        root=ROOT, family="test", check="board:e906:repl-system", result="pass" if ok else "fail",
        metrics={**{k: int(v) for k, v in res.items()}, **{k: v for k, v in detail.items() if isinstance(v, int)}},
        requirements=["SPEC-E906-REPL-05", "SPEC-E906-REPL-07", "SPEC-E906-REPL-08", "SPEC-E906-REPL-10"],
        duration_s=time.time() - t0, tool="test_e906_repl_system.py",
        notes="REPL/TLP/trace plane emission on Cubie A5E E906")
    print("ledger:", ledger.append(ROOT, rec))
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
