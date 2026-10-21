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
    assert implemented == total, f"Missing implementations: {total - implemented} untagged specs"
    assert coverage == 100.0, f"Specification coverage is {coverage}%, expected 100.0%"

    # Validate that every implementation file path actually exists
    for spec_id, data in specs.items():
        assert len(data["implementations"]) > 0, f"Spec {spec_id} has no implementing files"
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

    assert total == 10, f"Expected 10 reference app specs, found {total}"
    assert implemented == 10, f"Expected 10 implemented reference app specs, found {implemented}"
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
