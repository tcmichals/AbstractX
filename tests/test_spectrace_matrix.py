#!/usr/bin/env python3
# @verifies [SPEC-AUDIT-06] [SPEC-AUDIT-07] [SPEC-AUDIT-08] [SPEC-AUDIT-10] docs/SPECTRACE.md
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
Tests for tools/spectrace_matrix.py.
Builds a throwaway git repository with known requirements, skills and code and
checks the matrix verdicts and the change review. Runs with plain python3 or pytest.
"""

import contextlib
import io
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import spectrace_matrix as sm  # noqa: E402

SPEC = """# Fixture Specification

### `[SPEC-FIX-01]` Verified requirement
* **Requirement**: does a thing. A sentence mentioning `* **Verified by**: `not-a-skill`` is prose.
* **Verified by**: `host-tests` (`csrw 0x7c1` with `0x1031`, `sim/x.cpp`)

### `[SPEC-FIX-02]` Implemented, no verifying skill
* **Requirement**: does another thing.

### `[SPEC-FIX-03]` Names a skill that does not exist
* **Verified by**: `no-such-skill`

### `[SPEC-FIX-04]` Specified only
* **Verified by**: `host-tests`

### `[SPEC-FIX-05]` Partial implementation
* **Verified by**: `host-tests`
"""

IMPL = "@" + "impl"  # keep fixture tags out of the real repository scan

A_CPP = """// header
// {IMPL} [SPEC-FIX-01] docs/FIX_SPECIFICATION.md
int one() {
    return 1;
}

// {IMPL} [SPEC-FIX-02] docs/FIX_SPECIFICATION.md
int two() {
    return 2;
}
""".replace("{IMPL}", IMPL)

B_CPP = """// {IMPL} [SPEC-FIX-03] [SPEC-FIX-05] docs/FIX_SPECIFICATION.md
// @status Partial
int three() { return 3; }
""".replace("{IMPL}", IMPL)


def run(cmd, cwd):
    subprocess.run(cmd, cwd=cwd, check=True, capture_output=True)


def make_repo(root: Path) -> None:
    (root / "docs").mkdir()
    (root / "src").mkdir()
    (root / ".agents" / "skills").mkdir(parents=True)
    (root / "docs" / "FIX_SPECIFICATION.md").write_text(SPEC)
    (root / ".agents" / "skills" / "host-tests.md").write_text("---\nname: host-tests\n---\n")
    (root / "src" / "a.cpp").write_text(A_CPP)
    (root / "src" / "b.cpp").write_text(B_CPP)
    (root / "src" / "t_test.cpp").write_text("// @" + "verifies [SPEC-FIX-04]\nint t() { return 0; }\n")
    run(["git", "init", "-q"], root)
    run(["git", "add", "-A"], root)
    run(["git", "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-q", "-m", "fixture"], root)


def review(root: Path, strict: bool = False, prompt: bool = False):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = sm.review_changes(root, "", prompt, strict)
    return rc, out.getvalue()


def test_matrix_verdicts():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        make_repo(root)
        reqs, skills, _ = sm.build_matrix(root)
        known = set(skills)
        verdicts = {tag: r.verdict(known) for tag, r in reqs.items()}
        assert verdicts == {
            "SPEC-FIX-01": "VERIFIED",
            "SPEC-FIX-02": "IMPLEMENTED",
            "SPEC-FIX-03": "BROKEN",
            "SPEC-FIX-04": "SPEC-ONLY",
            "SPEC-FIX-05": "PARTIAL",
        }, verdicts
        assert reqs["SPEC-FIX-01"].skills == ["host-tests"], "a mention inside prose is not a Verified-by line"
        assert verdicts["SPEC-FIX-04"] == "SPEC-ONLY", "@verifies is not an implementation"
        with contextlib.redirect_stdout(io.StringIO()):
            assert sm.print_matrix(reqs, skills, root / "m.md") == 1, "not complete -> non-zero"
        text = (root / "m.md").read_text()
        assert "1/5" in text and "| `SPEC-FIX-03` |" in text


def test_change_review_maps_hunks_and_flags_untraced():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        make_repo(root)
        a = root / "src" / "a.cpp"
        a.write_text(A_CPP.replace("return 2;", "return 22;"))  # governed by SPEC-FIX-02
        (root / "src" / "new.cpp").write_text("int untagged() { return 0; }\n")
        spec = root / "docs" / "FIX_SPECIFICATION.md"
        spec.write_text(SPEC.replace("does a thing.", "does a thing, faster."))

        rc, out = review(root)
        assert rc == 0
        assert "SPEC-FIX-02 Implemented, no verifying skill  <- src/a.cpp:9" in out, out
        assert "[UNTRACED] src/new.cpp:1" in out, out
        (root / "src" / "t_test.cpp").write_text("// @" + "verifies [SPEC-FIX-04]\nint t() { return 1; }\n")
        _, out2 = review(root)
        assert "SPEC-FIX-04 Specified only  <- src/t_test.cpp:2" in out2, "test change maps to what it verifies"
        assert "[SPEC CHANGED] SPEC-FIX-01" in out and "re-check src/a.cpp:2" in out, out

        rc, _ = review(root, strict=True)
        assert rc == 1, "--strict fails on untraced hunks"

        # --staged only sees what is staged: the untracked file is not reviewed
        out_io = io.StringIO()
        with contextlib.redirect_stdout(out_io):
            rc = sm.review_changes(root, "", False, True, staged=True)
        assert rc == 0 and "UNTRACED" not in out_io.getvalue(), "nothing staged -> nothing untraced"
        run(["git", "add", "src/new.cpp"], root)
        out_io = io.StringIO()
        with contextlib.redirect_stdout(out_io):
            rc = sm.review_changes(root, "", False, True, staged=True)
        assert rc == 1 and "[UNTRACED] src/new.cpp:1" in out_io.getvalue(), "staged untagged file blocks the commit"

        _, bundle = review(root, prompt=True)
        assert "## SPEC-FIX-02" in bundle and "does another thing." in bundle, "requirement text retrieved"
        assert "+    return 22;" in bundle, "diff included"


def test_stale_and_failing_verdicts():
    from trac_lib import ledger
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        make_repo(root)
        reqs, skills, _ = sm.build_matrix(root)
        assert reqs["SPEC-FIX-01"].verdict(set(skills)) == "VERIFIED", "baseline without ledger"

        commit = subprocess.check_output(["git", "rev-parse", "--short=12", "HEAD"], cwd=root, text=True).strip()
        rec_fail = {
            "check": "ctest:debug", "commit": commit, "dirty": False,
            "family": "test", "host": "test-box", "metrics": {"failed": 1},
            "requirements": ["SPEC-FIX-01"], "result": "fail", "time": "2026-10-17T00:00:00Z"
        }
        ledger.append(root, rec_fail)
        reqs, skills, _ = sm.build_matrix(root)
        assert reqs["SPEC-FIX-01"].verdict(set(skills)) == "FAILING", "failed check marks requirement FAILING"

        rec_pass = {
            "check": "ctest:debug", "commit": commit, "dirty": False,
            "family": "test", "host": "test-box", "metrics": {"failed": 0},
            "requirements": ["SPEC-FIX-01"], "result": "pass", "time": "2026-10-17T00:01:00Z"
        }
        ledger.append(root, rec_pass)
        reqs, skills, _ = sm.build_matrix(root)
        assert reqs["SPEC-FIX-01"].verdict(set(skills)) == "VERIFIED", "passing check marks requirement VERIFIED"

        (root / "src" / "a.cpp").write_text(A_CPP + "\n// modified\n")
        run(["git", "add", "src/a.cpp"], root)
        run(["git", "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-q", "-m", "modify a.cpp"], root)
        reqs, skills, _ = sm.build_matrix(root)
        assert reqs["SPEC-FIX-01"].verdict(set(skills)) == "STALE", "governed code change after test marks requirement STALE"


def test_device_tree_and_yaml_traceability():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        make_repo(root)
        # Add a device tree overlay and a yaml configuration implementing SPEC-FIX-01 and SPEC-FIX-02
        dt_file = root / "src" / "board.dtso"
        dt_file.write_text(f"// {IMPL} [SPEC-FIX-01] docs/FIX_SPECIFICATION.md\n/dts-v1/;\n/plugin/;\n")
        yaml_file = root / "src" / "config.yaml"
        yaml_file.write_text(f"# {IMPL} [SPEC-FIX-02] docs/FIX_SPECIFICATION.md\nversion: '1.0'\n")

        run(["git", "add", "src/board.dtso", "src/config.yaml"], root)
        run(["git", "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-q", "-m", "add dt and yaml"], root)

        reqs, skills, _ = sm.build_matrix(root)
        impl_files_01 = [file for file, _, _ in reqs["SPEC-FIX-01"].impls]
        impl_files_02 = [file for file, _, _ in reqs["SPEC-FIX-02"].impls]
        assert "src/board.dtso" in impl_files_01, "device tree overlay is an implementation"
        assert "src/config.yaml" in impl_files_02, "yaml config is an implementation"

        # Check that changes in .dtso and .yaml are governed by requirements
        dt_file.write_text(f"// {IMPL} [SPEC-FIX-01] docs/FIX_SPECIFICATION.md\n/dts-v1/;\n/plugin/;\n/* new node */\n")
        yaml_file.write_text(f"# {IMPL} [SPEC-FIX-02] docs/FIX_SPECIFICATION.md\nversion: '1.0'\nchannels: 4\n")

        rc, out = review(root)
        assert rc == 0
        assert "SPEC-FIX-01" in out and "src/board.dtso" in out
        assert "SPEC-FIX-02" in out and "src/config.yaml" in out

        # Check that an untraced .yaml or .dtso hunk is flagged
        (root / "src" / "untraced.yaml").write_text("untraced_field: true\n")
        run(["git", "add", "src/untraced.yaml"], root)
        out_io = io.StringIO()
        with contextlib.redirect_stdout(out_io):
            rc = sm.review_changes(root, "", False, True, staged=True)
        assert rc == 1 and "[UNTRACED] src/untraced.yaml:1" in out_io.getvalue(), "untraced yaml fails --strict"


def test_hierarchy_and_graph_export():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        make_repo(root)
        spec = root / "docs" / "FIX_SPECIFICATION.md"
        spec.write_text(SPEC + """
### `[SPEC-FIX-06]` Child derived requirement
* **Refines**: `[SPEC-FIX-01]`
* **Method**: `Analysis`
* **Verified by**: `host-tests`
""")
        reqs, skills, _ = sm.build_matrix(root)
        assert reqs["SPEC-FIX-06"].refines == ["SPEC-FIX-01"]
        assert "SPEC-FIX-06" in reqs["SPEC-FIX-01"].children
        assert reqs["SPEC-FIX-06"].method == "Analysis"

        # Check Mermaid graph export
        graph = sm.generate_mermaid_graph(reqs, skills, "SPEC-FIX-01")
        assert "```mermaid" in graph
        assert "SPEC_FIX_01 --> SPEC_FIX_06" in graph
        assert "Method: Analysis" in graph


def test_reqif_export():
    import xml.etree.ElementTree as ET
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        make_repo(root)
        spec = root / "docs" / "FIX_SPECIFICATION.md"
        spec.write_text(SPEC + """
### `[SPEC-FIX-06]` Child derived requirement
* **Refines**: `[SPEC-FIX-01]`
* **Method**: `Analysis`
* **Verified by**: `host-tests`
""")
        reqs, skills, _ = sm.build_matrix(root)
        reqif_file = root / "build" / "specs.reqif"
        sm.export_reqif(reqs, skills, reqif_file)
        assert reqif_file.exists()

        # Parse and validate XML schema
        tree = ET.parse(reqif_file)
        root_elem = tree.getroot()
        assert root_elem.tag.endswith("REQ-IF")

        # Check tool ID in header
        header = root_elem.find(".//{*}REQ-IF-HEADER")
        assert header is not None
        tool_id = header.find("{*}REQ-IF-TOOL-ID")
        assert tool_id is not None and tool_id.text == "AbstractX SpecTrace"

        # Check spec objects
        spec_objs = root_elem.findall(".//{*}SPEC-OBJECT")
        obj_ids = [so.attrib.get("IDENTIFIER") for so in spec_objs]
        assert "SO-SPEC-FIX-01" in obj_ids
        assert "SO-SPEC-FIX-06" in obj_ids

        # Check relations (Refines)
        relations = root_elem.findall(".//{*}SPEC-RELATION")
        assert len(relations) >= 1
        rel = relations[0]
        assert rel.find("{*}SOURCE/{*}SPEC-OBJECT-REF").text == "SO-SPEC-FIX-06"
        assert rel.find("{*}TARGET/{*}SPEC-OBJECT-REF").text == "SO-SPEC-FIX-01"

        # Check specification document hierarchy
        docs = root_elem.findall(".//{*}SPECIFICATION")
        assert len(docs) >= 1


if __name__ == "__main__":
    test_matrix_verdicts()
    test_change_review_maps_hunks_and_flags_untraced()
    test_stale_and_failing_verdicts()
    test_device_tree_and_yaml_traceability()
    test_hierarchy_and_graph_export()
    test_reqif_export()
    print("[PASS] spectrace_matrix")



