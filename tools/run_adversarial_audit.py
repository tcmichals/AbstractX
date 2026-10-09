#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
AbstractX Static Invariant & Specification Auditor (run_adversarial_audit.py)
=============================================================================
Automated Static Analysis & Invariant Auditor enforcing core AbstractX
architectural contracts across Markdown specifications and C++/RTL source code:

1. Stage 0: Specification Markdown & SSOT Traceability ([SPEC-*] tags, Mermaid, spec-to-code parity).
2. Stage 1: Zero-Heap Allocation & Freestanding Environment (No malloc/new/heap).
3. Stage 2: Non-Blocking HAL & Cooperative Coroutine Lifecycle (No spin-sleeps in tasks).
4. Stage 3: ISR Boundary & SPSC Dispatch Safety (No coroutine .resume() in ISRs).
5. Stage 4: Wire Framing & Endianness Consistency (64B TLP alignment and layout).
6. Stage 5: Comprehensive Test Verification (CppUTest SITL & Cocotb RTL suites).
"""

import os
import re
import sys
import argparse
import subprocess
from datetime import datetime
from pathlib import Path

RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
BLUE = "\033[94m"
BOLD = "\033[1m"
RESET = "\033[0m"

# @impl [SPEC-AUDIT-01] [SPEC-AUDIT-02] [SPEC-AUDIT-03] [SPEC-AUDIT-04] docs/SPECTRACE.md
AUDIT_SUFFIXES = {".hpp", ".h", ".cpp", ".c", ".S", ".sv", ".md", ".py"}
CODE_SUFFIXES = {".hpp", ".h", ".cpp", ".c", ".S", ".sv", ".py"}
AUDIT_ROOTS = ("include", "apps", "targets", "docs", "rtl", "hw", "sim", "examples", "src", "tools")
GENERATED_DIRS = {".git", ".venv", "venv", "third_party", "node_modules", "__pycache__", "build", "dist", "out"}


def _is_generated_path(path: Path, root_dir: Path) -> bool:
    try:
        parts = path.relative_to(root_dir).parts
    except ValueError:
        return True
    return any(
        part in GENERATED_DIRS or part.startswith(("build_", "bld."))
        for part in parts
    )


def _is_linux_target(path: Path, root_dir: Path) -> bool:
    try:
        parts = path.relative_to(root_dir).parts
    except ValueError:
        return False
    return len(parts) >= 2 and parts[0] == "targets" and parts[1] == "linux"


def collect_audit_files(root_dir: Path, candidates, include_linux: bool = False) -> list:
    """Collect maintained audit inputs while pruning generated/vendor trees."""
    files = set()
    for candidate in candidates:
        candidate_path = Path(candidate)
        path = candidate_path if candidate_path.is_absolute() else root_dir / candidate_path
        if not path.exists():
            continue
        if path.is_file():
            if path.suffix in AUDIT_SUFFIXES and not _is_generated_path(path, root_dir):
                if include_linux or not _is_linux_target(path, root_dir):
                    files.add(path.resolve())
            continue

        for current, dirs, filenames in os.walk(path):
            current_path = Path(current)
            dirs[:] = [
                name for name in dirs
                if not _is_generated_path(current_path / name, root_dir)
                and (include_linux or not _is_linux_target(current_path / name, root_dir))
            ]
            for filename in filenames:
                file_path = current_path / filename
                if file_path.suffix not in AUDIT_SUFFIXES or _is_generated_path(file_path, root_dir):
                    continue
                if include_linux or not _is_linux_target(file_path, root_dir):
                    files.add(file_path.resolve())
    return sorted(files)


def collect_changed_files(root_dir: Path, base_ref: str | None = None,
                          include_linux: bool = False) -> list:
    """Collect committed branch changes plus staged, unstaged, and untracked files."""
    changed = set()

    def add_git_paths(args):
        result = subprocess.run(args, cwd=root_dir, capture_output=True, check=True)
        changed.update(root_dir / item.decode("utf-8", errors="surrogateescape")
                       for item in result.stdout.split(b"\0") if item)

    if base_ref:
        add_git_paths(["git", "diff", "--name-only", "-z", f"{base_ref}...HEAD"])
    add_git_paths(["git", "diff", "--name-only", "-z", "HEAD"])
    add_git_paths(["git", "ls-files", "--others", "--exclude-standard", "-z"])
    return collect_audit_files(root_dir, changed, include_linux=include_linux)


def emit_ai_review_prompt(files: list, root_dir: Path, base_ref: str | None = None) -> None:
    """Print a compact, paste-ready AI review instruction for selected files."""
    print("AI review scope (open only these files and named dependencies):")
    for path in files:
        print(f"  - {path.relative_to(root_dir)}")
    print("""
Review this module as an adversarial senior engineer. Read its SPECIFICATION.md
or linked authoritative contract before implementation and tests. Restrict findings to selected files and directly
used interfaces; do not audit unrelated Linux host or generated Buildroot code.
For each concrete finding, report severity, file:line, violated requirement, and
failure scenario. Distinguish verified defects from questions/false positives.
Do not change code until the relevant specification is updated. Identify the
focused build/test command for this module; static checks alone do not prove
correctness.""")
    if base_ref:
        print(f"Committed changes compared against: {base_ref}")


class InvariantAuditor:
    # region Auditor lifecycle and traceability index
    def __init__(self, root_dir: Path):
        self.root_dir = root_dir
        self.violations = {
            "Stage 0 (Specification Markdown & SSOT)": [],
            "Stage 1 (Zero-Heap & Freestanding C++20)": [],
            "Stage 2 (Non-Blocking HAL & Lifecycle)": [],
            "Stage 3 (ISR Boundary & Dispatch Safety)": [],
            "Stage 4 (Endianness & Wire Framing)": [],
            "Stage 5 (Test Verification)": []
        }
        self._code_impl_tags = None

    def log_issue(self, stage: str, filepath: Path, line_no: int, rule: str, snippet: str):
        rel_path = filepath.relative_to(self.root_dir) if filepath.is_relative_to(self.root_dir) else filepath
        self.violations[stage].append({
            "file": str(rel_path),
            "line": line_no,
            "rule": rule,
            "snippet": snippet.strip()
        })

    def get_code_impl_tags(self) -> set:
        """Scan codebase once for all active // @impl [SPEC-*] tags"""
        if self._code_impl_tags is not None:
            return self._code_impl_tags
        self._code_impl_tags = set()
        sources = collect_audit_files(
            self.root_dir,
            [self.root_dir / d for d in AUDIT_ROOTS if d != "docs"],
        )
        for source in sources:
            if source.suffix not in CODE_SUFFIXES:
                continue
            try:
                with open(source, "r", encoding="utf-8", errors="ignore") as fp:
                    for line in fp:
                        if "@impl" in line:
                            self._code_impl_tags.update(re.findall(r"\[(SPEC-[A-Z0-9\-]+)\]", line))
            except Exception:
                pass
        return self._code_impl_tags
    # endregion

    # region Stage 0: Specification SSOT and traceability
    def audit_stage0_spec_markdown(self, filepath: Path, lines: list):
        """Stage 0: Verify Markdown specification contracts, invariants, and code traceability"""
        is_spec_doc = ("SPECIFICATION.md" in filepath.name or 
                       "DESIGN_SPECIFICATION.md" in filepath.name or 
                       filepath.name.startswith("SPEC_"))
        if not is_spec_doc:
            return

        spec_tags = []
        has_mermaid = False

        for idx, line in enumerate(lines, 1):
            sline = line.strip()
            if "```mermaid" in sline:
                has_mermaid = True

            m = re.search(r"###\s+`?\[(SPEC-[A-Z0-9\-]+)\]`?", line)
            if m:
                tag = m.group(1)
                spec_tags.append((tag, idx))

            # Flag if the specification prescribes blocking synchronous transfers
            if re.search(r"\b(transfer_sync|read_sync|write_sync)\b", line):
                if not any(neg in line.lower() for neg in ["prohibit", "forbidden", "banned", "not permit", "zero", "no "]):
                    self.log_issue("Stage 0 (Specification Markdown & SSOT)", filepath, idx,
                                   "Spec prescribes synchronous blocking HAL methods, violating Rule 4", line)

            # Flag if the specification recommends raw dynamic heap allocation
            if re.search(r"\b(malloc\(|operator new|std::vector)\b", line):
                if not any(neg in line.lower() for neg in ["prohibit", "forbidden", "banned", "not permit", "zero", "no "]):
                    self.log_issue("Stage 0 (Specification Markdown & SSOT)", filepath, idx,
                                   "Spec permits raw dynamic heap allocation, violating Rule 2", line)

        if not has_mermaid:
            self.log_issue("Stage 0 (Specification Markdown & SSOT)", filepath, 1,
                           "Missing Mermaid architecture or sequence diagram in specification document", "")

        if spec_tags:
            code_impls = self.get_code_impl_tags()
            for tag, line_no in spec_tags:
                if tag not in code_impls:
                    self.log_issue("Stage 0 (Specification Markdown & SSOT)", filepath, line_no,
                                   f"Unimplemented requirement [{tag}] in markdown specification (Missing matching // @impl tag in code)", "")
    # endregion

    # region Stage 1: Freestanding and zero-heap checks
    def audit_stage1_zero_heap(self, filepath: Path, lines: list):
        """Stage 1: Verify zero dynamic heap allocation in freestanding code"""
        if "third_party/" in str(filepath) or "tests/" in str(filepath) or "build_" in str(filepath):
            return

        forbidden_headers = [
            (r"<iostream>", "Forbidden hosted standard header <iostream> violates Rule 1"),
            (r"<vector>", "Forbidden dynamic container <vector>; use etl::vector or static array (Rule 2)"),
            (r"<string>", "Forbidden dynamic container <string>; use etl::string or string_view (Rule 2)"),
            (r"<memory>", "Forbidden <memory> in freestanding; use placement new or static memory"),
        ]

        for idx, line in enumerate(lines, 1):
            sline = line.strip()
            if sline.startswith("//") or sline.startswith("*") or sline.startswith("/*"):
                continue

            for pattern, desc in forbidden_headers:
                if re.search(pattern, line):
                    self.log_issue("Stage 1 (Zero-Heap & Freestanding C++20)", filepath, idx, desc, line)

            alloc_patterns = [
                (r"\bmalloc\s*\(", "Forbidden raw malloc() violates Rule 2"),
                (r"\bcalloc\s*\(", "Forbidden raw calloc() violates Rule 2"),
                (r"\bfree\s*\(", "Forbidden raw free() violates Rule 2"),
                (r"\bnew\s+[A-Za-z0-9_:]+\s*(\[|\()", "Dynamic heap new expression violates Rule 2"),
            ]
            for pattern, desc in alloc_patterns:
                if re.search(pattern, line):
                    if "new (" not in line and "operator new" not in line and "placement" not in line.lower():
                        self.log_issue("Stage 1 (Zero-Heap & Freestanding C++20)", filepath, idx, desc, line)
    # endregion

    # region Stage 2: Non-blocking HAL checks
    def audit_stage2_non_blocking_hal(self, filepath: Path, lines: list):
        """Stage 2: Verify zero synchronous blocking calls in active drivers and tasks"""
        if "third_party/" in str(filepath) or "tests/" in str(filepath) or "build_" in str(filepath):
            return

        blocking_calls = [
            (r"\busleep\s*\(", "Blocking usleep() violates Rule 12; use co_await timer"),
            (r"\bsleep\s*\([0-9]+\)", "Blocking sleep() violates Rule 12; use co_await timer"),
        ]

        is_impl = filepath.suffix == ".cpp" or "driver" in str(filepath) or "app" in str(filepath)
        if not is_impl:
            return

        for idx, line in enumerate(lines, 1):
            sline = line.strip()
            if sline.startswith("//") or sline.startswith("*") or sline.startswith("/*"):
                continue

            for pattern, desc in blocking_calls:
                if re.search(pattern, line):
                    if "sync_impl" in line or "pre-scheduler" in line.lower() or "fallback" in line.lower():
                        continue
                    self.log_issue("Stage 2 (Non-Blocking HAL & Lifecycle)", filepath, idx, desc, line)
    # endregion

    # region Stage 3: ISR boundary checks
    def audit_stage3_isr_boundary(self, filepath: Path, lines: list):
        """Stage 3: Verify no coroutine .resume() directly inside ISRs"""
        if "third_party/" in str(filepath) or "tests/" in str(filepath) or "build_" in str(filepath):
            return

        in_isr_func = False
        isr_sig = re.compile(r"\bvoid\s+[A-Za-z0-9_]*(isr|interrupt|irq|handler)[A-Za-z0-9_]*\s*\(", re.IGNORECASE)

        for idx, line in enumerate(lines, 1):
            sline = line.strip()
            if sline.startswith("//") or sline.startswith("*") or sline.startswith("/*"):
                continue

            if isr_sig.search(line):
                in_isr_func = True

            if in_isr_func and line.startswith("}"):
                in_isr_func = False

            if in_isr_func and re.search(r"\b[A-Za-z0-9_]+\.resume\s*\(\)", line):
                self.log_issue("Stage 3 (ISR Boundary & Dispatch Safety)", filepath, idx,
                               "Direct .resume() from ISR context violates Rule 4.2", line)
    # endregion

    # region Stage 4: Wire framing checks
    def audit_stage4_wire_framing_endianness(self, filepath: Path, lines: list):
        """Stage 4: Verify wire packet packing, 64B alignment, and payload scaling invariants"""
        if filepath.name in ["asp_tlp64.h", "asp_tlp64.hpp"]:
            has_static_assert = False
            for idx, line in enumerate(lines, 1):
                if "static_assert" in line and "64" in line:
                    has_static_assert = True

            if not has_static_assert and filepath.suffix in [".hpp", ".h"]:
                self.log_issue("Stage 4 (Endianness & Wire Framing)", filepath, 1,
                               "Missing compile-time static_assert(sizeof(...) == 64) for wire protocol", "")
            return

        # For driver and application C++ files, audit fixed 64B TLP payload scaling boundaries
        if filepath.suffix in [".cpp", ".hpp"]:
            for idx, line in enumerate(lines, 1):
                sline = line.strip()
                if sline.startswith("//") or sline.startswith("*"):
                    continue
                # Flag unsegmented bulk buffer transfers exceeding 40B payload limit
                if re.search(r"\bTlp64::make_raw\s*\([^)]*,\s*[^)]*(?:5[0-9]|[6-9][0-9]|[1-9][0-9]{2,})\b", line):
                    self.log_issue("Stage 4 (Endianness & Wire Framing)", filepath, idx,
                                   "Fixed 64B TLP payload overflow: buffer exceeds 40B container without segmentation or DMA burst bypass", line)
    # endregion

    # region Stage 5: Verification gate
    def audit_stage5_test_coverage(self):
        """Stage 5: Verify companion test suites exist"""
        test_dir = self.root_dir / "tests"
        sim_dir = self.root_dir / "sim" / "cocotb"
        if not (test_dir.exists() and sim_dir.exists()):
            self.violations["Stage 5 (Test Verification)"].append({
                "file": "CMakeLists.txt",
                "line": 1,
                "rule": "Missing test directories",
                "snippet": "Both tests/ and sim/cocotb/ must exist"
            })
    # endregion

    # region Audit execution and report
    def run_audit(self, target_files: list):
        for f in target_files:
            if not f.exists() or f.is_dir():
                continue
            if f.suffix not in [".hpp", ".h", ".cpp", ".c", ".sv", ".md"]:
                continue

            try:
                with open(f, "r", encoding="utf-8", errors="ignore") as fp:
                    lines = fp.readlines()
            except Exception:
                continue

            if f.suffix == ".md":
                self.audit_stage0_spec_markdown(f, lines)
            elif f.suffix in CODE_SUFFIXES - {".py"}:
                self.audit_stage1_zero_heap(f, lines)
                self.audit_stage2_non_blocking_hal(f, lines)
                self.audit_stage3_isr_boundary(f, lines)
                self.audit_stage4_wire_framing_endianness(f, lines)

        self.audit_stage5_test_coverage()

    def print_report(self) -> int:
        print(f"\n{BOLD}{BLUE}======================================================================={RESET}")
        print(f"{BOLD}{BLUE}         AbstractX Static Invariant & Spec Audit Report                {RESET}")
        print(f"{BOLD}{BLUE}======================================================================={RESET}\n")

        total_issues = 0
        for stage, issues in self.violations.items():
            count = len(issues)
            total_issues += count
            if count == 0:
                print(f"{GREEN}[PASS] {stage}: 0 issues found{RESET}")
            else:
                print(f"{RED}[FAIL] {stage}: {count} issue(s) detected{RESET}")
                for iss in issues:
                    print(f"  {YELLOW}-> {iss['file']}:{iss['line']}{RESET} [{iss['rule']}]")
                    if iss["snippet"]:
                        print(f"     Code: {iss['snippet']}")

        print(f"\n{BOLD}{BLUE}======================================================================={RESET}")
        if total_issues == 0:
            print(f"{BOLD}{GREEN}VERDICT: [PASS FOR COMMIT] (0 Issues){RESET}")
            print(f"{BOLD}{BLUE}======================================================================={RESET}\n")
            return 0
        else:
            print(f"{BOLD}{RED}VERDICT: [INVARIANT VIOLATIONS DETECTED] ({total_issues} Issues){RESET}")
            print(f"{BOLD}{BLUE}======================================================================={RESET}\n")
            return 1
    # endregion

    def generate_engineering_log_entry(self, target_files: list) -> str:
        today_str = datetime.now().strftime("%Y-%m-%d")
        file_list_str = "\n".join([f"* `{f.relative_to(self.root_dir) if f.is_relative_to(self.root_dir) else f}`" for f in target_files[:8]])

        log_content = f"""
## [{today_str}] - Mistake & Lesson: <Title>

### 1. Mistake / Bug Observed
* **Target Subsystem / Files**:
{file_list_str}
* **Symptoms**: <What failed, hung, crashed, or drifted?>

### 2. Root Cause & Lesson Learned
* **Why it happened**: <Technical explanation of failure or incorrect assumption>
* **Architectural Lesson**: <Which invariant was missed or underspecified?>

### 3. Specification Update (SSOT Defense)
* **Target Spec**: `targets/SPECIFICATION.md` or `apps/<app>/SPECIFICATION.md`
* **Requirement Tag Added / Updated**: `[SPEC-XXX-YY]`
* **Rule Added to Spec**:
  > <Exact requirement contract added so this mistake never repeats>

### 4. Implementation & Verification
* **Code Changes**: Tagged with `// @impl [SPEC-XXX-YY]`
* **Regression Test**: Verified via CTest (`sim/` or `tests/`)
* **Traceability Audit**: `python3 tools/audit_specs.py` -> 100% verified
"""
        return log_content

ADVERSARIAL_PROMPTS = """
=======================================================================
       AbstractX Anti-Drift Adversarial Review Prompt Templates
=======================================================================

--- STAGE 0: Specification Contract & Anti-Drift Prompt ---
"Audit the attached SPECIFICATION.md against AbstractX Invariants to prevent drift:
1. Does every functional requirement have a unique [SPEC-XXX-YY] tag?
2. Are all declared [SPEC-*] requirements implemented in code with matching // @impl tags (Zero Spec-Code Drift)?
3. Does the spec explicitly mandate freestanding C++20 (0 B dynamic heap, no malloc/new)?
4. Does the spec mandate asynchronous awaitable HAL interfaces (zero synchronous blocking calls)?
5. For multi-rate sensors, does the spec mandate the Primary-Paced Coroutine Channel pattern?
6. Are wire packets explicitly defined as 64-byte TLPs with endianness rules?
7. Are all error paths, teardown lifecycles, and cancellation states formally defined?
8. Does the spec include a Mermaid sequence/dataflow diagram?"

--- STAGE 1: Freestanding C++20 & Hardirq Concurrency Prompt ---
"Audit the attached diff/code against AbstractX Freestanding Invariants:
1. Are there ANY dynamic memory allocations (operator new, malloc, calloc)?
2. Are there any forbidden hosted headers (<iostream>, <vector>, <string>, <thread>, <mutex>)?
3. Does any ISR or worker thread invoke coroutine .resume() directly?
4. Are multi-core or ISR queues protected with lock-free atomic release/acquire memory barriers?"

--- STAGE 2: Non-Blocking HAL & Coroutine Lifecycle Prompt ---
"Audit the attached diff/code against AbstractX Async Lifecycle Invariants:
1. Are there any blocking synchronous functions (usleep, delay_ms, spin-polling)?
2. Do all driver lifecycles return coro::Task<bool> or coro::Task<void>?
3. Is teardown strictly symmetric and cancel-safe?
4. Are hardware clocks and peripherals enabled before MMIO access and kept alive during shutdown?"

--- STAGE 3: Subsystem Contracts & Primary-Paced Channel Prompt ---
"Audit the attached diff/code against AbstractX Sensor Ingestion Invariants:
1. Is the multi-rate loop driven strictly by the primary sensor (co_await g_imu_channel.pop())?
2. Are auxiliary sensors drained non-blockingly via while (aux_channel.try_pop())?
3. Are tick modulus prescalers (counter % N == 0) or nested state machines completely absent?"

--- STAGE 4: Hardware Interconnect, TLP Framing & Endianness Prompt ---
"Audit the attached diff/code against AbstractX Hardware Wire Invariants:
1. Does the 64-byte TLP struct have a compile-time static_assert(sizeof(...) == 64)?
2. Are MMIO posted register writes flushed via a readl() read-back before reset deassertions?
3. Are Big-Endian wire formats cleanly transformed to host native-endian representations?"

--- STAGE 5: Adversarial Gatekeeper & Anti-Drift Directives ---
"Consolidate findings across BOTH Markdown Specifications and Code.
Discard false positives. For every genuine defect, race hazard, or specification gap:
1. Formulate the exact Markdown requirement tag ([SPEC-XXX-YY]) and contract text.
2. Direct the developer/AI to update SPECIFICATION.md FIRST before modifying code.
3. Classify issue severity: [Critical], [High], [Medium]."
"""

# region CLI: scope selection, AI context, and execution
def main():
    parser = argparse.ArgumentParser(description="AbstractX Invariant & Specification Auditor")
    parser.add_argument("--all", action="store_true", help="Explicitly audit maintained module roots (Linux and generated output excluded)")
    parser.add_argument("--path", "--module", dest="path", type=str, default=None,
                        help="Audit only this module/file path; selecting targets/linux opts Linux in")
    parser.add_argument("--related-path", action="append", default=[],
                        help="Add an associated test or shared-interface path to a module audit (repeatable)")
    parser.add_argument("--git-diff", action="store_true",
                        help="Compatibility alias for auditing current staged/unstaged/untracked changes")
    parser.add_argument("--base-ref", type=str, default=None,
                        help="Also audit committed changes from this ref to HEAD, e.g. origin/main")
    parser.add_argument("--include-linux", action="store_true",
                        help="Include targets/linux in --all or changed-file scope")
    parser.add_argument("--ai-prompt", action="store_true",
                        help="Print a bounded AI review prompt and selected file manifest")
    parser.add_argument("--kickoff", action="store_true", help="Generate engineering_log.md template entry")
    parser.add_argument("--append", action="store_true", help="Append generated kickoff entry to engineering_log.md")
    parser.add_argument("--prompts", action="store_true", help="Display the 6 adversarial review prompt templates")
    args = parser.parse_args()

    root_dir = Path(__file__).resolve().parent.parent

    if args.prompts:
        print(ADVERSARIAL_PROMPTS)
        sys.exit(0)

    if args.all and (args.path or args.related_path):
        parser.error("--all cannot be combined with --path/--module or --related-path")
    if args.related_path and not args.path:
        parser.error("--related-path requires --path/--module")
    if args.path and (args.base_ref or args.git_diff):
        parser.error("--path/--module cannot be combined with git-diff scope options")

    if args.path:
        module_path = Path(args.path)
        if not module_path.is_absolute():
            module_path = root_dir / module_path
        related_paths = [Path(path) for path in args.related_path]
        related_paths = [path if path.is_absolute() else root_dir / path for path in related_paths]
        selected_paths = [module_path, *related_paths]
        include_linux = args.include_linux or any(
            _is_linux_target(path, root_dir) for path in selected_paths
        )
        target_files = collect_audit_files(
            root_dir,
            selected_paths,
            include_linux=include_linux,
        )
    elif args.all:
        target_files = collect_audit_files(
            root_dir,
            [root_dir / name for name in AUDIT_ROOTS],
            include_linux=args.include_linux,
        )
    else:
        try:
            target_files = collect_changed_files(
                root_dir, base_ref=args.base_ref, include_linux=args.include_linux
            )
        except subprocess.CalledProcessError as exc:
            print(f"Unable to collect audit scope: {exc}", file=sys.stderr)
            sys.exit(2)

    if args.ai_prompt:
        emit_ai_review_prompt(target_files, root_dir, base_ref=args.base_ref)

    if not target_files:
        print("No maintained files in the selected scope. Use --module <path> or --all for an explicit audit.")
        sys.exit(0)

    auditor = InvariantAuditor(root_dir)
    auditor.run_audit(target_files)

    if args.kickoff:
        report = auditor.generate_engineering_log_entry(target_files)
        if args.append:
            log_file = root_dir / "engineering_log.md"
            with open(log_file, "a", encoding="utf-8") as f:
                f.write(report)
            print(f"{GREEN}[SUCCESS] Appended new lesson scaffold to engineering_log.md{RESET}")
        else:
            print(f"\n{BOLD}{BLUE}======================================================================={RESET}")
            print(f"{BOLD}{GREEN}  Generated Engineering Log Entry for engineering_log.md:              {RESET}")
            print(f"{BOLD}{BLUE}======================================================================={RESET}\n")
            print(report)

    sys.exit(auditor.print_report())
 # endregion

if __name__ == "__main__":
    main()

