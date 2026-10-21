#!/usr/bin/env python3
# @verifies [SPEC-AUDIT-01] [SPEC-AUDIT-02] [SPEC-AUDIT-03] [SPEC-AUDIT-04] [SPEC-AUDIT-05] [SPEC-AUDIT-14] docs/SPECTRACE.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Test Suite: Sashiko Multi-Stage Adversarial Invariants
------------------------------------------------------
Enforces the 5 Sashiko Invariant Gates on all firmware source files:
- Stage 1: Zero-Heap & Freestanding Environment (no malloc/new/heap STL)
- Stage 2: Non-Blocking HAL & Cooperative Lifecycle (no sleep/delay in coroutines)
- Stage 3: ISR Boundary & SPSC Dispatch Safety (no .resume() from ISR context)
- Stage 4: Endianness & Wire Framing Consistency (64B TLP alignment)
- Stage 5: Comprehensive CppUTest SITL coverage
"""

import contextlib
import io
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))
sys.path.insert(0, str(REPO_ROOT / "tools"))

from tools.run_adversarial_audit import (
    InvariantAuditor,
    collect_audit_files,
    emit_ai_review_prompt,
)


def test_module_scope_excludes_linux_and_generated_trees(tmp_path):
    maintained = tmp_path / "targets" / "allwinner_e906" / "src" / "driver.cpp"
    linux_host = tmp_path / "targets" / "linux" / "src" / "host.cpp"
    buildroot = tmp_path / "hw" / "zynq7000" / "bld.alinx-20" / "build" / "kernel.cpp"
    vendor = tmp_path / "third_party" / "vendor.hpp"
    for file_path in (maintained, linux_host, buildroot, vendor):
        file_path.parent.mkdir(parents=True, exist_ok=True)
        file_path.write_text("// test input\n", encoding="utf-8")

    selected = collect_audit_files(tmp_path, [tmp_path])
    assert maintained in selected
    assert linux_host not in selected
    assert buildroot not in selected
    assert vendor not in selected


def test_linux_scope_requires_explicit_opt_in(tmp_path):
    linux_source = tmp_path / "targets" / "linux" / "src" / "host.cpp"
    linux_source.parent.mkdir(parents=True)
    linux_source.write_text("// host code\n", encoding="utf-8")

    assert collect_audit_files(tmp_path, [tmp_path / "targets"]) == []
    assert collect_audit_files(
        tmp_path, [tmp_path / "targets" / "linux"], include_linux=True
    ) == [linux_source]


def test_ai_review_prompt_lists_only_selected_files(tmp_path, capsys):
    selected = tmp_path / "apps" / "sensor" / "SPECIFICATION.md"
    selected.parent.mkdir(parents=True)
    selected.write_text("# module spec\n", encoding="utf-8")

    emit_ai_review_prompt([selected], tmp_path, base_ref="origin/main")
    output = capsys.readouterr().out
    assert "apps/sensor/SPECIFICATION.md" in output
    assert "origin/main" in output
    assert "unrelated Linux host" in output


def test_auditor_folding_regions_are_balanced(repo_root):
    source = (repo_root / "tools" / "run_adversarial_audit.py").read_text(encoding="utf-8")
    assert source.count("# region ") == source.count("# endregion")
    for stage in range(6):
        assert f"# region Stage {stage}:" in source


def get_freestanding_sources(repo_root: Path):
    """Returns freestanding C++ source files subject to zero-heap rules."""
    target_files = []
    # Core public headers and reference applications
    for sub in ["include", "apps"]:
        d = repo_root / sub
        if d.exists():
            target_files.extend(d.rglob("*.hpp"))
            target_files.extend(d.rglob("*.cpp"))
            target_files.extend(d.rglob("*.h"))
            target_files.extend(d.rglob("*.c"))  # modabstractx.c was never audited ([SPEC-AUDIT-14])

    # Freestanding target BSPs (excluding hosted Linux host reactor)
    targets_dir = repo_root / "targets"
    if targets_dir.exists():
        for bsp in ["pico2w_rp2350", "esp32p4", "allwinner_e906"]:
            bsp_dir = targets_dir / bsp
            if bsp_dir.exists():
                target_files.extend(bsp_dir.rglob("*.hpp"))
                target_files.extend(bsp_dir.rglob("*.cpp"))
                target_files.extend(bsp_dir.rglob("*.h"))
                target_files.extend(bsp_dir.rglob("*.c"))

    return target_files


def test_stage1_zero_heap_invariant(repo_root):
    """Enforces zero-heap allocation across all freestanding code."""
    sources = get_freestanding_sources(repo_root)
    assert len(sources) > 0, "No freestanding C++ production sources found"

    auditor = InvariantAuditor(repo_root)
    for src in sources:
        try:
            with open(src, "r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
            auditor.audit_stage1_zero_heap(src, lines)
        except Exception as e:
            raise AssertionError(f"Failed to audit {src}: {e}")

    stage1_issues = auditor.violations["Stage 1 (Zero-Heap & Freestanding C++20)"]
    assert len(stage1_issues) == 0, f"Stage 1 Zero-Heap violations found: {stage1_issues}"


def test_stage2_non_blocking_hal_invariant(repo_root):
    """Enforces non-blocking awaitable HAL calls without thread sleeping."""
    sources = get_freestanding_sources(repo_root)
    auditor = InvariantAuditor(repo_root)
    for src in sources:
        try:
            with open(src, "r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
            auditor.audit_stage2_non_blocking_hal(src, lines)
        except Exception as e:
            raise AssertionError(f"Failed to audit {src}: {e}")

    stage2_issues = auditor.violations["Stage 2 (Non-Blocking HAL & Lifecycle)"]
    assert len(stage2_issues) == 0, f"Stage 2 Non-Blocking HAL violations found: {stage2_issues}"


def test_stage3_isr_boundary_safety(repo_root):
    """Enforces that coroutine .resume() is never called inside hardware ISRs."""
    sources = get_freestanding_sources(repo_root)
    auditor = InvariantAuditor(repo_root)
    for src in sources:
        try:
            with open(src, "r", encoding="utf-8", errors="ignore") as f:
                lines = f.readlines()
            auditor.audit_stage3_isr_boundary(src, lines)
        except Exception as e:
            raise AssertionError(f"Failed to audit {src}: {e}")

    stage3_issues = auditor.violations["Stage 3 (ISR Boundary & Dispatch Safety)"]
    assert len(stage3_issues) == 0, f"Stage 3 ISR dispatch violations found: {stage3_issues}"


def test_stage4_wire_framing_and_tlp_scaling_invariants(repo_root):
    """Enforces 64B wire alignment and flags unsegmented bulk transfers into fixed 64B TLPs."""
    auditor = InvariantAuditor(repo_root)
    # 1. Audit core TLP definition headers
    tlp_header = repo_root / "include" / "asp_tlp64.hpp"
    with open(tlp_header, "r", encoding="utf-8") as f:
        lines = f.readlines()
    auditor.audit_stage4_wire_framing_endianness(tlp_header, lines)
    assert len(auditor.violations["Stage 4 (Endianness & Wire Framing)"]) == 0

    # 2. Test that unsegmented payload > 40 bytes is caught as an architectural scaling violation
    mock_bad_code = [
        "void bad_transfer() {",
        "    Tlp64::make_raw(Channel::Debug, 0x01, large_buffer, 128); // Overrun 40B container",
        "}"
    ]
    mock_file = repo_root / "apps" / "mock_overflow.cpp"
    auditor.audit_stage4_wire_framing_endianness(mock_file, mock_bad_code)
    stage4_issues = auditor.violations["Stage 4 (Endianness & Wire Framing)"]
    assert len(stage4_issues) == 1
    assert "Fixed 64B TLP payload overflow" in stage4_issues[0]["rule"]


BAD_E906 = """
#define HW_UART_LSR (*(volatile unsigned *)0x1)
void tx(char c) {
    while (!(HW_UART_LSR & (1U << 5))) {
        __asm__ volatile("" : : : "memory");
    }
}
bool wait_flag() {
    unsigned timeout = 50000;
    while (!(HW_TWI1_CNTR & 8)) {
        if (--timeout == 0) return false;
        abstractx_repl_vm_hook();
    }
    return true;
}
void send(int c) { while ((ARM_MSG_STA_REG(c) & 7) >= 4) {} }
void stop() { while (HW_TWI1_CNTR & 16) ; }
void loop() { for (;;) { rpmsg.poll(); } }
namespace plic { inline unsigned claim() { return 0; } }
void uart_isr() noexcept {
}
"""

GOOD_E906 = """
void drain() {
    while (UART2_LSR & 1) {
        uint8_t ch = UART2_RBR & 0xFF;
        g_ring.push(ch);
    }
    while ((RV_MSG_STA_REG(c) & 7) > 0) {
        (void)RV_MSG_FIFO_REG(c);
    }
    // pre-scheduler bring-up: clocks settle before the dispatcher exists
    while (!(HW_CCU_PLL_STATUS & 1)) {}
    do { x(); } while (HW_FOO_STATUS & 1);
}
// PLIC was wrong here; the E906 uses the CLIC (comments are not code)
void timer_isr() noexcept {
}
void irq_enable() { hal::clic::enable(17); }
void after_doorbell() {
    if (take_doorbell()) {
        rpmsg.poll();  // irq-gated: only after the message box interrupt
    }
}
"""


def _audit_snippets(root: Path, files: dict, baseline: str = None):
    for rel, text in files.items():
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text, encoding="utf-8")
    if baseline is not None:
        (root / "tools").mkdir(exist_ok=True)
        (root / "tools" / "audit_baseline.txt").write_text(baseline, encoding="utf-8")
    auditor = InvariantAuditor(root)
    auditor.run_audit([root / rel for rel in files])
    found = [(i["file"], i["rule"].split(":", 1)[0], i["line"])
             for v in auditor.violations.values() for i in v if i["file"] in files]
    return auditor, found


def test_interrupt_and_polling_rules_on_fixtures(tmp_path):
    """[SPEC-AUDIT-14] each rule fires on the bad pattern and not on drains, comments or bring-up."""
    bad = "targets/allwinner_e906/src/bad.cpp"
    _, found = _audit_snippets(tmp_path, {bad: BAD_E906})
    rules = sorted((r, n) for f, r, n in found if f == bad)
    assert rules == [("DRIVER-POLL", 18), ("E906-NO-CLIC-ENABLE", 20), ("E906-PLIC", 19),
                     ("MMIO-SPIN", 4), ("MMIO-SPIN", 10), ("MMIO-SPIN", 16), ("MMIO-SPIN", 17)], rules

    with tempfile.TemporaryDirectory() as d:
        _, found = _audit_snippets(Path(d), {"targets/allwinner_e906/src/good.cpp": GOOD_E906})
        assert found == [], found

    with tempfile.TemporaryDirectory() as d:  # PLIC and CLIC-enable rules are E906-only
        _, found = _audit_snippets(Path(d), {"targets/pico2w_rp2350/src/p.cpp": BAD_E906})
        assert sorted({r for _, r, _ in found}) == ["DRIVER-POLL", "MMIO-SPIN"], found


def test_baseline_reports_known_and_fails_when_stale(tmp_path):
    """[SPEC-AUDIT-14] a listed violation is KNOWN; an entry that stopped matching fails."""
    rel = "apps/x/src/a.c"
    code = "void tx(void) {\n    while (!(HW_UART3_LSR & 32)) {\n    }\n}\n"
    entry = f"{rel} MMIO-SPIN while (!(HW_UART3_LSR & 32)) {{  # engineering_log test\n"
    auditor, found = _audit_snippets(tmp_path, {rel: code}, baseline=entry)
    assert found == [] and len(auditor.known) == 1 and auditor.stale_baseline() == []

    (tmp_path / rel).write_text("void tx(void) { }\n", encoding="utf-8")  # fixed, entry left behind
    auditor = InvariantAuditor(tmp_path)
    auditor.run_audit([tmp_path / rel])
    assert len(auditor.stale_baseline()) == 1
    with contextlib.redirect_stdout(io.StringIO()):
        assert auditor.print_report() == 1, "a stale baseline entry fails the audit"


def test_todays_e906_polling_is_found(repo_root):
    """[SPEC-AUDIT-14] the rules find the 2026-10-21 violations when the baseline is ignored."""
    auditor = InvariantAuditor(repo_root, use_baseline=False)
    auditor.run_audit(get_freestanding_sources(repo_root))
    found = {(i["file"], i["rule"].split(":", 1)[0]) for v in auditor.violations.values() for i in v}
    for expected in [("apps/e906_repl_app/src/modabstractx.c", "MMIO-SPIN"),
                     ("apps/e906_repl_app/src/main.cpp", "DRIVER-POLL"),
                     ("targets/allwinner_e906/src/irq_dispatcher.cpp", "E906-PLIC")]:
        if expected in found:
            continue
        # fixed: then its baseline entry must be gone too (checked by the full-suite test)
        assert not any(k[0] == expected[0] and k[1] == expected[1] for k in InvariantAuditor(repo_root).baseline), \
            f"{expected} no longer found but still baselined"


def test_open_requirement_is_not_a_traceability_miss(tmp_path):
    """[SPEC-AUDIT-12] a requirement marked 'Not implemented yet' is open, not missing."""
    spec = tmp_path / "apps" / "x" / "SPECIFICATION.md"
    spec.parent.mkdir(parents=True)
    spec.write_text("# X\n```mermaid\ngraph LR; a-->b\n```\n"
                    "### `[SPEC-X-01]` Open\n* **Requirement**: later.\n* **Not implemented yet**: `f`\n"
                    "### `[SPEC-X-02]` Promised\n* **Requirement**: now.\n", encoding="utf-8")
    auditor = InvariantAuditor(tmp_path)
    auditor.run_audit([spec])
    rules = [i["rule"] for i in auditor.violations["Stage 0 (Specification Markdown & SSOT)"]]
    assert any("SPEC-X-02" in r for r in rules), rules
    assert not any("SPEC-X-01" in r for r in rules), rules


def test_full_adversarial_audit_suite(repo_root):
    """Executes the full 5-stage adversarial audit pipeline."""
    sources = get_freestanding_sources(repo_root)
    auditor = InvariantAuditor(repo_root)
    auditor.run_audit(sources)
    total_issues = sum(len(v) for v in auditor.violations.values())
    assert total_issues == 0, f"Adversarial audit failed with {total_issues} violations: {auditor.violations}"
    assert auditor.stale_baseline() == [], auditor.stale_baseline()


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as td:
        test_module_scope_excludes_linux_and_generated_trees(Path(td))
    with tempfile.TemporaryDirectory() as td:
        test_linux_scope_requires_explicit_opt_in(Path(td))
    with tempfile.TemporaryDirectory() as td:
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            class CapsysMock:
                def readouterr(self):
                    class Res:
                        out = buf.getvalue()
                    return Res()
            test_ai_review_prompt_lists_only_selected_files(Path(td), CapsysMock())
    test_auditor_folding_regions_are_balanced(REPO_ROOT)
    test_stage1_zero_heap_invariant(REPO_ROOT)
    test_stage2_non_blocking_hal_invariant(REPO_ROOT)
    test_stage3_isr_boundary_safety(REPO_ROOT)
    test_stage4_wire_framing_and_tlp_scaling_invariants(REPO_ROOT)
    with tempfile.TemporaryDirectory() as td:
        test_interrupt_and_polling_rules_on_fixtures(Path(td))
    with tempfile.TemporaryDirectory() as td:
        test_baseline_reports_known_and_fails_when_stale(Path(td))
    test_todays_e906_polling_is_found(REPO_ROOT)
    with tempfile.TemporaryDirectory() as td:
        test_open_requirement_is_not_a_traceability_miss(Path(td))
    test_full_adversarial_audit_suite(REPO_ROOT)
    print("All adversarial invariant tests passed.")
