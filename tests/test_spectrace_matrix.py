#!/usr/bin/env python3
# @verifies [SPEC-AUDIT-06] [SPEC-AUDIT-07] [SPEC-AUDIT-08] [SPEC-AUDIT-10] [SPEC-AUDIT-11] [SPEC-AUDIT-12] [SPEC-AUDIT-13] docs/SPECTRACE.md
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
    (root / ".agents" / "skills" / "host-tests").mkdir(parents=True)
    (root / "docs" / "FIX_SPECIFICATION.md").write_text(SPEC)
    (root / ".agents" / "skills" / "host-tests" / "SKILL.md").write_text(
        "---\nname: host-tests\ndescription: Run the host tests.\nruns_on: host\n---\n")
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
            "SPEC-FIX-01": "STALE",  # skills named but no evidence yet
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
        assert "0/5" in text and "| `SPEC-FIX-03` |" in text


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
        assert reqs["SPEC-FIX-01"].verdict(set(skills)) == "STALE", "no evidence is never VERIFIED"

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


def test_skill_layout_shared_by_all_agents():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        skills = root / ".agents" / "skills"
        (skills / "good").mkdir(parents=True)
        (skills / "good" / "SKILL.md").write_text("---\nname: good\ndescription: Does a thing.\nruns_on: host\n---\n")
        (skills / "flat.md").write_text("---\nname: flat\nverifies: x\n---\n")
        (skills / "nodesc").mkdir()
        (skills / "nodesc" / "SKILL.md").write_text("---\nname: other\n---\n")
        (root / ".agents" / "rules").mkdir()
        (root / ".agents" / "rules" / "r.md").write_text("# rule\n")
        problems = "\n".join(sm.check_skills(root))
        assert "flat.md" in problems, "loose .md skill is flagged"
        assert "nodesc" in problems and "description" in problems, "missing description is flagged"
        assert "name 'other'" in problems, "name must match folder"
        assert ".claude/skills" in problems, "missing Claude Code link is flagged"
        assert "r.md" in problems, "rule not imported by CLAUDE.md is flagged"
        assert set(sm.load_skills(root)) == {"good", "flat", "nodesc"}, "both layouts still load"
        try:
            import yaml  # noqa: F401
            (skills / "good" / "SKILL.md").write_text("---\nname: good\ndescription: a: b\n---\n")
            assert "not valid YAML" in "\n".join(sm.check_skills(root)), "unquoted ': ' is flagged"
            (skills / "good" / "SKILL.md").write_text("---\nname: good\ndescription: Does a thing.\nruns_on: host\n---\n")
        except ImportError:
            pass

        (skills / "flat.md").unlink()
        (skills / "nodesc" / "SKILL.md").write_text("---\nname: nodesc\ndescription: d\nruns_on: cloud\n---\n")
        assert "runs_on 'cloud'" in "\n".join(sm.check_skills(root)), "unknown runs_on is flagged"
        (skills / "nodesc" / "SKILL.md").write_text("---\nname: nodesc\ndescription: d\nruns_on: board\n---\n")
        (root / ".claude").mkdir()
        (root / ".claude" / "skills").symlink_to("../.agents/skills")
        (root / "CLAUDE.md").write_text("@AGENTS.md\n@.agents/rules/r.md\n")
        assert sm.check_skills(root) == [], sm.check_skills(root)

    repo = Path(__file__).resolve().parent.parent
    assert sm.check_skills(repo) == [], sm.check_skills(repo)


DRIFT_SPEC = """# Drift Fixture

### `[SPEC-DRIFT-01]` Names a function the code lacks
* **Requirement**: calls `do_thing(x)` and `missing_fn()`.
* **Verified by**: `host-tests`
* **Implementation Target**: `src/c.cpp`
"""


def test_drift_check_and_not_implemented_line():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        make_repo(root)
        (root / "docs" / "DRIFT_SPECIFICATION.md").write_text(DRIFT_SPEC)
        (root / "src" / "c.cpp").write_text("// @" "impl [SPEC-DRIFT-01] docs/DRIFT_SPECIFICATION.md\n"
                                            "void do_thing(int) {}\n")
        (root / "tests").mkdir(exist_ok=True)
        (root / "tests" / "t.py").write_text("missing_fn()  # a test mentioning it does not count\n")
        run(["git", "add", "-A"], root)
        reqs, _, _ = sm.build_matrix(root)
        drift = sm.check_drift(root, reqs)
        assert drift == {"SPEC-DRIFT-01": ["missing_fn"]}, drift

        spec = root / "docs" / "DRIFT_SPECIFICATION.md"
        spec.write_text(DRIFT_SPEC.replace("* **Verified by**", "* **Not implemented yet**: `missing_fn`\n* **Verified by**"))
        reqs, skills, _ = sm.build_matrix(root)
        assert sm.check_drift(root, reqs) == {}
        assert reqs["SPEC-DRIFT-01"].verdict(set(skills)) == "PARTIAL", "unimplemented parts are never VERIFIED"


def test_orphan_test_check():
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        (root / "tests" / "board").mkdir(parents=True)
        (root / "sim" / "cocotb").mkdir(parents=True)
        for f in ("tests/test_a.py", "tests/test_b.py", "tests/test_c.py", "tests/test_d.cpp",
                  "tests/board/test_e.py", "sim/cocotb/test_x_cocotb.py", "sim/cocotb/test_y_cocotb.py"):
            (root / f).write_text("")
        (root / "tests" / "CMakeLists.txt").write_text(
            "add_test(NAME a COMMAND python3 test_a.py)\nadd_x(test_d.cpp)\n"
            "# not-in-ctest: tests/test_c.py needs a board\n# not-in-ctest: tests/board/test_e.py runs on the board\n")
        (root / "sim" / "CMakeLists.txt").write_text("add_cocotb_test(t mod test_y_cocotb)\n")
        problems = "\n".join(sm.check_tests(root))
        assert "tests/test_b.py" in problems and "sim/cocotb/test_x_cocotb.py" in problems, problems
        for ok in ("test_a.py", "test_c.py", "test_d.cpp", "test_e.py", "test_y_cocotb"):
            assert ok not in problems, (ok, problems)


def test_placement_and_board_evidence_source():
    from trac_lib import ledger
    with tempfile.TemporaryDirectory() as d:
        root = Path(d)
        make_repo(root)
        (root / "targets" / "mcu").mkdir(parents=True)
        (root / "targets" / "mcu" / "drv.cpp").write_text("// @" + "impl [SPEC-FIX-01] docs/FIX_SPECIFICATION.md\n")
        bb = root / ".agents" / "skills" / "board-check"
        bb.mkdir()
        bb.joinpath("SKILL.md").write_text("---\nname: board-check\ndescription: d\nruns_on: board\n---\n")
        run(["git", "add", "-A"], root)
        run(["git", "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-q", "-m", "target code"], root)
        reqs, skills, _ = sm.build_matrix(root)
        assert "SPEC-FIX-01" in sm.check_placement(root, reqs), "host-only check of target code is reported"

        spec = root / "docs" / "FIX_SPECIFICATION.md"
        spec.write_text(SPEC.replace("* **Verified by**: `host-tests` (", "* **Verified by**: `board-check` ("))
        run(["git", "-c", "user.email=t@t", "-c", "user.name=t", "commit", "-qam", "board skill"], root)
        commit = subprocess.check_output(["git", "rev-parse", "--short=12", "HEAD"], cwd=root, text=True).strip()
        reqs, skills, _ = sm.build_matrix(root)
        assert "SPEC-FIX-01" not in sm.check_placement(root, reqs)
        rec = {"check": "board-check", "commit": commit, "dirty": False, "family": "board", "host": "pc",
               "metrics": {}, "requirements": ["SPEC-FIX-01"], "result": "pass", "time": "2026-10-20T00:00:00Z"}
        ledger.append(root, dict(rec, source="host"))
        reqs, skills, _ = sm.build_matrix(root)
        assert reqs["SPEC-FIX-01"].verdict(set(skills)) == "STALE", "board skill evidence made on the host does not count"
        ledger.append(root, dict(rec, source="board", time="2026-10-20T00:01:00Z"))
        reqs, skills, _ = sm.build_matrix(root)
        assert reqs["SPEC-FIX-01"].verdict(set(skills)) == "VERIFIED"


def test_repo_is_honest():
    """The real tree: no drift and no orphan tests ([SPEC-AUDIT-12])."""
    repo = Path(__file__).resolve().parent.parent
    reqs, _, _ = sm.build_matrix(repo)
    assert sm.check_drift(repo, reqs) == {}, sm.check_drift(repo, reqs)
    assert sm.check_tests(repo) == [], sm.check_tests(repo)


if __name__ == "__main__":
    test_matrix_verdicts()
    test_change_review_maps_hunks_and_flags_untraced()
    test_stale_and_failing_verdicts()
    test_device_tree_and_yaml_traceability()
    test_hierarchy_and_graph_export()
    test_reqif_export()
    test_skill_layout_shared_by_all_agents()
    test_drift_check_and_not_implemented_line()
    test_orphan_test_check()
    test_placement_and_board_evidence_source()
    test_repo_is_honest()
    print("[PASS] spectrace_matrix")



