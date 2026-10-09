#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
SpecTrace Anti-Drift & Traceability Parity Engine (spectrace_drift.py)
====================================================================
Validates bidirectional parity between Markdown specifications (SSOT)
and Freestanding C++20 / RTL implementation code:

1. Spec -> Code Parity: Flags any [SPEC-*] requirement missing code implementation.
2. Code -> Spec Parity: Flags any code implementation unanchored in a specification.
3. Multi-Spec Discovery: Discovers all subsystem SPECIFICATION.md files repository-wide.
4. Semantic Spec Review: Scans specifications for ambiguity, unquantified terms, or missing verification criteria.
"""

import os
import re
import sys
import argparse
import subprocess
from pathlib import Path

# ANSI Color Codes
GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
BLUE = "\033[94m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

SPEC_PATTERN = re.compile(r"^###\s+`?\[(SPEC-[A-Z0-9\-]+)\]`?\s*(.*)")
TARGET_PATTERN = re.compile(r"\*\s+\*\*Implementation Target\*\*:\s*`?([^`\n\r]+)`?")
IMPL_PATTERN = re.compile(r"@impl\s+`?\[(SPEC-[A-Z0-9\-]+)\]`?\s*(.*)")
IGNORED_TAGS = {"SPEC-XXX-YY", "SPEC-X", "SPEC-SAMPLE", "SPEC-TEMPLATE"}

CODE_EXTS = {".hpp", ".cpp", ".h", ".c", ".S", ".ld", ".sv", ".py"}


def get_root_dir() -> Path:
    return Path(__file__).resolve().parent.parent


def discover_all_specs(root_dir: Path) -> list:
    """Discovers all authoritative specification Markdown files using git ls-files."""
    try:
        res = subprocess.run(
            ["git", "ls-files", "*SPECIFICATION.md"],
            cwd=root_dir,
            capture_output=True,
            text=True,
            check=True,
        )
        spec_paths = [root_dir / line.strip() for line in res.stdout.splitlines() if line.strip()]
    except Exception:
        spec_paths = list(root_dir.glob("**/*SPECIFICATION.md"))

    # Also include docs/SPECTRACE.md as it defines authoritative audit specifications
    spectrace_md = root_dir / "docs" / "SPECTRACE.md"
    if spectrace_md.exists() and spectrace_md not in spec_paths:
        spec_paths.append(spectrace_md)

    return sorted(spec_paths)


def find_spec_for_path(target_path: Path, root_dir: Path, all_specs: list) -> Path | None:
    """Finds the most specific SPECIFICATION.md applicable to a path."""
    curr = target_path if target_path.is_dir() else target_path.parent
    while curr != root_dir and curr != curr.parent:
        candidate = curr / "SPECIFICATION.md"
        if candidate in all_specs:
            return candidate
        curr = curr.parent

    # Fallback to DESIGN_SPECIFICATION.md
    fallback = root_dir / "docs" / "DESIGN_SPECIFICATION.md"
    if fallback.exists():
        return fallback
    return None


def parse_specification_file(spec_file: Path, root_dir: Path) -> dict:
    """Parses all [SPEC-*] tags from a single specification file."""
    if not spec_file.exists():
        return {}

    specs = {}
    current_tag = None

    with open(spec_file, "r", encoding="utf-8", errors="ignore") as f:
        for idx, line in enumerate(f, 1):
            m_spec = SPEC_PATTERN.match(line.strip())
            if m_spec:
                current_tag = m_spec.group(1)
                title = m_spec.group(2).strip()
                specs[current_tag] = {
                    "tag": current_tag,
                    "title": title,
                    "line": idx,
                    "spec_file": spec_file,
                    "rel_spec": str(spec_file.relative_to(root_dir)),
                    "targets": [],
                    "implementations": [],
                }
                continue

            if current_tag:
                m_target = TARGET_PATTERN.search(line)
                if m_target:
                    raw_targets = m_target.group(1).split(",")
                    for t in raw_targets:
                        cleaned = t.strip().strip("`").strip()
                        if cleaned:
                            specs[current_tag]["targets"].append(cleaned)
    return specs


def scan_all_codebase_impls(root_dir: Path) -> dict:
    """Scans all tracked source files for // @impl [SPEC-*] tags."""
    try:
        res = subprocess.run(
            ["git", "ls-files"],
            cwd=root_dir,
            capture_output=True,
            text=True,
            check=True,
        )
        files = [root_dir / line.strip() for line in res.stdout.splitlines() if line.strip()]
    except Exception:
        files = [p for p in root_dir.rglob("*") if p.is_file()]

    code_impls = {}
    for f_path in files:
        if f_path.suffix not in CODE_EXTS:
            continue
        try:
            rel_path = f_path.relative_to(root_dir)
            with open(f_path, "r", encoding="utf-8", errors="ignore") as f:
                for idx, line in enumerate(f, 1):
                    if "@impl" in line:
                        matches = re.findall(r"\[(SPEC-[A-Z0-9\-]+)\]", line)
                        for m_tag in matches:
                            if m_tag in IGNORED_TAGS:
                                continue
                            if m_tag not in code_impls:
                                code_impls[m_tag] = []
                            code_impls[m_tag].append({
                                "file": str(rel_path),
                                "line": idx,
                                "desc": line.strip(),
                                "loc": f"{rel_path}:{idx}",
                            })
        except Exception:
            pass
    return code_impls


def calculate_parity(root_dir: Path, spec_files: list | None = None) -> dict:
    """Calculates repository-wide or scoped parity between specifications and code."""
    if not spec_files:
        spec_files = discover_all_specs(root_dir)

    all_specs = {}
    for sf in spec_files:
        parsed = parse_specification_file(sf, root_dir)
        all_specs.update(parsed)

    code_impls = scan_all_codebase_impls(root_dir)

    for tag, data in all_specs.items():
        if tag in code_impls:
            data["implementations"] = code_impls[tag]

    synced = [tag for tag, d in all_specs.items() if d["implementations"]]
    unimplemented_in_code = [tag for tag, d in all_specs.items() if not d["implementations"]]
    unregistered_in_spec = [tag for tag in code_impls if tag not in all_specs]

    return {
        "all_specs": all_specs,
        "code_impls": code_impls,
        "synced": synced,
        "unimplemented_in_code": unimplemented_in_code,
        "unregistered_in_spec": unregistered_in_spec,
    }


def audit_spec_markdown_semantics(spec_file: Path, root_dir: Path) -> list:
    """Audits the specification markdown itself for vague language, missing boundaries, or incomplete contracts."""
    issues = []
    rel_path = spec_file.relative_to(root_dir)

    vague_phrases = [
        (r"\bshould work\b", "Vague expectation 'should work'; specify deterministic pass/fail contract"),
        (r"\bas fast as possible\b", "Unquantified performance goal; specify rate in Hz or latency bound in µs"),
        (r"\bhandle appropriately\b", "Undefined error policy; specify exact error code or state transition"),
        (r"\betc\.\b", "Open-ended 'etc.'; enumerate all valid types or states explicitly"),
        (r"\breliably\b", "Ambiguous quality claim 'reliably'; specify MTBF, retry policy, or timeout"),
    ]

    has_mermaid = False
    spec_tags_found = 0

    with open(spec_file, "r", encoding="utf-8", errors="ignore") as fp:
        lines = fp.readlines()

    for idx, line in enumerate(lines, 1):
        sline = line.strip()
        if "```mermaid" in sline:
            has_mermaid = True

        if SPEC_PATTERN.match(sline):
            spec_tags_found += 1

        for pattern, desc in vague_phrases:
            if re.search(pattern, line, re.IGNORECASE):
                # Don't flag if it's in a rule prohibiting vague terms
                if not any(k in line.lower() for k in ["prohibit", "forbidden", "banned", "avoid", "do not"]):
                    issues.append({
                        "file": str(rel_path),
                        "line": idx,
                        "rule": desc,
                        "snippet": line.strip(),
                    })

    if not has_mermaid:
        issues.append({
            "file": str(rel_path),
            "line": 1,
            "rule": "Missing Mermaid architecture or dataflow diagram in specification",
            "snippet": "",
        })

    return issues


def emit_spec_review_ai_prompt(spec_file: Path, root_dir: Path):
    """Emits the shift-left Spec Review AI instruction prompt."""
    rel_path = spec_file.relative_to(root_dir)
    print(f"\n{BOLD}{CYAN}=== SpecTrace Shift-Left Spec Review Prompt ==={RESET}")
    print(f"Target Specification : {BOLD}{rel_path}{RESET}")
    print("""
[CONTEXT]
You are a Principal Safety-Critical Systems Architect conducting a Shift-Left
Specification Review before any C++ or RTL code is written or refactored.

[INPUT]
- Read the specification file: """ + str(rel_path) + """

[TASK - AUDIT THE SPECIFICATION ITSELF]
Scrutinize this specification across the following 4 engineering dimensions:
1. Edge Cases & Boundary Conditions:
   - What happens when ring buffers saturate or sensor rates mismatch?
   - Identify 3 scenarios where inputs overflow, time out, or produce invalid states.
2. Data Integrity & Concurrency:
   - Are lock-free boundaries (SPSC/MPSC) and interrupt doorbells fully specified?
   - Can race conditions or stale states occur in this dataflow?
3. Protocol & Transport Security:
   - Are packet boundaries, 64-byte alignment, endianness, and CRC checks explicit?
4. Concrete Testability:
   - Is every [SPEC-*] requirement mathematically falsifiable with a unit test?
   - Flag any vague language or missing timeout/error specifications.

Report findings with exact line numbers and proposed [SPEC-*] contract additions.
""")


def print_drift_report(parity: dict, root_dir: Path) -> int:
    """Prints a visual anti-drift report and returns non-zero exit code if drift detected."""
    print("\n" + "=" * 80)
    print(f"{BOLD}             SpecTrace Anti-Drift & Traceability Parity Report{RESET}")
    print("=" * 80)
    print(f"Total Authoritative Requirements : {len(parity['all_specs'])}")
    print(f"Synchronized (<->)                : {GREEN}{len(parity['synced'])}{RESET}")
    print(f"Unimplemented in Code (->)        : {YELLOW if parity['unimplemented_in_code'] else GREEN}{len(parity['unimplemented_in_code'])}{RESET}")
    print(f"Unregistered in Spec (<- DRIFT)   : {RED if parity['unregistered_in_spec'] else GREEN}{len(parity['unregistered_in_spec'])}{RESET}")
    print("-" * 80)

    # Report unregistered in spec (DRIFT)
    if parity["unregistered_in_spec"]:
        print(f"\n{BOLD}{RED}[CODE DRIFT DETECTED - CODE HAS UNANCHORED IMPLEMENTATION TAGS]{RESET}")
        print("The following code tags have NO matching requirement in ANY specification:")
        for tag in sorted(parity["unregistered_in_spec"]):
            locs = ", ".join(i["loc"] for i in parity["code_impls"][tag][:2])
            print(f"  {RED}<- [{tag}]{RESET} implemented in: {locs}")
        print("\nFix: Add formal requirement `### [SPEC-...]` in the relevant SPECIFICATION.md first.")

    # Report unimplemented in code
    if parity["unimplemented_in_code"]:
        print(f"\n{BOLD}{YELLOW}[UNIMPLEMENTED REQUIREMENTS IN SPECIFICATIONS]{RESET}")
        for tag in sorted(parity["unimplemented_in_code"])[:10]:
            sdata = parity["all_specs"][tag]
            print(f"  {YELLOW}-> [{tag}]{RESET} ({sdata['rel_spec']}:{sdata['line']}): {sdata['title']}")
        if len(parity["unimplemented_in_code"]) > 10:
            print(f"  ... and {len(parity['unimplemented_in_code']) - 10} more.")

    print("\n" + "=" * 80)
    drift_count = len(parity["unregistered_in_spec"])
    if drift_count == 0:
        print(f"{BOLD}{GREEN}VERDICT: [ZERO SPECIFICATION DRIFT] (100% Code Anchored in Specs){RESET}")
        print("=" * 80 + "\n")
        return 0
    else:
        print(f"{BOLD}{RED}VERDICT: [FAILED - CODE DRIFT DETECTED] ({drift_count} Unanchored Tags){RESET}")
        print("=" * 80 + "\n")
        return 1


def main():
    parser = argparse.ArgumentParser(description="SpecTrace Anti-Drift & Traceability Parity Engine")
    parser.add_argument("--spec", type=str, help="Path to specific specification file")
    parser.add_argument("--module", type=str, help="Module path to audit drift for")
    parser.add_argument("--spec-review", action="store_true", help="Perform shift-left review of the specification itself")
    parser.add_argument("--ai-prompt", action="store_true", help="Emit shift-left AI spec review prompt")
    args = parser.parse_args()

    root_dir = get_root_dir()
    all_specs = discover_all_specs(root_dir)

    target_spec = None
    if args.spec:
        target_spec = root_dir / args.spec if not Path(args.spec).is_absolute() else Path(args.spec)
    elif args.module:
        target_spec = find_spec_for_path(Path(args.module) if Path(args.module).is_absolute() else root_dir / args.module, root_dir, all_specs)

    if args.spec_review or args.ai_prompt:
        if not target_spec:
            target_spec = root_dir / "docs" / "DESIGN_SPECIFICATION.md"
        if not target_spec.exists():
            print(f"{RED}[ERROR] Specification file not found: {target_spec}{RESET}", file=sys.stderr)
            sys.exit(1)

        print(f"\n{BOLD}[SpecTrace Shift-Left Spec Review]{RESET} Auditing: {target_spec.relative_to(root_dir)}")
        issues = audit_spec_markdown_semantics(target_spec, root_dir)
        if issues:
            print(f"{YELLOW}[WARNING] Found {len(issues)} specification semantic warning(s):{RESET}")
            for iss in issues:
                print(f"  {YELLOW}-> {iss['file']}:{iss['line']}{RESET} [{iss['rule']}]")
                if iss["snippet"]:
                    print(f"     Context: {iss['snippet']}")
        else:
            print(f"{GREEN}[PASS] Specification structure and diagrams verified!{RESET}")

        emit_spec_review_ai_prompt(target_spec, root_dir)
        sys.exit(0)

    # Parity & Drift report
    spec_files = [target_spec] if target_spec else all_specs
    parity = calculate_parity(root_dir, spec_files)
    sys.exit(print_drift_report(parity, root_dir))


if __name__ == "__main__":
    main()
