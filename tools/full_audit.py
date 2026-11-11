#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-AUDIT-17] docs/SPECTRACE.md (full audit)
"""
full_audit.py - run every automatic audit stage and write one report.

Each stage ends as PASS, FAIL, REPORT (a measurement with no threshold yet), SKIPPED (with the
reason) or MANUAL (a checklist for the reviewer). A stage that did not run is never PASS.
Exit code 1 when any stage FAILs.

  python3 tools/full_audit.py                       all stages, report in build_audit/
  python3 tools/full_audit.py --stages gate,trace   chosen stages
  python3 tools/full_audit.py --quick               gate --quick + trace + static
  python3 tools/full_audit.py --list                stage ids
"""

from __future__ import annotations

import argparse
import datetime as dt
import glob
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PASS, FAIL, REPORT, SKIPPED, MANUAL = "PASS", "FAIL", "REPORT", "SKIPPED", "MANUAL"
ANSI = re.compile(r"\x1b\[[0-9;]*m")


@dataclass
class Result:
    stage: str
    title: str
    status: str
    summary: str
    details: list[str] = field(default_factory=list)


def sh(cmd: list[str] | str, timeout: int, cwd: Path = ROOT) -> tuple[int, str]:
    """Run a command; a timeout is a failure (exit 124), never a pass."""
    try:
        p = subprocess.run(cmd, cwd=cwd, shell=isinstance(cmd, str), capture_output=True,
                           text=True, timeout=timeout)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except subprocess.TimeoutExpired as e:
        out = e.stdout.decode(errors="ignore") if isinstance(e.stdout, bytes) else (e.stdout or "")
        return 124, out + f"\n[timeout after {timeout} s]"


def tail(text: str, n: int = 8) -> list[str]:
    lines = [ANSI.sub("", ln).rstrip() for ln in text.splitlines() if ln.strip()]
    return lines[-n:]


def have(tool: str) -> str | None:
    return shutil.which(tool)


# --- automatic stages -----------------------------------------------------------------------

def stage_gate(args) -> Result:
    cmd = ["bash", "tools/gate.sh"] + (["--quick"] if args.quick else [])
    rc, out = sh(cmd, timeout=6 * 3600)
    lines = tail(out, 60)
    verdict = [ln for ln in lines if ln.startswith(("[PASS]", "[FAIL]"))]
    failed = [ln.strip() for ln in lines if re.match(r"\s+\d+ - ", ln)]
    summary = (verdict[-1] if verdict else f"exit {rc}") + (f" ({'; '.join(failed)})" if failed else "")
    return Result("gate", "Gate: host plain / ASan+UBSan / TSan (build error stops the run)",
                  PASS if rc == 0 else FAIL, summary,
                  [ln for ln in lines if ln.startswith(("==", "[", "   ")) or re.match(r"\s+\d+ - ", ln)])


TRACE_CHECKS = [
    ["python3", "tools/audit_specs.py"],
    ["python3", "tools/spectrace_matrix.py", "--check-skills"],
    ["python3", "tools/spectrace_matrix.py", "--check-verifies"],
    ["python3", "tools/audit_coro_contracts.py"],
    ["python3", "tools/audit_boundaries.py"],
    ["python3", "tools/audit_interfaces.py"],
    ["python3", "tools/audit_hal_invariants.py"],
    ["python3", "tools/audit_ipc_invariants.py"],
    ["python3", "tools/boundary_review.py", "--check"],
]


def stage_trace(args) -> Result:
    failed, details = [], []
    for cmd in TRACE_CHECKS:
        name = " ".join(cmd[1:])
        if not (ROOT / cmd[1]).exists():
            details.append(f"SKIPPED {name}: tool missing")
            continue
        rc, out = sh(cmd, timeout=900)
        details.append(f"{'ok  ' if rc == 0 else 'FAIL'} {name}: {(tail(out, 1) or [''])[0]}")
        if rc != 0:
            failed.append(name)
    return Result("trace", "Traceability and contract audits", FAIL if failed else PASS,
                  f"{len(TRACE_CHECKS) - len(failed)}/{len(TRACE_CHECKS)} pass"
                  + (f"; failing: {', '.join(failed)}" if failed else ""), details)


def stage_static(args) -> Result:
    details, status, parts = [], PASS, []
    clang_tidy = have("clang-tidy")
    cppcheck = have("cppcheck")
    if clang_tidy:
        rc, out = sh(["python3", "tools/run_static_analysis.py"], timeout=3600)
        line = (tail(out, 1) or [""])[0]
        details.append(f"clang-tidy (shrink-only baseline): exit {rc}: {line}")
        parts.append(f"clang-tidy: {line}")
        if rc != 0:
            status = FAIL
    else:
        details.append("SKIPPED required clang-tidy analysis: not installed")
        parts.append("clang-tidy not installed")
        status = SKIPPED
    if cppcheck:
        rc, out = sh([cppcheck, "--quiet", "--enable=warning,performance,portability",
                      "--std=c++20", "--inline-suppr", "-I", "include",
                      "include", "src", "hosts"], timeout=1800)
        found = [ln for ln in out.splitlines()
                 if re.search(r":\d+:\d+: (warning|error|performance|portability)", ln)]
        details.append(f"cppcheck (advisory, no baseline yet): exit {rc}, {len(found)} finding(s)")
        details += found[:15]
        parts.append(f"cppcheck {len(found)} (advisory)")
    else:
        details.append("SKIPPED cppcheck: not installed")
    return Result("static", "Static analysis (clang-tidy gate, cppcheck advisory)", status,
                  "; ".join(parts) or "no analyzer installed", details)


def matching_gcov(build_dir: Path) -> str:
    """gcov of the same major version as the build's C++ compiler. A GCC 14 build read by the
    default gcov 13 makes gcov exit 3 and gcovr fail (2026-11-05)."""
    cache = build_dir / "CMakeCache.txt"
    m = re.search(r"^CMAKE_CXX_COMPILER:\w+=(.+)$", cache.read_text(), re.M) if cache.exists() else None
    if m:
        rc, ver = sh([m.group(1).strip(), "-dumpversion"], timeout=60)
        major = ver.strip().split(".")[0]
        if rc == 0 and major.isdigit() and have(f"gcov-{major}"):
            return f"gcov-{major}"
    return "gcov"


def stage_coverage(args) -> Result:
    title = "Coverage of the host suite (core, hosts, Linux target)"
    if not have("gcovr"):
        return Result("coverage", title, SKIPPED, "gcovr not installed")
    d, jobs = ROOT / "build_cov", f"-j{os.cpu_count() or 4}"
    rc, out = sh(["cmake", "-S", ".", "-B", str(d), "-DABSTRACTX_TARGET=host",
                  "-DCMAKE_CXX_FLAGS=--coverage -fprofile-update=atomic -O0",
                  "-DCMAKE_EXE_LINKER_FLAGS=--coverage"], timeout=900)
    if rc == 0:
        rc, out = sh(["cmake", "--build", str(d), jobs], timeout=3600)
    if rc != 0:
        return Result("coverage", title, FAIL, "coverage build failed", tail(out))
    # Atomic counters: the host tests run threads; plain counters race and gcov then reports
    # negative hits (GCC PR 68080) that make gcovr abort (2026-11-05).
    sh(["find", str(d), "-name", "*.gcda", "-delete"], timeout=300)  # no counts from older builds
    sh(["ctest", "--test-dir", str(d), jobs, "--timeout", "300"], timeout=3600)
    # cwd = build dir: gcov writes its *.gcov intermediates in the working directory, never the repo root.
    rc, out = sh(["gcovr", "-r", str(ROOT), str(d), "--gcov-executable", matching_gcov(d),
                  "--filter", str(ROOT / "include") + "/", "--filter", str(ROOT / "src") + "/",
                  "--filter", str(ROOT / "hosts") + "/", "--filter", str(ROOT / "targets/linux") + "/",
                  "--gcov-ignore-errors=all", "--sort", "uncovered-percent", "--txt", "-", "--txt-summary"],
                 timeout=1800, cwd=d)
    if rc != 0:
        return Result("coverage", title, FAIL, "gcovr failed", tail(out))
    summary = " ".join(ln.strip() for ln in out.splitlines() if ln.startswith(("lines:", "functions:", "branches:")))
    worst = [ln for ln in out.splitlines() if re.search(r"\s\d+%\s", ln)][:20]
    return Result("coverage", title, REPORT, summary or "see details", ["least covered files first:"] + worst)


def stage_mutation(args) -> Result:
    title = "Mutation testing (do the tests fail when the code is wrong?)"
    runner = next((t for t in ("mull-runner-18", "mull-runner-17", "mull-runner") if have(t)), None)
    if not runner:
        return Result("mutation", title, SKIPPED,
                      "Mull not installed (github.com/mull-project/mull, package mull-18); "
                      "until then the strength of the tests is not measured")
    return Result("mutation", title, MANUAL, f"{runner} found; run per the full-audit skill")


def stage_fuzz(args) -> Result:
    title = "Fuzzing of parsers (TLP, RPMsg, CTF)"
    targets = sorted(glob.glob(str(ROOT / "fuzz" / "*.cpp")))
    if not targets:
        return Result("fuzz", title, SKIPPED, "no fuzz targets in fuzz/ yet (clang -fsanitize=fuzzer is "
                      f"{'available' if have('clang++') else 'not installed'})")
    return Result("fuzz", title, MANUAL, f"{len(targets)} target(s); run per the full-audit skill")


def stage_e906(args) -> Result:
    title = "E906 cross-build, layout and memory"
    tc = sorted(glob.glob(os.path.expanduser("~/.tools/xpack-riscv-none-elf-gcc-*/bin/riscv-none-elf-readelf")))
    if not tc:
        return Result("e906", title, SKIPPED, "riscv-none-elf toolchain not in ~/.tools")
    d = ROOT / "build_audit" / "e906"
    rc, out = sh(["cmake", "-S", ".", "-B", str(d), "-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/riscv-none-elf.cmake",
                  "-DABSTRACTX_TARGET=e906", "-DCMAKE_BUILD_TYPE=Release"], timeout=900)
    if rc == 0:
        rc, out = sh(["cmake", "--build", str(d), f"-j{os.cpu_count() or 4}"], timeout=3600)
    if rc != 0:
        return Result("e906", title, FAIL, "build failed", tail(out))
    details, failed = [], False
    for elf in sorted(glob.glob(str(d / "apps" / "*" / "*"))):
        name = Path(elf).name
        if not (os.access(elf, os.X_OK) and Path(elf).parent.name == name):
            continue
        rc, out = sh(["python3", "tools/check_e906_layout.py", "--readelf", tc[-1], elf], timeout=120)
        _, mem = sh(["bash", "tools/analyze_elf_size.sh", elf, "targets/allwinner_e906/bsp/e906_ddr.ld"], timeout=300)
        # row: SRAM_FAST origin cap KB used KB free KB pct
        row = next((ANSI.sub("", ln).split() for ln in mem.splitlines()
                    if ANSI.sub("", ln).strip().startswith("SRAM_FAST ")), None)
        use = f"SRAM_FAST {row[4]} / {row[2]} KB ({row[8]})" if row and len(row) >= 9 else "SRAM_FAST ?"
        details.append(f"{'ok  ' if rc == 0 else 'FAIL'} {name}: layout {(tail(out, 1) or [''])[0]}; {use}")
        failed |= rc != 0
    return Result("e906", title, FAIL if failed else PASS, f"{len(details)} firmware image(s)", details)


def stage_ci(args) -> Result:
    title = "CI status on main (last 5 runs)"
    if not have("gh"):
        return Result("ci", title, SKIPPED, "gh CLI not installed")
    rc, out = sh(["gh", "run", "list", "-L", "5", "--branch", "main", "--json", "conclusion,headSha,createdAt",
                  "-q", '.[] | "\\(.createdAt[0:16]) \\(.conclusion) \\(.headSha[0:7])"'], timeout=60)
    if rc != 0:
        return Result("ci", title, SKIPPED, "gh cannot read runs (not logged in?)")
    runs = [ln for ln in out.splitlines() if ln.strip()]
    if not runs:
        return Result("ci", title, SKIPPED, "no runs")
    return Result("ci", title, PASS if " success " in runs[0] + " " else FAIL, runs[0], runs)


# OSes AbstractX means to run on ([SPEC-ARCH-15]): host directory, or the code that is its own host
PLATFORM_HOSTS = {
    "FreeRTOS": "hosts/freertos",
    "Zephyr": "hosts/zephyr",
    "ChibiOS": "hosts/chibios",
    "Linux (Boost.Asio)": "hosts/asio",
    "bare metal (wfi loop)": "src/runtime.cpp",
}


def stage_hosts(args) -> Result:
    present = [o for o, rel in PLATFORM_HOSTS.items() if (ROOT / rel).exists()]
    missing = [o for o in PLATFORM_HOSTS if o not in present]
    details = [f"{'present' if o in present else 'missing'}  {o}: {PLATFORM_HOSTS[o]}" for o in PLATFORM_HOSTS]
    return Result("hosts", "OS hosts that exist (host contract [SPEC-ARCH-15])", REPORT,
                  f"{len(present)}/{len(PLATFORM_HOSTS)} present"
                  + (f"; missing: {', '.join(missing)}" if missing else ""), details)


# Vendor / OS calls that block the calling context. In a driver behind an AbstractX awaiter they
# stall step() and every coroutine ([SPEC-ARCH-15], AGENTS §4). Mark boot-only uses "boot-only".
BLOCKING_CALLS = [
    # pico-sdk
    r"\b(spi|i2c|uart|pio_sm)_\w*_blocking\s*\(", r"\bdma_channel_wait_for_finish_blocking\s*\(",
    r"\bsleep_(ms|us)\s*\(", r"\bbusy_wait_(ms|us)\w*\s*\(", r"\buart_getc\s*\(", r"\buart_putc\w*\s*\(",
    r"\bcyw43_arch_wifi_connect_(blocking|timeout_ms)\s*\(",
    # ESP-IDF
    r"\bspi_device_(transmit|polling_transmit)\s*\(", r"\bi2c_master_(transmit|receive|transmit_receive)\s*\(",
    r"\buart_read_bytes\s*\(", r"\bvTaskDelay\s*\(",
    # POSIX / Linux (blocking unless the fd is non-blocking or driven by an event loop)
    r"\busleep\s*\(", r"\bnanosleep\s*\(", r"std::this_thread::sleep_for",
    r"\bgpiod_line_request_wait_edge_events\s*\(", r"\bgpiod_line_event_wait\s*\(",
    # AbstractX's own boot-time delays ([SPEC-HAL-05]: boot / reset only)
    r"\bTimer::delay_(us|ms|ticks)\s*\(", r"\.delay_(us|ms)\s*\(",
]


def stage_drivers(args) -> Result:
    pat = re.compile("|".join(BLOCKING_CALLS))
    hits = []
    for base in ("targets", "hosts", "applib", "include", "src", "apps"):
        for p in sorted((ROOT / base).rglob("*")):
            if p.suffix not in (".c", ".cpp", ".h", ".hpp") or any(x in p.parts for x in ("third_party", "build")):
                continue
            for i, line in enumerate(p.read_text(errors="ignore").splitlines(), 1):
                code = line.split("//")[0]
                if pat.search(code) and "boot-only" not in line:
                    hits.append(f"{p.relative_to(ROOT)}:{i}: {code.strip()[:110]}")
    per_dir: dict[str, int] = {}
    for h in hits:
        k = "/".join(h.split("/")[:2])
        per_dir[k] = per_dir.get(k, 0) + 1
    return Result("drivers", "Blocking vendor / OS calls in first-party code (mark boot-only uses)", REPORT,
                  f"{len(hits)} call(s): " + ", ".join(f"{k} {v}" for k, v in sorted(per_dir.items())),
                  hits[:60] + ([f"... {len(hits) - 60} more"] if len(hits) > 60 else []))


AUTO_STAGES = {
    "gate": stage_gate, "trace": stage_trace, "static": stage_static, "coverage": stage_coverage,
    "mutation": stage_mutation, "fuzz": stage_fuzz, "e906": stage_e906, "ci": stage_ci,
    "hosts": stage_hosts, "drivers": stage_drivers,
}
QUICK_STAGES = ["gate", "trace", "static"]


# --- manual checklists ----------------------------------------------------------------------

MANUAL_CHECKLISTS = {
    "review": ("Independent review of the change or module",
               ["give reviewers the code/diff and the requirements, not your suspicions",
                "each finding: failing test, lint rule or recorded hardware fact, else it stays a candidate",
                "re-check open items of the previous review in docs/reviews/"]),
    "concurrency": ("Concurrency protocols (wake/notify, dispatcher queue, rings)",
                    ["boundary-race-review checklist for every ISR / cross-core / cross-thread change",
                     "stress test under TSan with randomised scheduling for each handshake",
                     "model check (TLA+ / similar) for wake protocols: planned, tool not set up"]),
    "buses": ("Bus and device APIs are await-efficient (SPI, I2C, UART, GPIO / gpiod, Wi-Fi)",
              ["per target and bus: completion comes from an interrupt or DMA, never polling or a blocking "
               "vendor call (see the drivers stage)",
               "per transaction: interrupts taken, copies made ([SPEC-ARCH-08]), latency from completion to resume",
               "vendor SDKs that block (pico-sdk *_blocking, CYW43 bus DMA wait, ESP-IDF transmit, gpiod wait): "
               "replaced, moved off the AbstractX core, or documented as boot-only",
               "Pico 2 W Wi-Fi: what the CYW43 driver blocks on, on which core, and its cost to AbstractX",
               "same API on every target: an application moves between targets with no #ifdef"]),
    "gui": ("Studio GUI behaviour",
            ["tests that click (Dear ImGui Test Engine): tabs, Pop to Canvas / Pop In, View menu, connect",
             "today only frame-render smoke tests exist"]),
    "board": ("Board and hardware claims",
              ["e906-board-selftest and bench runs for every requirement verified on hardware",
               "a board result quoted in the log must name the commit it ran on"]),
    "platforms": ("Hosting on other OSes and competing with them (FreeRTOS, Zephyr, ChibiOS, Linux)",
                  ["per OS: its native event loop and async model (FreeRTOS notifications/queues/timers; "
                   "Zephyr k_poll, k_work, k_timer; ChibiOS events, virtual timers, OSAL; Linux epoll/io_uring, "
                   "Boost.Asio awaitables)",
                   "per OS: how the host contract maps onto it (step() deadline + wake) without a second loop, "
                   "and which of its async/coroutine runtimes must never mix with AbstractX coroutines "
                   "([SPEC-ARCH-15]; Boost.Asio was missed until 2026-11-04)",
                   "per OS: where its drivers' completions enter AbstractX (ISR, callback, completion handler) "
                   "and who owns timers, the tick hook and interrupt priorities",
                   "services: timeouts on every wait, multi-object waits, priorities / preemption, "
                   "work queues, init order, power management, logging and tracing, latency measurement; "
                   "each: AbstractX better / equivalent / gap / not needed (why)",
                   "port cost: what an application changes to move between these OSes (target: nothing)"]),
}


def write_report(results: list[Result], path: Path, head: str) -> None:
    lines = [f"# AbstractX full audit ({dt.date.today().isoformat()}, `{head}`)", "",
             "Statuses: PASS, FAIL, REPORT (measured, no threshold yet), SKIPPED (did not run, reason given), "
             "MANUAL (reviewer checklist). A stage that did not run is never PASS.", "",
             "| Stage | Status | Summary |", "| --- | --- | --- |"]
    for r in results:
        lines.append(f"| {r.title} | **{r.status}** | {r.summary.replace('|', '/')} |")
    for r in results:
        lines += ["", f"## {r.title}: {r.status}", ""]
        lines += [f"- {d}" for d in r.details] or ["- (no details)"]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--stages", help="comma-separated stage ids (default: all)")
    ap.add_argument("--quick", action="store_true", help="gate --quick + trace + static")
    ap.add_argument("--out", type=Path, help="report path (default build_audit/audit-<date>.md)")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()

    if args.list:
        for k in AUTO_STAGES:
            print(f"{k:12s} automatic")
        for k, (t, _) in MANUAL_CHECKLISTS.items():
            print(f"{k:12s} manual: {t}")
        return 0

    chosen = (args.stages.split(",") if args.stages else QUICK_STAGES if args.quick
              else list(AUTO_STAGES) + list(MANUAL_CHECKLISTS))
    unknown = [s for s in chosen if s not in AUTO_STAGES and s not in MANUAL_CHECKLISTS]
    if unknown:
        print(f"unknown stage(s): {', '.join(unknown)}", file=sys.stderr)
        return 2

    results = []
    for s in chosen:
        if s in AUTO_STAGES:
            print(f"== {s} ...", flush=True)
            r = AUTO_STAGES[s](args)
        else:
            title, items = MANUAL_CHECKLISTS[s]
            r = Result(s, title, MANUAL, "reviewer answers the checklist in the review document", items)
        print(f"   {r.status}: {r.summary}", flush=True)
        results.append(r)

    _, head = sh(["git", "rev-parse", "--short", "HEAD"], timeout=30)
    out = args.out or ROOT / "build_audit" / f"audit-{dt.date.today().isoformat()}.md"
    write_report(results, out, head.strip())
    print(f"report: {out}")
    return 1 if any(r.status == FAIL for r in results) else 0


if __name__ == "__main__":
    sys.exit(main())
