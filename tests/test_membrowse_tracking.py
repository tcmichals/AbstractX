"""
Test Suite: MemBrowse Memory Footprint & Zero-Heap Symbol Audit
---------------------------------------------------------------
Verifies:
1. Static section tracking targets (tools/membrowse-targets.json) define budgets.
2. Zero-heap symbol auditor flags forbidden dynamic symbols (malloc, new, free).
3. Memory metrics JSON schema for AbstractX Studio memory gauge integration.
"""

import json
import shutil
import subprocess
import pytest
from pathlib import Path
from tools.track_memory_membrowse import audit_zero_heap, main

def test_membrowse_targets_configuration(repo_root):
    """
    # @impl [SPEC-STUDIO-10] tools/visualizer/SPECIFICATION.md
    Verifies that tools/membrowse-targets.json defines targets and strict memory budgets.
    """
    config_path = repo_root / "tools" / "membrowse-targets.json"
    assert config_path.exists(), f"Configuration file missing: {config_path}"

    with open(config_path, "r", encoding="utf-8") as f:
        config = json.load(f)

    assert "targets" in config, "Config missing 'targets' list"
    assert len(config["targets"]) > 0, "No targets defined in membrowse-targets.json"

    target_names = [t["name"] for t in config["targets"]]
    assert "pico2w_flight" in target_names or any("pico" in name for name in target_names)

    for target in config["targets"]:
        assert "ram_budget_bytes" in target, f"Target {target['name']} missing ram_budget_bytes"
        assert target["ram_budget_bytes"] > 0
        if target.get("memory_model") != "sram_only":
            assert "flash_budget_bytes" in target, f"Target {target['name']} missing flash_budget_bytes"
            assert target["flash_budget_bytes"] > 0

# @verifies [SPEC-AUDIT-19] docs/SPECTRACE.md
def _object_defining(tmp_path, body: str) -> Path:
    """A relocatable object whose symbol table *defines* the given function (as a static newlib does)."""
    cc = shutil.which("cc") or shutil.which("gcc")
    if not cc:
        pytest.skip("no C compiler to build the test object")
    src = tmp_path / "heap.c"
    src.write_text(body)
    obj = tmp_path / "heap.o"
    subprocess.run([cc, "-c", "-o", str(obj), str(src)], check=True)
    return obj


def test_zero_heap_missing_elf_fails(tmp_path):
    """A missing ELF is not a pass (the old check returned True, "Binary not found")."""
    ok, msg = audit_zero_heap(tmp_path / "nonexistent.elf")
    assert ok is False
    assert "not found" in msg


def test_zero_heap_flags_defined_malloc(tmp_path):
    """EL-1027-31: newlib's _malloc_r is defined in the firmware, so `nm -u` never listed it."""
    obj = _object_defining(tmp_path, "void *_malloc_r(void *r, unsigned long n) { (void)r; (void)n; return 0; }\n")
    ok, msg = audit_zero_heap(obj)
    assert ok is False
    assert "_malloc_r" in msg
    # The old check (`nm -u`, substring match) passed this object.
    undefined = subprocess.run(["nm", "-u", str(obj)], stdout=subprocess.PIPE, text=True, check=True).stdout
    assert "_malloc_r" not in undefined


def test_zero_heap_flags_operator_new_and_passes_clean(tmp_path):
    obj = _object_defining(tmp_path, "void _Znwj(void) {}\nvoid _ZdlPvj(void) {}\nint freelist_count(void) { return 0; }\n")
    ok, msg = audit_zero_heap(obj)
    assert ok is False and "_Znwj" in msg and "freelist_count" not in msg
    assert "_ZdlPvj" not in msg  # a trapping operator delete cannot allocate ([SPEC-E906-MEM-03])
    clean = _object_defining(tmp_path, "int freelist_count(void) { return 0; }\nint main(void) { return 0; }\n")
    ok, msg = audit_zero_heap(clean)
    assert ok is True, msg


def test_zero_heap_elf_mode_exit_code(tmp_path):
    """--elf exits non-zero on a violation or a missing file and 0 on a clean object."""
    bad = _object_defining(tmp_path, "void _sbrk(void) {}\n")
    assert main(["--elf", str(bad)]) == 1
    assert main(["--elf", str(tmp_path / "missing.elf")]) == 1
    good_dir = tmp_path / "good"
    good_dir.mkdir()
    good = _object_defining(good_dir, "int f(void) { return 1; }\n")
    assert main(["--elf", str(good)]) == 0
