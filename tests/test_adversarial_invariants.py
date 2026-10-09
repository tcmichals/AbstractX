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
from tools.run_adversarial_audit import InvariantAuditor

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

def test_full_adversarial_audit_suite(repo_root):
    """Executes the full 5-stage adversarial audit pipeline."""
    sources = get_freestanding_sources(repo_root)
    auditor = InvariantAuditor(repo_root)
    auditor.run_audit(sources)
    total_issues = sum(len(v) for v in auditor.violations.values())
    assert total_issues == 0, f"Adversarial audit failed with {total_issues} violations: {auditor.violations}"
