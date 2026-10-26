#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-E906-IPC-02] [SPEC-E906-IPC-04] [SPEC-E906-CORO-01] targets/allwinner_e906/SPECIFICATION.md
"""
audit_ipc_invariants.py - static checks for inter-processor transports (RPMsg,
message box, UIO, rpmsg_char/epoll awaiters, the Linux Python bridges).

Written after the 2026-10-26 RPMsg review found bugs that every existing check
passed. Each check is named after the bug it would have caught:

  IPC-WAIT     a hardware wait with no limit (while (!X::send(..)), for (;;) with no bound)
  IPC-REREAD   a host-writable field (hdr->len, desc->len, desc->addr) read twice in one function
  IPC-PASSTHRU an address translation that returns an unknown address unchanged
  IPC-STALL    a ring-error path that returns without advancing or resyncing the ring
  IPC-ISRSHARE a coroutine handle shared with an ISR that is not std::atomic
  IPC-ZEROLEN  tx_commit(.., 0, ..): a 0-byte message used to give a buffer back
  IPC-ERRNO    errno tested in await_resume (stale: other code ran since the syscall)
  IPC-REPLYADDR an RpmsgMessage built for reply() with our own endpoint in .src (swapped)
  IPC-SLOT     await_suspend returning void that stores into a global wake slot
  PY-NBBREAK   non-blocking write loop that gives up for good on a transient error
  PY-NBSPIN    retry on BlockingIOError with no deadline
  PY-MMIO      register access through mmap slices / struct.*_from (width not guaranteed)
  PY-UIO0      hardcoded /dev/uio0 instead of finding the device by name
  (T-VERIFIES moved to `spectrace_matrix.py --check-verifies`, which covers the whole repo)
  T-MAP32      MAP_32BIT without #ifdef (x86-64 only: the test does not build elsewhere)

A deliberate exception is marked on the line (or the line before) with
`ipc-audit: allow <CHECK>` and a reason.

Usage:
  python3 tools/audit_ipc_invariants.py               # working tree
  python3 tools/audit_ipc_invariants.py --git-rev HEAD~1   # same files at another revision
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

CPP_GLOBS = [
    "targets/allwinner_e906/src/rpmsg*.cpp",
    "targets/allwinner_e906/include/hal/rpmsg*.hpp",
    "apps/*/src/rpmsg_console.cpp",
    "apps/e906_bench/bench.cpp",
    "targets/linux/include/abstractx/reactor/async_*.hpp",
]
PY_GLOBS = [
    "tools/e906_rpmsg_bridge.py",
    "apps/e906_repl_app/tests/board/e906_link.py",
    "hw/zynq7000/qmtech_zynq7020/tools/*.py",
    "hw/zynq7000/testApps/imu_backend_validation_poc/backends.py",
]
TEST_GLOBS = ["sim/test_e906_rpmsg_*.cpp"]


class Source:
    def __init__(self, rev):
        self.rev = rev

    def files(self, patterns):
        if self.rev is None:
            out = []
            for pat in patterns:
                out += sorted(p.relative_to(ROOT).as_posix() for p in ROOT.glob(pat))
            return out
        listed = subprocess.run(["git", "-C", str(ROOT), "ls-tree", "-r", "--name-only", self.rev],
                                capture_output=True, text=True, check=True).stdout.split()
        return [f for f in listed if any(Path(f).match(p) for p in patterns)]

    def read(self, rel):
        if self.rev is None:
            return (ROOT / rel).read_text(encoding="utf-8", errors="ignore")
        return subprocess.run(["git", "-C", str(ROOT), "show", f"{self.rev}:{rel}"],
                              capture_output=True, text=True, check=True).stdout


def line_of(text, pos):
    return text.count("\n", 0, pos) + 1


def allowed(text, pos, check):
    ln = line_of(text, pos)
    lines = text.splitlines()
    window = lines[max(0, ln - 2):ln]
    return any(f"ipc-audit: allow {check}" in l for l in window)


def block_after(text, open_pos):
    """Text of the {...} block starting at text[open_pos] == '{'."""
    depth = 0
    for i in range(open_pos, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[open_pos:i + 1]
    return text[open_pos:]


FUNC_RE = re.compile(r"\n[\w:<>\s\*&,~]*?\b([A-Za-z_]\w*)\s*\([^;{}]*\)\s*(?:const\s*)?(?:noexcept\s*)?(?:override\s*)?\{")


def functions(text):
    for m in FUNC_RE.finditer(text):
        name = m.group(1)
        if name in {"if", "for", "while", "switch", "return", "sizeof"}:
            continue
        yield name, m.end() - 1, block_after(text, m.end() - 1)


def check_cpp(rel, text, out):
    def report(check, pos, msg):
        if not allowed(text, pos, check):
            out.append(f"{rel}:{line_of(text, pos)}: [{check}] {msg}")

    for m in re.finditer(r"while\s*\(\s*!\s*\w+(?:::\w+)*::send\w*\s*\(", text):
        report("IPC-WAIT", m.start(), "spin on a hardware send with no limit; bound it and retry later")
    for m in re.finditer(r"(?:for\s*\(\s*;\s*;\s*\)|while\s*\(\s*(?:true|1)\s*\))\s*\{", text):
        body = block_after(text, text.index("{", m.start()))
        bounded = re.search(r"\b(k\w*(Tries|Rounds|Max|Limit|Budget)\w*|deadline|timeout)\b", body)
        progress = re.search(r"\b(last_avail_idx|continue)\b", body)
        hw = re.search(r"::(send|receive|is_rx_pending)\b|notify|drain", body)
        if hw and not bounded and not progress:
            report("IPC-WAIT", m.start(), "endless loop around hardware/peer state with no bound")

    for name, start, body in functions(text):
        for field in (r"\bhdr->len\b", r"\bdesc->len\b", r"\bdesc->addr\b", r"\.desc\[\w+\]\.len\b",
                      r"\.desc\[\w+\]\.addr\b"):
            hits = list(re.finditer(field, body))
            if len(hits) > 1:
                report("IPC-REREAD", start + hits[1].start(),
                       f"{name}(): host-writable {hits[0].group(0)} read {len(hits)}x; read once into a local")
        if name.startswith("translate"):
            params = re.search(r"\(([^)]*)\)", text[text.rfind("\n", 0, start):start])
            pnames = re.findall(r"(\w+)\s*(?:,|$)", params.group(1)) if params else []
            for r in re.finditer(r"return\s+(?:static_cast<[^>]+>\()?\s*(\w+)\s*\)?\s*;", body):
                if r.group(1) in pnames:
                    report("IPC-PASSTHRU", start + r.start(),
                           f"{name}() passes an unknown address through; reject it instead")
        if name == "await_resume":
            for e in re.finditer(r"\berrno\s*[!=]=", body):
                report("IPC-ERRNO", start + e.start(), "errno tested in await_resume; save it in the awaiter")
        if name == "await_suspend":
            sig = text[text.rfind("\n", 0, start):start]
            if re.search(r"\bvoid\b", sig) and re.search(r"\bs_\w+\s*=\s*handle\b", body):
                report("IPC-SLOT", start, "void await_suspend stores into a global slot: a second waiter overwrites the first")

    for m in re.finditer(r"ring_errors\w*\.fetch_add\([^;]*;\s*([^}]*)\}", text):
        tail = m.group(1)
        if re.search(r"\breturn\b", tail) and "last_avail_idx" not in tail:
            ctx = text[max(0, m.start() - 300):m.start()]
            if "avail->ring[" in ctx:
                report("IPC-STALL", m.start(), "ring error returns without consuming or resyncing: RX/TX stalls for good")

    if re.search(r'extern\s+"C"[^\n]*\n[^\n]*_isr\s*\(', text):
        for m in re.finditer(r"static\s+std::coroutine_handle<>\s+\w+", text):
            report("IPC-ISRSHARE", m.start(), "coroutine handle shared with an ISR must be std::atomic")

    for m in re.finditer(r"tx_commit\(\s*[^,()]+,\s*0\s*,", text):
        report("IPC-ZEROLEN", m.start(), "0-byte commit used to give a buffer back; use tx_cancel()")

    for m in re.finditer(r"(\w+)\.src\s*=\s*([A-Za-z_]\w*)\s*;", text):
        var, val = m.group(1), m.group(2)
        if re.search(r"(ENDPOINT|Endpoint|kEndpoint|_ADDR$)", val) and \
                re.search(rf"\breply\w*\(\s*{var}\b", text[m.end():m.end() + 600]):
            report("IPC-REPLYADDR", m.start(),
                   f"{var}.src = {val}: reply() sends to .src; put the host there and our endpoint in .dst")


def check_py(rel, text, out):
    def report(check, pos, msg):
        if not allowed(text, pos, check):
            out.append(f"{rel}:{line_of(text, pos)}: [{check}] {msg}")

    nonblocking = "O_NONBLOCK" in text or "set_blocking(" in text or "setblocking(False)" in text
    if nonblocking:
        for m in re.finditer(r"except\s+\(?\s*OSError[^:]*:\s*\n\s*break\b", text):
            before = text[max(0, m.start() - 400):m.start()]
            if "BlockingIOError" not in before and re.search(r"os\.write|hello\(|\.send", before):
                report("PY-NBBREAK", m.start(),
                       "a full ring (BlockingIOError is an OSError) ends this loop for good; retry it")
    for m in re.finditer(r"except\s+BlockingIOError\s*:\s*\n\s*(?:time\.sleep|continue|pass)", text):
        fn_start = text.rfind("\ndef ", 0, m.start())
        fn = text[fn_start:m.end()]
        if "while" in fn and not re.search(r"deadline|timeout", fn):
            report("PY-NBSPIN", m.start(), "retries BlockingIOError forever; add a deadline")
    if "/dev/uio" in text or "UioRegion" in text or "uio" in rel:
        for m in re.finditer(r"struct\.(?:unpack|pack)(?:_from|_into)?\([^)]*\b(?:mem|csr|mmio|regs)\b"
                             r"|\b(?:self\.)?(?:mem|csr)\[[^\]]*:[^\]]*\]\s*=?", text):
            if "dma[" in m.group(0):
                continue
            report("PY-MMIO", m.start(), "register access via mmap slice/struct; use memoryview(mm).cast('I')")
        for m in re.finditer(r"default\s*=\s*(?:Path\()?\"/dev/uio0\"", text):
            report("PY-UIO0", m.start(), "hardcoded /dev/uio0; find the device by /sys/class/uio/*/name")


def check_tests(src, out):
    # @verifies claims are checked repo-wide by `spectrace_matrix.py --check-verifies` ([SPEC-AUDIT-15]).
    for rel in src.files(TEST_GLOBS):
        text = src.read(rel)
        m = re.search(r"MAP_32BIT", text)
        guarded = "#ifdef MAP_32BIT" in text or "#if defined(MAP_32BIT)" in text
        if m and not guarded and not allowed(text, m.start(), "T-MAP32"):
            out.append(f"{rel}:{line_of(text, m.start())}: [T-MAP32] MAP_32BIT is x86-64 only; guard it")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--git-rev", default=None, help="audit the files at this git revision")
    args = ap.parse_args()
    src = Source(args.git_rev)
    out = []
    for rel in src.files(CPP_GLOBS):
        check_cpp(rel, src.read(rel), out)
    for rel in src.files(PY_GLOBS):
        check_py(rel, src.read(rel), out)
    check_tests(src, out)
    where = args.git_rev or "working tree"
    if out:
        print(f"[FAIL] {len(out)} IPC transport invariant violation(s) in {where}:")
        for v in out:
            print("  " + v)
        return 1
    print(f"[PASS] IPC transport invariants hold in {where}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
