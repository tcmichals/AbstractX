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

import pytest
from pathlib import Path
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

    # Freestanding target BSPs (excluding hosted Linux host reactor)
    targets_dir = repo_root / "targets"
    if targets_dir.exists():
        for bsp in ["pico2w_rp2350", "esp32p4", "allwinner_e906"]:
            bsp_dir = targets_dir / bsp
            if bsp_dir.exists():
                target_files.extend(bsp_dir.rglob("*.hpp"))
                target_files.extend(bsp_dir.rglob("*.cpp"))
                target_files.extend(bsp_dir.rglob("*.h"))

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
            pytest.fail(f"Failed to audit {src}: {e}")

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
            pytest.fail(f"Failed to audit {src}: {e}")

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
            pytest.fail(f"Failed to audit {src}: {e}")

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

def test_full_adversarial_audit_suite(repo_root):
    """Executes the full 5-stage adversarial audit pipeline."""
    sources = get_freestanding_sources(repo_root)
    auditor = InvariantAuditor(repo_root)
    auditor.run_audit(sources)
    total_issues = sum(len(v) for v in auditor.violations.values())
    assert total_issues == 0, f"Adversarial audit failed with {total_issues} violations: {auditor.violations}"

