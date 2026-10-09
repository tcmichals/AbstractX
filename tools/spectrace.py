#!/usr/bin/env python3
"""
SpecTrace Master CLI: Full Round-Trip Synchronization & Verification Engine
============================================================================
Provides complete bidirectional round-tripping between Markdown Specifications
(Single Source of Truth) and Freestanding C++20 / RTL Implementation Code.

Capabilities:
  1. Status:      Inspect bidirectional parity (Spec <-> Code).
  2. To-Spec:     Push new code implementations & docstrings back into SPECIFICATION.md.
  3. To-Code:     Scaffold missing code stubs and // @impl tags from SPECIFICATION.md.
  4. Round-Trip:  Execute full two-way reconciliation in a single command.
  5. Audit:       Execute the 6-Stage Adversarial Invariant Gate.

Usage:
  python3 tools/spectrace.py --status
  python3 tools/spectrace.py --to-spec [--spec <path>] [--apply]
  python3 tools/spectrace.py --to-code [--spec <path>] [--apply]
  python3 tools/spectrace.py --roundtrip [--spec <path>] [--apply]
    python3 tools/spectrace.py --review [--module <path>] [--related-path <path>]
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

def get_root_dir() -> Path:
    return Path(__file__).resolve().parent.parent

def parse_specification(spec_file: Path) -> dict:
    """Parses all [SPEC-*] requirements, titles, and implementation targets from a markdown spec."""
    if not spec_file.exists():
        return {}

    spec_pattern = re.compile(r"###\s+`?\[(SPEC-[A-Z0-9\-]+)\]`?\s*(.*)")
    target_pattern = re.compile(r"\*\s+\*\*Implementation Target\*\*:\s*`?([^`\n\r]+)`?")

    specs = {}
    current_tag = None

    with open(spec_file, "r", encoding="utf-8") as f:
        for idx, line in enumerate(f, 1):
            m_spec = spec_pattern.match(line.strip())
            if m_spec:
                current_tag = m_spec.group(1)
                title = m_spec.group(2).strip()
                specs[current_tag] = {
                    "tag": current_tag,
                    "title": title,
                    "line": idx,
                    "targets": [],
                    "implementations": []
                }
                continue

            if current_tag:
                m_target = target_pattern.search(line)
                if m_target:
                    raw_targets = m_target.group(1).split(",")
                    for t in raw_targets:
                        cleaned = t.strip().strip("`").strip()
                        if cleaned:
                            specs[current_tag]["targets"].append(cleaned)
    return specs

def scan_codebase_impls(root_dir: Path) -> dict:
    """Scans all source code for // @impl [SPEC-*] tags."""
    scan_dirs = ["include", "targets", "apps", "examples", "sim", "rtl", "tools", "bsp", "src"]
    code_impls = {}
    impl_regex = re.compile(r"@impl\s+`?\[(SPEC-[A-Z0-9\-]+)\]`?\s*(.*)")
    IGNORED_TAGS = {"SPEC-XXX-YY", "SPEC-X", "SPEC-SAMPLE", "SPEC-TEMPLATE"}

    for s_dir in scan_dirs:
        dir_path = root_dir / s_dir
        if not dir_path.exists():
            continue
        for ext in ["*.hpp", "*.cpp", "*.h", "*.c", "*.S", "*.ld", "*.sv", "*.py"]:
            for f_path in dir_path.rglob(ext):
                rel_path = f_path.relative_to(root_dir)
                try:
                    with open(f_path, "r", encoding="utf-8", errors="ignore") as f:
                        lines = f.readlines()
                    for idx, line in enumerate(lines, 1):
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
                                    "loc": f"{rel_path}:{idx}"
                                })
                except Exception:
                    pass
    return code_impls

def check_sync_status(spec_file: Path, root_dir: Path):
    """Calculates bidirectional synchronization state between Spec and Code."""
    specs = parse_specification(spec_file)
    code_impls = scan_codebase_impls(root_dir)

    for tag, data in specs.items():
        if tag in code_impls:
            data["implementations"] = code_impls[tag]

    # Tags in spec missing in code
    unimplemented_in_code = [tag for tag, d in specs.items() if not d["implementations"]]
    
    # Tags in code missing in spec
    unregistered_in_spec = [tag for tag in code_impls if tag not in specs]

    # Synchronized tags
    synced = [tag for tag, d in specs.items() if d["implementations"]]

    return {
        "spec_file": spec_file,
        "specs": specs,
        "code_impls": code_impls,
        "synced": synced,
        "missing_in_code": unimplemented_in_code,
        "missing_in_spec": unregistered_in_spec
    }

def print_status_report(status: dict, root_dir: Path):
    """Renders a visual bidirectional status dashboard."""
    spec_rel = status["spec_file"].relative_to(root_dir)
    print("\n" + "=" * 80)
    print(f"{BOLD}             SpecTrace Bidirectional Round-Trip Sync Matrix{RESET}")
    print("=" * 80)
    print(f"Target Specification : {CYAN}{spec_rel}{RESET}")
    print(f"Total Requirements   : {len(status['specs'])}")
    print(f"Synchronized (<->)   : {GREEN}{len(status['synced'])}{RESET}")
    print(f"Missing in Code (->) : {YELLOW if status['missing_in_code'] else GREEN}{len(status['missing_in_code'])}{RESET}")
    print(f"Missing in Spec (<-) : {YELLOW if status['missing_in_spec'] else GREEN}{len(status['missing_in_spec'])}{RESET}")
    print("-" * 80)
    print(f"{'SPEC TAG':<20} | {'SYNC STATE':<18} | {'TARGET / CODE LOCATION':<38}")
    print("-" * 80)

    # 1. Print Synced
    for tag in sorted(status["synced"]):
        impls = status["specs"][tag]["implementations"]
        locs = ", ".join(i["loc"] for i in impls[:2])
        print(f"{GREEN}{tag:<20}{RESET} | {GREEN}{'<-> SYNCHRONIZED':<18}{RESET} | {locs:<38}")

    # 2. Print Missing in Code
    for tag in sorted(status["missing_in_code"]):
        spec_data = status["specs"][tag]
        target_str = ", ".join(spec_data["targets"]) if spec_data["targets"] else "No target specified"
        print(f"{YELLOW}{tag:<20}{RESET} | {YELLOW}{'-> MISSING IN CODE':<18}{RESET} | {target_str:<38}")

    # 3. Print Missing in Spec
    for tag in sorted(status["missing_in_spec"]):
        locs = ", ".join(i["loc"] for i in status["code_impls"][tag][:2])
        print(f"{CYAN}{tag:<20}{RESET} | {CYAN}{'<- UNREGISTERED':<18}{RESET} | {locs:<38}")

    print("=" * 80)

def forward_sync_to_code(status: dict, root_dir: Path, apply: bool = False):
    """Pushes unimplemented specification requirements into their designated code targets."""
    missing = status["missing_in_code"]
    if not missing:
        print(f"\n{GREEN}[SUCCESS] All specification requirements are already implemented in code!{RESET}")
        return 0

    print(f"\n{BOLD}[FORWARD SYNC: Spec -> Code]{RESET} Found {len(missing)} requirement(s) to push into code:")

    actions_taken = 0
    for tag in missing:
        spec_data = status["specs"][tag]
        targets = spec_data["targets"]
        title = spec_data["title"]

        if not targets:
            print(f"  {YELLOW}? [{tag}]{RESET} has no 'Implementation Target' declared in spec; skipping.")
            continue

        target_file_str = targets[0]
        target_path = root_dir / target_file_str
        if not target_path.exists():
            print(f"  {RED}! [{tag}]{RESET} Target file not found: {target_file_str}")
            continue

        comment_prefix = "//"
        if target_path.suffix in [".S", ".s", ".py"]:
            comment_prefix = "#"
        elif target_path.suffix in [".ld"]:
            comment_prefix = "/*"

        annotation = f"{comment_prefix} @impl [{tag}] {title}\n"
        if target_path.suffix == ".ld":
            annotation = f"/* @impl [{tag}] {title} */\n"

        print(f"  {CYAN}+ [{tag}]{RESET} -> Injecting into {BOLD}{target_file_str}{RESET}")

        if apply:
            try:
                with open(target_path, "r", encoding="utf-8") as f:
                    content = f.read()
                # Insert at top after comments or headers
                with open(target_path, "w", encoding="utf-8") as f:
                    f.write(annotation + content)
                actions_taken += 1
            except Exception as e:
                print(f"    {RED}Error writing {target_file_str}: {e}{RESET}")
        else:
            actions_taken += 1

    if apply:
        print(f"\n{GREEN}[SUCCESS] Injected {actions_taken} @impl annotations into source files!{RESET}")
    else:
        print(f"\n{YELLOW}[DRY-RUN] Would inject {actions_taken} @impl annotations. Run with --apply to commit.{RESET}")
    return actions_taken

def reverse_sync_to_spec(status: dict, root_dir: Path, apply: bool = False):
    """Pushes unmapped code implementations back into the specification markdown."""
    missing = status["missing_in_spec"]
    if not missing:
        print(f"\n{GREEN}[SUCCESS] No unmapped code implementations found. Spec is fully synchronized!{RESET}")
        return 0

    print(f"\n{BOLD}[REVERSE SYNC: Code -> Spec]{RESET} Found {len(missing)} unmapped implementation tag(s) to push into spec:")
    spec_file = status["spec_file"]

    synthesized_entries = []
    for tag in missing:
        impl_list = status["code_impls"][tag]
        first_impl = impl_list[0]
        title = first_impl["desc"] if first_impl["desc"] else f"Architectural Invariant for {tag}"
        files_str = ", ".join(f"`{i['file']}`" for i in impl_list)

        entry = f"""
### `[{tag}]` {title}
* **Requirement**: Implementation must adhere to freestanding C++20 non-blocking execution invariants.
* **Contract Detail**: Synthesized from active implementation in {files_str}.
* **Implementation Target**: {files_str}
"""
        synthesized_entries.append(entry.strip())
        print(f"  {CYAN}+ [{tag}]{RESET} -> Synthesizing requirement: {BOLD}{title}{RESET}")

    full_text = "\n\n" + "\n\n".join(synthesized_entries) + "\n"

    if apply:
        with open(spec_file, "a", encoding="utf-8") as f:
            f.write(full_text)
        print(f"\n{GREEN}[SUCCESS] Pushed {len(missing)} requirement(s) into {spec_file.relative_to(root_dir)}!{RESET}")
    else:
        print(f"\n{YELLOW}[DRY-RUN] Would append {len(missing)} requirement blocks to {spec_file.relative_to(root_dir)}. Run with --apply to commit.{RESET}")
    return len(missing)

def main():
    parser = argparse.ArgumentParser(
        description="SpecTrace: Bidirectional Round-Trip Synchronization & Verification Engine"
    )
    parser.add_argument("--spec", default=None, help="Path to specification markdown file")
    parser.add_argument("--status", action="store_true", help="Display bidirectional synchronization matrix")
    parser.add_argument("--drift", action="store_true", help="Audit repository-wide or module spec-to-code drift and unanchored code")
    parser.add_argument("--dedupe", action="store_true", help="Audit the specification markdown files for semantic and literal redundancy")
    parser.add_argument("--spec-review", action="store_true", help="Run shift-left review of the specification itself")
    parser.add_argument("--dashboard", "--trends", dest="dashboard", action="store_true",
                        help="Run SpecTrace Embedded Observability & Quality Dashboard")
    parser.add_argument("--to-code", action="store_true", help="Push unimplemented specs to code (Forward Sync)")
    parser.add_argument("--to-spec", action="store_true", help="Push unmapped code to specification (Reverse Sync)")
    parser.add_argument("--roundtrip", action="store_true", help="Execute full two-way round-trip synchronization")
    parser.add_argument("--apply", action="store_true", help="Apply modifications to files in place")
    parser.add_argument("--review", "--audit", dest="audit", action="store_true",
                        help="Run the canonical scoped SpecTrace code review (Stages 1-5)")
    parser.add_argument("--module", type=str, help="Module path to audit; default is the current diff")
    parser.add_argument("--related-path", action="append", default=[],
                        help="Related test/contract path (repeatable; requires --module)")
    parser.add_argument("--base-ref", type=str, help="Include committed changes since this ref")
    parser.add_argument("--all-modules", action="store_true", help="Explicitly sweep maintained module roots")
    parser.add_argument("--include-linux", action="store_true", help="Opt in to targets/linux")
    parser.add_argument("--no-ai-prompt", action="store_true", help="Run static checks without printing AI context")
    args = parser.parse_args()

    root_dir = get_root_dir()

    if args.dashboard:
        dashboard_tool = root_dir / "tools" / "track_quality_trends.py"
        cmd = [sys.executable, str(dashboard_tool)]
        result = subprocess.run(cmd, cwd=root_dir, check=False)
        sys.exit(result.returncode)

    if args.drift or args.spec_review or (args.status and not args.spec):
        drift_tool = root_dir / "tools" / "spectrace_drift.py"
        cmd = [sys.executable, str(drift_tool)]
        if args.spec:
            cmd.extend(["--spec", args.spec])
        if args.module:
            cmd.extend(["--module", args.module])
        if args.spec_review:
            cmd.append("--spec-review")
        result = subprocess.run(cmd, cwd=root_dir, check=False)
        sys.exit(result.returncode)

    if args.dedupe:
        dedupe_tool = root_dir / "tools" / "spectrace_dedupe.py"
        cmd = [sys.executable, str(dedupe_tool)]
        result = subprocess.run(cmd, cwd=root_dir, check=False)
        sys.exit(result.returncode)

    default_spec_rel = args.spec if args.spec else "docs/DESIGN_SPECIFICATION.md"
    spec_file = root_dir / default_spec_rel if not Path(default_spec_rel).is_absolute() else Path(default_spec_rel)

    if args.audit:
        if args.all_modules and args.module:
            parser.error("--all-modules and --module are mutually exclusive")
        if args.related_path and not args.module:
            parser.error("--related-path requires --module")
        if args.module and args.base_ref:
            parser.error("--module and --base-ref are mutually exclusive")

        audit_tool = root_dir / "tools" / "run_adversarial_audit.py"
        command = [sys.executable, str(audit_tool)]
        if args.module:
            command.extend(["--module", args.module])
        if args.all_modules:
            command.append("--all")
        if args.base_ref:
            command.extend(["--base-ref", args.base_ref])
        for related_path in args.related_path:
            command.extend(["--related-path", related_path])
        if args.include_linux:
            command.append("--include-linux")
        if not args.no_ai_prompt:
            command.append("--ai-prompt")

        # @impl [SPEC-AUDIT-05] docs/SPECTRACE.md
        result = subprocess.run(command, cwd=root_dir, check=False)
        sys.exit(result.returncode)

    if not spec_file.exists():
        print(f"{RED}[ERROR] Specification file not found: {spec_file}{RESET}")
        sys.exit(1)

    status = check_sync_status(spec_file, root_dir)

    if args.roundtrip:
        print(f"\n{BOLD}========================================================================{RESET}")
        print(f"{BOLD}           Executing SpecTrace Full Two-Way Round-Trip                  {RESET}")
        print(f"{BOLD}========================================================================{RESET}")
        # 1. Reverse Sync: Code -> Spec
        reverse_sync_to_spec(status, root_dir, apply=args.apply)
        # Refresh status after reverse sync
        status = check_sync_status(spec_file, root_dir)
        # 2. Forward Sync: Spec -> Code
        forward_sync_to_code(status, root_dir, apply=args.apply)
        # Final status
        status = check_sync_status(spec_file, root_dir)
        print_status_report(status, root_dir)
        return

    if args.to_code:
        forward_sync_to_code(status, root_dir, apply=args.apply)
        return

    if args.to_spec:
        reverse_sync_to_spec(status, root_dir, apply=args.apply)
        return

    # Default action: status
    print_status_report(status, root_dir)

if __name__ == "__main__":
    main()
