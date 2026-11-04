"""
Test Suite: Specification-to-Code Traceability (SSOT Standard)
--------------------------------------------------------------
Checks that every requirement in the Markdown specifications has an `@impl` tag.
Tag presence only: whether the code works is the spectrace_matrix verdict.
"""
# @verifies [SPEC-AUDIT-07] [SPEC-AUDIT-12] docs/SPECTRACE.md

from pathlib import Path
from tools.audit_specs import run_traceability_audit

def test_system_specification_traceability(repo_root, design_spec_path):
    """Proves 100% implementation coverage for docs/DESIGN_SPECIFICATION.md"""
    assert design_spec_path.exists(), f"SSOT specification missing: {design_spec_path}"
    
    results = run_traceability_audit(design_spec_path, repo_root)
    total = results["total"]
    implemented = results["implemented"]
    coverage = results["coverage"]
    specs = results["specs"]

    assert total > 0, "No specification requirements found in docs/DESIGN_SPECIFICATION.md"
    # [SPEC-AUDIT-07] a requirement may be committed before its code only when it says so
    untagged = [k for k, d in specs.items() if not d["implementations"] and not d["planned"]]
    assert not untagged, f"Untagged and not declared 'Not implemented yet': {untagged}"
    assert implemented + results["planned"] == total

    # Validate that every implementation file path actually exists
    for spec_id, data in specs.items():
        if data["planned"] and not data["implementations"]:
            continue
        for impl in data["implementations"]:
            rel_file = impl.split(":")[0]
            full_path = repo_root / rel_file
            assert full_path.exists(), f"Implementation file referenced by {spec_id} not found: {rel_file}"

def test_reference_application_specification_traceability(repo_root, app_spec_path):
    """Proves 100% implementation coverage for apps/gps_imu_app/SPECIFICATION.md"""
    assert app_spec_path.exists(), f"App specification missing: {app_spec_path}"

    results = run_traceability_audit(app_spec_path, repo_root)
    total = results["total"]
    implemented = results["implemented"]
    coverage = results["coverage"]

    assert total == 11, f"Expected 11 reference app specs, found {total}"
    assert implemented == 11, f"Expected 11 implemented reference app specs, found {implemented}"
    assert coverage == 100.0, f"Reference app coverage is {coverage}%, expected 100.0%"

def test_studio_specification_traceability(repo_root, studio_spec_path):
    """Proves 100% implementation coverage for tools/visualizer/SPECIFICATION.md"""
    assert studio_spec_path.exists(), f"Studio specification missing: {studio_spec_path}"

    results = run_traceability_audit(studio_spec_path, repo_root)
    total = results["total"]
    implemented = results["implemented"]
    coverage = results["coverage"]
    specs = results["specs"]

    assert total >= 14, f"Expected at least 14 studio specs, found {total}"
    assert implemented == total, f"{total - implemented} studio specs have no @impl tag"
    assert coverage == 100.0, f"Studio spec coverage is {coverage}%, expected 100.0%"

    # Validate that every implementation file path actually exists
    for spec_id, data in specs.items():
        assert len(data["implementations"]) > 0, f"Spec {spec_id} has no implementing files"
        for impl in data["implementations"]:
            rel_file = impl.split(":")[0]
            full_path = repo_root / rel_file
            assert full_path.exists(), f"Implementation file referenced by {spec_id} not found: {rel_file}"


def test_serial_loopback_specification_traceability(repo_root):
    """Proves 100% implementation coverage for apps/serial_loopback_app/SPECIFICATION.md"""
    spec_path = repo_root / "apps" / "serial_loopback_app" / "SPECIFICATION.md"
    assert spec_path.exists(), f"App specification missing: {spec_path}"

    results = run_traceability_audit(spec_path, repo_root)
    total = results["total"]
    implemented = results["implemented"]
    coverage = results["coverage"]

    assert total == 9, f"Expected 9 serial loopback specs, found {total}"
    assert implemented == 9, f"Expected 9 implemented serial loopback specs, found {implemented}"
    assert coverage == 100.0, f"Serial loopback coverage is {coverage}%, expected 100.0%"


def test_planned_requirement_needs_the_marker(tmp_path):
    """[SPEC-AUDIT-07] 2026-11-04: only a declared 'Not implemented yet' requirement may lack code."""
    spec = tmp_path / "SPEC.md"
    spec.write_text(
        "### `[SPEC-T-01]` Declared design\n* **Not implemented yet**: all of it.\n\n"
        "### `[SPEC-T-02]` Silently missing\n* **Requirement**: something.\n")
    r = run_traceability_audit(spec, tmp_path)
    assert r["specs"]["SPEC-T-01"]["planned"] == "all of it."
    assert r["specs"]["SPEC-T-02"]["planned"] is None
    assert r["planned"] == 1 and r["implemented"] == 0
