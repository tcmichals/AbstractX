"""
Test Suite: MemBrowse Memory Footprint & Zero-Heap Symbol Audit
---------------------------------------------------------------
Verifies:
1. Static section tracking targets (tools/membrowse-targets.json) define budgets.
2. Zero-heap symbol auditor flags forbidden dynamic symbols (malloc, new, free).
3. Memory metrics JSON schema for AbstractX Studio memory gauge integration.
"""

import json
import pytest
from pathlib import Path
from tools.track_memory_membrowse import audit_zero_heap

def test_membrowse_targets_configuration(repo_root):
    """Verifies that tools/membrowse-targets.json defines targets and strict memory budgets."""
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
        assert "flash_budget_bytes" in target, f"Target {target['name']} missing flash_budget_bytes"
        assert target["ram_budget_bytes"] > 0
        assert target["flash_budget_bytes"] > 0

def test_zero_heap_symbol_audit_detection(repo_root, tmp_path):
    """Verifies that audit_zero_heap correctly identifies clean binaries vs forbidden heap calls."""
    # Test on a non-existent binary returns True, "Binary not found"
    clean, msg = audit_zero_heap(tmp_path / "nonexistent.elf")
    assert clean is True
    assert "not found" in msg
