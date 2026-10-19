#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
SpecTrace matrix and change review.

Ties three things together for every requirement:
  * what / why    : the requirement text in a *SPECIFICATION.md file
  * implemented by: `@impl [SPEC-*]` tags in code (with optional `@status`)
  * verified by   : skills named in the requirement's `**Verified by**:` line,
                    each a procedure in .agents/skills/<name>.md

Modes
  --matrix            requirement -> code -> skills, with a verdict per row
  --changes           map a git diff to the requirements that govern each
                      changed hunk; flag hunks no requirement covers
  --changes --prompt  bundle the retrieved requirement text with the diff for
                      a reviewer (human or AI)

Exit codes: --require-complete fails unless every requirement is VERIFIED;
--strict fails on untraced code hunks.
"""

# @impl [SPEC-AUDIT-06] [SPEC-AUDIT-07] [SPEC-AUDIT-08] [SPEC-AUDIT-10] docs/SPECTRACE.md
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from spectrace_drift import CODE_EXTS, IGNORED_TAGS, SPEC_PATTERN, discover_all_specs  # noqa: E402

from trac_lib import ledger  # noqa: E402

TAG_RE = re.compile(r"\[(SPEC-[A-Z0-9\-]+)\]")
# Only a bullet that starts with **Verified by**: (not a mention inside prose).
VERIFIED_RE = re.compile(r"^\s*[*-]\s+\*\*Verified by\*\*\s*:\s*(.*)")
REFINES_RE = re.compile(r"^\s*[*-]\s+\*\*(?:Refines|Parent)\*\*\s*:\s*(.*)")
METHOD_RE = re.compile(r"^\s*[*-]\s+\*\*Method\*\*\s*:\s*(.*)")
STATUS_RE = re.compile(r"@status\s+([A-Za-z]+)")
HEADING_RE = re.compile(r"^#{1,3}\s")
HUNK_RE = re.compile(r"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@")
SKILL_TOKEN_RE = re.compile(r"`([a-z0-9][a-z0-9\-]*)`")
EXCLUDED_PARTS = {"third_party", ".git", "build", "node_modules"}

VERDICTS = ("VERIFIED", "STALE", "FAILING", "IMPLEMENTED", "PARTIAL", "SPEC-ONLY", "BROKEN")

SKILL_MAP = {
    "host-tests": {"ctest:debug", "ctest:host", "ctest:plain"},
    "check-host-tests": {"ctest:debug", "ctest:host", "ctest:plain"},
    "sanitizer-gate": {"ctest:asan", "ctest:asan-ubsan"},
    "check-asan-ubsan": {"ctest:asan", "ctest:asan-ubsan"},
    "thread-sanitizer": {"ctest:tsan"},
    "check-tsan": {"ctest:tsan"},
    "cocotb-regression": {"cocotb:all"},
    "check-rtl-sim": {"cocotb:all"},
    "rtl-lint": {"rtl:lint"},
    "check-rtl-lint": {"rtl:lint"},
    "rtl-synth-check": {"rtl:synth-check"},
    "check-rtl-synth": {"rtl:synth-check"},
    "e906-build-layout": {"elf:e906", "elf:e906/release", "e906:layout"},
    "check-e906-layout": {"elf:e906", "elf:e906/release", "e906:layout"},
    "e906-disassembly-check": {"disasm:e906"},
    "check-e906-disasm": {"disasm:e906"},
    "memory-footprint": {"elf:e906", "elf:pico2w", "elf:esp32p4"},
    "measure-memory": {"elf:e906", "elf:pico2w", "elf:esp32p4"},
    "fpga-fit": {"fpga-fit:tang_nano20k", "fpga-fit:primer20k"},
    "measure-fpga-fit": {"fpga-fit:tang_nano20k", "fpga-fit:primer20k"},
    "spectrace-matrix": {"spectrace:matrix"},
}


@dataclass
class Requirement:
    tag: str
    title: str
    spec: str
    line: int
    end_line: int
    body: list[str] = field(default_factory=list)
    skills: list[str] = field(default_factory=list)
    impls: list[tuple[str, int, str]] = field(default_factory=list)  # (file, line, status)
    tests: list[tuple[str, int]] = field(default_factory=list)       # (file, line)
    refines: list[str] = field(default_factory=list)                 # parent requirement tags
    children: list[str] = field(default_factory=list)                # child requirement tags
    method: str = "Test"                                             # Test, Analysis, Inspection, Demonstration
    evidence: dict = field(default_factory=dict)

    def verdict(self, known_skills: set[str]) -> str:
        if any(s not in known_skills for s in self.skills):
            return "BROKEN"
        if not self.impls:
            return "SPEC-ONLY"
        if any(st.lower() == "partial" for _, _, st in self.impls):
            return "PARTIAL"
        if not self.skills:
            return "IMPLEMENTED"
        if self.evidence:
            if self.evidence.get("failing"):
                return "FAILING"
            if self.evidence.get("stale"):
                return "STALE"
            if self.evidence.get("verified"):
                return "VERIFIED"
        return "VERIFIED"


@dataclass
class Anchor:
    line: int
    tags: list[str]
    status: str
    kind: str = "impl"  # "impl" (implements) or "verifies" (a test of the requirement)


def git(root: Path, *args: str) -> str:
    return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, check=True).stdout


def is_code(path: str) -> bool:
    p = Path(path)
    return p.suffix in CODE_EXTS and not (set(p.parts) & EXCLUDED_PARTS)


def load_skills(root: Path) -> dict[str, Path]:
    return {p.stem: p for p in sorted((root / ".agents" / "skills").glob("*.md"))}


def parse_requirements(root: Path) -> dict[str, Requirement]:
    reqs: dict[str, Requirement] = {}
    for spec in discover_all_specs(root):
        if not spec.exists():
            continue
        rel = str(spec.relative_to(root))
        lines = spec.read_text(encoding="utf-8", errors="ignore").splitlines()
        current: Requirement | None = None
        for idx, raw in enumerate(lines, 1):
            m = SPEC_PATTERN.match(raw.strip())
            if m or (current and HEADING_RE.match(raw)):
                if current:
                    current.end_line = idx - 1
                    current = None
            if m and m.group(1) not in IGNORED_TAGS:
                current = Requirement(m.group(1), m.group(2).strip(), rel, idx, len(lines))
                reqs[current.tag] = current
            if current:
                current.body.append(raw)
                v = VERIFIED_RE.match(raw)
                if v:
                    # Parenthesised notes are evidence detail, not skill names.
                    names = SKILL_TOKEN_RE.findall(re.sub(r"\([^)]*\)", "", v.group(1)))
                    current.skills += [s for s in names if s not in current.skills]
                ref = REFINES_RE.match(raw)
                if ref:
                    parents = [p for p in TAG_RE.findall(ref.group(1)) if p not in IGNORED_TAGS]
                    current.refines += [p for p in parents if p not in current.refines]
                meth = METHOD_RE.match(raw)
                if meth:
                    m_val = meth.group(1).strip().strip("`* ")
                    if m_val:
                        current.method = m_val

    # Link child requirements to parents
    for tag, r in reqs.items():
        for p in r.refines:
            if p in reqs and tag not in reqs[p].children:
                reqs[p].children.append(tag)
    return reqs


def scan_anchors(root: Path, files: list[str]) -> dict[str, list[Anchor]]:
    """@impl / @verifies tag lines per file, with the @status that follows within 3 lines.

    `@verifies [SPEC-*]` marks a test: it governs changes for review but does
    not count as an implementation in the matrix.
    """
    anchors: dict[str, list[Anchor]] = {}
    for rel in files:
        path = root / rel
        if not is_code(rel) or not path.is_file():
            continue
        lines = path.read_text(encoding="utf-8", errors="ignore").splitlines()
        found = []
        for i, raw in enumerate(lines):
            if "@impl" in raw:
                kind = "impl"
            elif "@verifies" in raw:
                kind = "verifies"
            else:
                continue
            tags = [t for t in TAG_RE.findall(raw) if t not in IGNORED_TAGS]
            if not tags:
                continue
            status = "Complete"
            for nxt in lines[i + 1:i + 4]:
                s = STATUS_RE.search(nxt)
                if s:
                    status = s.group(1)
                    break
            found.append(Anchor(i + 1, tags, status, kind))
        if found:
            anchors[rel] = found
    return anchors


def tracked_files(root: Path) -> list[str]:
    files = git(root, "ls-files").splitlines()
    files += git(root, "ls-files", "--others", "--exclude-standard").splitlines()
    return files


def evaluate_evidence(root: Path, reqs: dict[str, Requirement], records: list[dict]) -> None:
    if not records:
        return

    latest_by_check: dict[str, dict] = {}
    for r in records:
        latest_by_check[r["check"]] = r

    latest_by_tag: dict[str, dict] = {}
    for r in records:
        for tag in r.get("requirements", []):
            latest_by_tag[tag] = r

    dirty_files = set()
    try:
        status_out = git(root, "status", "--porcelain", "--untracked-files=no")
        for line in status_out.splitlines():
            if len(line) > 3:
                dirty_files.add(line[3:].strip())
    except Exception:
        pass

    changed_cache: dict[str, set[str]] = {}

    def get_changed_since(commit: str) -> set[str]:
        if not commit or commit == "unknown":
            return set()
        if commit in changed_cache:
            return changed_cache[commit]
        try:
            out = git(root, "diff", "--name-only", commit, "HEAD")
            res = set(out.splitlines())
        except Exception:
            res = set()
        changed_cache[commit] = res
        return res

    for tag, r in reqs.items():
        if not r.skills or not r.impls:
            continue
        governed_files = {r.spec} | {f for f, _, _ in r.impls}
        failing = False
        stale = False
        skills_tested = 0

        for sk in r.skills:
            candidate = None
            if tag in latest_by_tag:
                candidate = latest_by_tag[tag]
            else:
                checks = SKILL_MAP.get(sk, {sk})
                matching = [latest_by_check[c] for c in checks if c in latest_by_check]
                if matching:
                    candidate = max(matching, key=lambda x: x.get("time", ""))

            if candidate is None:
                stale = True
            else:
                skills_tested += 1
                if candidate.get("result") == "fail":
                    failing = True
                elif candidate.get("result") == "pass":
                    cmt = candidate.get("commit", "")
                    changed = get_changed_since(cmt)
                    if any(gf in changed or gf in dirty_files for gf in governed_files):
                        stale = True
                else:
                    stale = True

        if skills_tested == 0:
            continue

        if failing:
            r.evidence = {"failing": True}
        elif stale:
            r.evidence = {"stale": True}
        else:
            r.evidence = {"verified": True}


def build_matrix(root: Path) -> tuple[dict[str, Requirement], dict[str, Path], dict[str, list[Anchor]]]:
    reqs = parse_requirements(root)
    skills = load_skills(root)
    anchors = scan_anchors(root, tracked_files(root))
    for rel, items in anchors.items():
        for a in items:
            if a.kind == "impl":
                for tag in a.tags:
                    if tag in reqs:
                        reqs[tag].impls.append((rel, a.line, a.status))
            elif a.kind == "verifies":
                for tag in a.tags:
                    if tag in reqs:
                        reqs[tag].tests.append((rel, a.line))
    try:
        records = ledger.read_all(root)
    except Exception:
        records = []
    evaluate_evidence(root, reqs, records)
    return reqs, skills, anchors


def print_matrix(reqs: dict[str, Requirement], skills: dict[str, Path], write: Path | None) -> int:
    known = set(skills)
    counts = {v: 0 for v in VERDICTS}
    rows = []
    for tag in sorted(reqs):
        r = reqs[tag]
        v = r.verdict(known)
        counts[v] += 1
        impl = ", ".join(sorted({f for f, _, _ in r.impls})) or "-"
        sk = ", ".join(r.skills) or "-"
        rows.append((v, r, impl, sk))
    total = len(reqs)
    verified = counts["VERIFIED"]
    print("=" * 78)
    print("SpecTrace matrix: requirement -> implemented by -> verified by")
    print("=" * 78)
    print(f"Requirements : {total}   skills: {len(skills)}")
    for v in VERDICTS:
        print(f"  {v:<12}: {counts[v]}")
    pct = 100.0 * verified / total if total else 0.0
    print(f"Complete (implemented AND verified): {verified}/{total} = {pct:.1f}%")
    for v, r, _, sk in rows:
        if v == "BROKEN":
            missing = [s for s in r.skills if s not in known]
            print(f"  BROKEN {r.tag}: unknown skill(s) {missing} ({r.spec}:{r.line})")
    if write:
        out = ["# SpecTrace Traceability Matrix", "",
               "Generated by `python3 tools/spectrace_matrix.py --matrix --write` — do not edit by hand.", "",
               f"**Complete (implemented and verified): {verified}/{total} ({pct:.1f}%)** · "
               + " · ".join(f"{v}: {counts[v]}" for v in VERDICTS), "",
               "| Requirement | Title | Method | Spec | Implemented by | Verified by (skills) | Verdict |",
               "| :--- | :--- | :--- | :--- | :--- | :--- | :--- |"]
        for v, r, impl, sk in rows:
            title = r.title.replace("|", "\\|")
            if r.refines:
                title += f" *(refines {', '.join(f'`{p}`' for p in r.refines)})*"
            out.append(f"| `{r.tag}` | {title} | {r.method} | `{r.spec}:{r.line}` | {impl} | {sk} | {v} |")
        write.parent.mkdir(parents=True, exist_ok=True)
        write.write_text("\n".join(out) + "\n", encoding="utf-8")
        print(f"Wrote {write}")
    return 0 if verified == total else 1


def generate_mermaid_graph(reqs: dict[str, Requirement], skills: dict[str, Path], root_tag: str | None = None) -> str:
    known = set(skills)
    lines = ["```mermaid", "graph TD"]
    lines.append("    %% SpecTrace Mermaid Traceability DAG")
    lines.append("    classDef verified fill:#14532d,stroke:#22c55e,color:#ffffff,stroke-width:2px;")
    lines.append("    classDef implemented fill:#1e3a8a,stroke:#3b82f6,color:#ffffff,stroke-width:2px;")
    lines.append("    classDef stale fill:#713f12,stroke:#eab308,color:#ffffff,stroke-width:2px;")
    lines.append("    classDef failing fill:#7f1d1d,stroke:#ef4444,color:#ffffff,stroke-width:2px;")
    lines.append("    classDef speconly fill:#334155,stroke:#64748b,color:#ffffff,stroke-dasharray: 5 5;")
    lines.append("    classDef code fill:#0f172a,stroke:#94a3b8,color:#e2e8f0;")
    lines.append("    classDef test fill:#312e81,stroke:#a5b4fc,color:#ffffff;")

    def safe_id(tag: str) -> str:
        return tag.replace("-", "_").replace(".", "_")

    def class_for_verdict(v: str) -> str:
        return {
            "VERIFIED": "verified",
            "STALE": "stale",
            "FAILING": "failing",
            "IMPLEMENTED": "implemented",
            "PARTIAL": "stale",
            "SPEC-ONLY": "speconly",
            "BROKEN": "failing",
        }.get(v, "speconly")

    target_tag = root_tag.strip() if root_tag else None
    if target_tag:
        if target_tag not in reqs:
            return f"%% Requirement {target_tag} not found"
        selected_tags = {target_tag}
        r = reqs[target_tag]
        selected_tags.update(r.refines)
        selected_tags.update(r.children)
    else:
        selected_tags = set(reqs.keys())

    # Requirement nodes
    for tag in sorted(selected_tags):
        if tag not in reqs:
            continue
        r = reqs[tag]
        v = r.verdict(known)
        cls = class_for_verdict(v)
        title = r.title.replace('"', "'").replace("[", "(").replace("]", ")")
        node_id = safe_id(tag)
        lines.append(f'    {node_id}["[{tag}]<br/>{title}<br/><i>Method: {r.method} | {v}</i>"]:::{cls}')

    # Derivation edges (parent --> child)
    for tag in sorted(selected_tags):
        if tag not in reqs:
            continue
        r = reqs[tag]
        for parent in r.refines:
            if parent in selected_tags:
                lines.append(f'    {safe_id(parent)} --> {safe_id(tag)}')

    # Code and test edges
    if target_tag or len(selected_tags) <= 12:
        for tag in sorted(selected_tags):
            if tag not in reqs:
                continue
            r = reqs[tag]
            node_id = safe_id(tag)
            for f, line, _ in r.impls:
                f_name = Path(f).name
                stem = safe_id(Path(f).stem)
                impl_id = f"impl_{safe_id(tag)}_{stem}_{line}"
                lines.append(f'    {impl_id}["@impl {f_name}:{line}"]:::code')
                lines.append(f'    {node_id} --> {impl_id}')
            for f, line in r.tests:
                f_name = Path(f).name
                stem = safe_id(Path(f).stem)
                test_id = f"test_{safe_id(tag)}_{stem}_{line}"
                lines.append(f'    {test_id}["@verifies {f_name}:{line}"]:::test')
                lines.append(f'    {test_id} -.-> {node_id}')

    lines.append("```")
    return "\n".join(lines)


def export_reqif(reqs: dict[str, Requirement], skills: dict[str, Path], output_path: Path) -> None:
    """Exports requirements, derivation hierarchy, and implementation anchors as OMG ReqIF XML."""
    now_iso = datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    known_skills = set(skills)

    reqif = ET.Element("REQ-IF", {
        "xmlns": "http://www.omg.org/spec/ReqIF/20110401/reqif.xsd",
        "xmlns:xsi": "http://www.w3.org/2001/XMLSchema-instance",
        "xsi:schemaLocation": "http://www.omg.org/spec/ReqIF/20110401/reqif.xsd reqif.xsd",
    })

    # Header
    header_top = ET.SubElement(reqif, "THE-HEADER")
    header = ET.SubElement(header_top, "REQ-IF-HEADER", {"IDENTIFIER": "REQIF-HEADER-ABSTRACTX"})
    ET.SubElement(header, "COMMENT").text = "Generated by AbstractX SpecTrace"
    ET.SubElement(header, "CREATION-TIME").text = now_iso
    ET.SubElement(header, "REQ-IF-TOOL-ID").text = "AbstractX SpecTrace"
    ET.SubElement(header, "REQ-IF-VERSION").text = "1.0"
    ET.SubElement(header, "SOURCE-TOOL-ID").text = "AbstractX"
    ET.SubElement(header, "TITLE").text = "AbstractX Requirements Specification"

    # Core Content
    core_top = ET.SubElement(reqif, "CORE-CONTENT")
    content = ET.SubElement(core_top, "REQ-IF-CONTENT")

    # Datatypes
    datatypes = ET.SubElement(content, "DATATYPES")
    ET.SubElement(datatypes, "DATATYPE-DEFINITION-STRING", {
        "IDENTIFIER": "DT-String",
        "LONG-NAME": "String",
        "MAX-LENGTH": "32768",
    })

    # Spec Types
    spec_types = ET.SubElement(content, "SPEC-TYPES")
    sot = ET.SubElement(spec_types, "SPEC-OBJECT-TYPE", {
        "IDENTIFIER": "SOT-Requirement",
        "LONG-NAME": "Requirement",
    })
    sot_attrs = ET.SubElement(sot, "SPEC-ATTRIBUTES")
    for attr_id, attr_name in [
        ("AD-ReqId", "ReqIF.ForeignID"),
        ("AD-Name", "ReqIF.Name"),
        ("AD-Text", "ReqIF.Text"),
        ("AD-Method", "VerificationMethod"),
        ("AD-SpecFile", "SpecFile"),
        ("AD-Verdict", "Verdict"),
        ("AD-Impls", "Implementations"),
    ]:
        ad = ET.SubElement(sot_attrs, "ATTRIBUTE-DEFINITION-STRING", {
            "IDENTIFIER": attr_id,
            "LONG-NAME": attr_name,
        })
        type_ref = ET.SubElement(ad, "TYPE")
        ET.SubElement(type_ref, "DATATYPE-DEFINITION-STRING-REF").text = "DT-String"

    # Spec Relation Type (Refines)
    ET.SubElement(spec_types, "SPEC-RELATION-TYPE", {
        "IDENTIFIER": "SRT-Refines",
        "LONG-NAME": "Refines",
    })

    # Specification Type (Document)
    spec_type = ET.SubElement(spec_types, "SPECIFICATION-TYPE", {
        "IDENTIFIER": "ST-Document",
        "LONG-NAME": "Document",
    })
    st_attrs = ET.SubElement(spec_type, "SPEC-ATTRIBUTES")
    ad_st = ET.SubElement(st_attrs, "ATTRIBUTE-DEFINITION-STRING", {
        "IDENTIFIER": "AD-DocTitle",
        "LONG-NAME": "ReqIF.Name",
    })
    type_ref = ET.SubElement(ad_st, "TYPE")
    ET.SubElement(type_ref, "DATATYPE-DEFINITION-STRING-REF").text = "DT-String"

    # Spec Objects
    spec_objects = ET.SubElement(content, "SPEC-OBJECTS")
    for tag in sorted(reqs.keys()):
        r = reqs[tag]
        v = r.verdict(known_skills)
        so = ET.SubElement(spec_objects, "SPEC-OBJECT", {
            "IDENTIFIER": f"SO-{tag}",
            "LONG-NAME": tag,
        })
        type_ref = ET.SubElement(so, "TYPE")
        ET.SubElement(type_ref, "SPEC-OBJECT-TYPE-REF").text = "SOT-Requirement"
        vals = ET.SubElement(so, "VALUES")

        def add_str_val(attr_def_id: str, val_text: str):
            av = ET.SubElement(vals, "ATTRIBUTE-VALUE-STRING", {"THE-VALUE": val_text})
            defn = ET.SubElement(av, "DEFINITION")
            ET.SubElement(defn, "ATTRIBUTE-DEFINITION-STRING-REF").text = attr_def_id

        add_str_val("AD-ReqId", tag)
        add_str_val("AD-Name", r.title)
        add_str_val("AD-Text", "\n".join(r.body).strip())
        add_str_val("AD-Method", r.method)
        add_str_val("AD-SpecFile", r.spec)
        add_str_val("AD-Verdict", v)
        impl_summary = "; ".join(f"{f}:{line}" for f, line, _ in r.impls)
        add_str_val("AD-Impls", impl_summary)

    # Spec Relations (Derivation hierarchy: child refines parent)
    spec_relations = ET.SubElement(content, "SPEC-RELATIONS")
    rel_idx = 0
    for tag in sorted(reqs.keys()):
        r = reqs[tag]
        for parent_tag in r.refines:
            if parent_tag in reqs:
                rel_idx += 1
                sr = ET.SubElement(spec_relations, "SPEC-RELATION", {
                    "IDENTIFIER": f"REL-{rel_idx}",
                    "LONG-NAME": f"{tag}-refines-{parent_tag}",
                })
                type_node = ET.SubElement(sr, "TYPE")
                ET.SubElement(type_node, "SPEC-RELATION-TYPE-REF").text = "SRT-Refines"
                source_node = ET.SubElement(sr, "SOURCE")
                ET.SubElement(source_node, "SPEC-OBJECT-REF").text = f"SO-{tag}"
                target_node = ET.SubElement(sr, "TARGET")
                ET.SubElement(target_node, "SPEC-OBJECT-REF").text = f"SO-{parent_tag}"

    # Specifications (Document structure)
    specifications = ET.SubElement(content, "SPECIFICATIONS")
    specs_by_file: dict[str, list[Requirement]] = {}
    for r in reqs.values():
        specs_by_file.setdefault(r.spec, []).append(r)

    for doc_idx, (spec_file, file_reqs) in enumerate(sorted(specs_by_file.items()), 1):
        spec_node = ET.SubElement(specifications, "SPECIFICATION", {
            "IDENTIFIER": f"SPEC-DOC-{doc_idx}",
            "LONG-NAME": Path(spec_file).name,
        })
        type_node = ET.SubElement(spec_node, "TYPE")
        ET.SubElement(type_node, "SPECIFICATION-TYPE-REF").text = "ST-Document"
        vals = ET.SubElement(spec_node, "VALUES")
        av = ET.SubElement(vals, "ATTRIBUTE-VALUE-STRING", {"THE-VALUE": spec_file})
        defn = ET.SubElement(av, "DEFINITION")
        ET.SubElement(defn, "ATTRIBUTE-DEFINITION-STRING-REF").text = "AD-DocTitle"

        children = ET.SubElement(spec_node, "CHILDREN")
        for r_idx, r in enumerate(file_reqs, 1):
            sh = ET.SubElement(children, "SPEC-HIERARCHY", {
                "IDENTIFIER": f"SH-{doc_idx}-{r_idx}",
                "LONG-NAME": r.tag,
            })
            obj_ref = ET.SubElement(sh, "OBJECT")
            ET.SubElement(obj_ref, "SPEC-OBJECT-REF").text = f"SO-{r.tag}"

    ET.indent(reqif, space="  ", level=0)
    xml_str = ET.tostring(reqif, encoding="utf-8", xml_declaration=True).decode("utf-8")
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(xml_str + "\n", encoding="utf-8")


@dataclass
class Hunk:
    path: str
    start: int
    count: int
    text: list[str]


def diff_hunks(root: Path, base: str, staged: bool = False) -> list[Hunk]:
    if staged:
        args = ["diff", "-U0", "--no-color", "--cached"]
    else:
        args = ["diff", "-U0", "--no-color", base] if base else ["diff", "-U0", "--no-color", "HEAD"]
    out = git(root, *args)
    hunks: list[Hunk] = []
    path = None
    for line in out.splitlines():
        if line.startswith("+++ "):
            path = None if line.endswith("/dev/null") else line[6:]
        elif line.startswith("@@") and path:
            m = HUNK_RE.match(line)
            if m:
                hunks.append(Hunk(path, int(m.group(3)), int(m.group(4) or 1), [line]))
        elif hunks and path and hunks[-1].path == path and line[:1] in "+- ":
            hunks[-1].text.append(line)
    untracked = [] if staged else git(root, "ls-files", "--others", "--exclude-standard").splitlines()
    for rel in untracked:
        p = root / rel
        if p.is_file():
            body = p.read_text(encoding="utf-8", errors="ignore").splitlines()
            hunks.append(Hunk(rel, 1, len(body), [f"@@ new file {rel} @@"] + ["+" + b for b in body[:40]]))
    return hunks


def governing(anchors: list[Anchor], start: int, count: int) -> Anchor | None:
    """The tag that governs a hunk: the last @impl at or above its end."""
    end = max(start + max(count, 1) - 1, 1)
    best = None
    for a in anchors:
        if a.line <= end:
            best = a
    return best or (anchors[0] if anchors else None)


def review_changes(root: Path, base: str, prompt: bool, strict: bool, staged: bool = False) -> int:
    reqs, skills, anchors = build_matrix(root)
    hunks = diff_hunks(root, base, staged)
    touched: dict[str, list[Hunk]] = {}
    untraced: list[Hunk] = []
    spec_changed: dict[str, list[Hunk]] = {}

    for h in hunks:
        if h.path.endswith("SPECIFICATION.md"):
            for r in reqs.values():
                if r.spec == h.path and r.line <= h.start + max(h.count, 1) - 1 and h.start <= r.end_line:
                    spec_changed.setdefault(r.tag, []).append(h)
            continue
        if not is_code(h.path):
            continue
        a = governing(anchors.get(h.path, []), h.start, h.count)
        if a is None:
            untraced.append(h)
        else:
            for tag in a.tags:
                touched.setdefault(tag, []).append(h)

    print("=" * 78)
    print(f"SpecTrace change review (base: {'staged changes' if staged else (base or 'HEAD')})")
    print("=" * 78)
    print(f"Changed code hunks governed by a requirement : {sum(len(v) for v in touched.values())}")
    print(f"Untraced code hunks (no requirement)          : {len(untraced)}")
    print(f"Requirements whose text changed               : {len(spec_changed)}")
    for tag in sorted(touched):
        r = reqs.get(tag)
        where = ", ".join(sorted({f"{h.path}:{h.start}" for h in touched[tag]}))
        if r is None:
            print(f"  [?] {tag}: tag in code but no such requirement  <- {where}")
            continue
        print(f"  [{r.verdict(set(skills))}] {tag} {r.title}  <- {where}")
        print(f"        re-run: {', '.join(r.skills) or 'NO VERIFYING SKILL'}")
    for tag in sorted(spec_changed):
        r = reqs[tag]
        code = ", ".join(sorted({f'{f}:{ln}' for f, ln, _ in r.impls})) or "no code yet"
        print(f"  [SPEC CHANGED] {tag} {r.title}: re-check {code}; re-run {', '.join(r.skills) or '-'}")
    for h in untraced:
        print(f"  [UNTRACED] {h.path}:{h.start} (+{h.count}) - add a requirement or an @impl tag")

    if prompt:
        print("\n" + "#" * 78)
        print("# Review bundle: retrieved requirements + the changes they govern")
        print("#" * 78)
        print("For each requirement below, check that the changed code still satisfies the "
              "requirement text, and say which `Verified by` skills must be re-run. "
              "Report untraced changes as needing a requirement.\n")
        for tag in sorted(set(touched) | set(spec_changed)):
            r = reqs.get(tag)
            if r is None:
                continue
            print(f"## {tag} — {r.title}  ({r.spec}:{r.line})\n")
            print("\n".join(r.body).strip() + "\n")
            for h in touched.get(tag, []) + spec_changed.get(tag, []):
                print(f"```diff\n# {h.path}\n" + "\n".join(h.text[:80]) + "\n```\n")
        if untraced:
            print("## Untraced changes\n")
            for h in untraced:
                print(f"```diff\n# {h.path}\n" + "\n".join(h.text[:80]) + "\n```\n")
    return 1 if (strict and untraced) else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--matrix", action="store_true", help="requirement -> code -> skills matrix")
    parser.add_argument("--write", type=Path, help="write the matrix as Markdown to this path")
    parser.add_argument("--require-complete", action="store_true",
                        help="exit 1 unless every requirement is implemented and verified")
    parser.add_argument("--changes", action="store_true", help="map a git diff to requirements")
    parser.add_argument("--base", default="", help="git ref to diff against (default: HEAD, i.e. uncommitted)")
    parser.add_argument("--prompt", action="store_true", help="with --changes: print a review bundle")
    parser.add_argument("--strict", action="store_true", help="with --changes: exit 1 on untraced hunks")
    parser.add_argument("--staged", action="store_true", help="with --changes: review only staged changes (pre-commit)")
    parser.add_argument("--graph", nargs="?", const="", default=None,
                        help="generate a Mermaid traceability DAG for a requirement tag (or all if omitted)")
    parser.add_argument("--export-reqif", type=Path,
                        help="export specifications and hierarchy as OMG ReqIF XML to this file")
    args = parser.parse_args()
    root = args.root.resolve()

    if args.changes:
        return review_changes(root, args.base, args.prompt, args.strict, args.staged)
    reqs, skills, _ = build_matrix(root)
    if args.export_reqif:
        export_reqif(reqs, skills, args.export_reqif.resolve())
        print(f"Exported ReqIF XML to {args.export_reqif}")
        return 0
    if args.graph is not None:
        graph = generate_mermaid_graph(reqs, skills, args.graph or None)
        if args.write:
            args.write.parent.mkdir(parents=True, exist_ok=True)
            args.write.write_text(graph + "\n", encoding="utf-8")
            print(f"Wrote {args.write}")
        else:
            print(graph)
        return 0
    rc = print_matrix(reqs, skills, args.write)
    return rc if args.require_complete else 0


if __name__ == "__main__":
    sys.exit(main())
