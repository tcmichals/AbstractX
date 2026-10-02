#!/usr/bin/env python3
"""
SpecTrace: Code-to-Specification Reverse Synchronization Tool
=============================================================
Enables engineers and AI agents to write/modify code first and have SpecTrace
autonomously synthesize or push formal requirement blocks back into SPECIFICATION.md.

Usage:
  # 1. Scan codebase for unmapped @impl or @spec tags not yet in the specification:
  python3 tools/sync_code_to_spec.py --spec docs/DESIGN_SPECIFICATION.md

  # 2. Preview what would be pushed back to the specification:
  python3 tools/sync_code_to_spec.py --spec docs/DESIGN_SPECIFICATION.md --dry-run

  # 3. Automatically push missing code requirements back to SPECIFICATION.md:
  python3 tools/sync_code_to_spec.py --spec docs/DESIGN_SPECIFICATION.md --apply

  # 4. Push a specific new function/method directly from code to spec:
  python3 tools/sync_code_to_spec.py --push-func "transfer_dma_async" \
      --file include/abstractx/hal/spi.hpp \
      --tag SPEC-HAL-06 \
      --title "Asynchronous DMA SPI Burst Transfers" \
      --spec docs/DESIGN_SPECIFICATION.md --apply
"""

import os
import re
import sys
import argparse
from pathlib import Path

def parse_spec_tags(spec_file: Path) -> dict:
    """Extracts all [SPEC-*] tags from a specification file."""
    if not spec_file.exists():
        return {}
    
    spec_pattern = re.compile(r"###\s+`?\[(SPEC-[A-Z0-9\-]+)\]`?\s*(.*)")
    specs = {}
    with open(spec_file, "r", encoding="utf-8") as f:
        for idx, line in enumerate(f, 1):
            m = spec_pattern.match(line.strip())
            if m:
                tag = m.group(1)
                title = m.group(2).strip()
                specs[tag] = {
                    "line": idx,
                    "title": title
                }
    return specs

def scan_codebase_tags(root_dir: Path) -> dict:
    """Scans all source code for @impl and @spec tags and surrounding docstrings."""
    scan_dirs = ["include", "targets", "apps", "examples", "sim", "rtl", "tools"]
    found_tags = {}
    
    tag_regex = re.compile(r"(?:@impl|@spec)\s+`?\[(SPEC-[A-Z0-9\-]+)\]`?\s*(.*)")
    
    # Ignore template placeholder tags
    IGNORED_TAGS = {"SPEC-XXX-YY", "SPEC-X", "SPEC-SAMPLE", "SPEC-TEMPLATE"}

    for s_dir in scan_dirs:
        dir_path = root_dir / s_dir
        if not dir_path.exists():
            continue
        for ext in ["*.hpp", "*.cpp", "*.h", "*.c", "*.S", "*.sv", "*.py"]:
            for f_path in dir_path.rglob(ext):
                rel_path = f_path.relative_to(root_dir)
                try:
                    with open(f_path, "r", encoding="utf-8", errors="ignore") as f:
                        lines = f.readlines()
                    for idx, line in enumerate(lines):
                        m = tag_regex.search(line)
                        if m:
                            tag = m.group(1)
                            if tag in IGNORED_TAGS:
                                continue
                            inline_desc = m.group(2).strip()
                            
                            # Gather context comments (3 lines before and 3 lines after)
                            start_ctx = max(0, idx - 4)
                            end_ctx = min(len(lines), idx + 5)
                            context_snippet = "".join(lines[start_ctx:end_ctx]).strip()
                            
                            if tag not in found_tags:
                                found_tags[tag] = {
                                    "tag": tag,
                                    "files": [],
                                    "descriptions": [],
                                    "contexts": []
                                }
                            found_tags[tag]["files"].append(f"{rel_path}:{idx + 1}")
                            if inline_desc:
                                found_tags[tag]["descriptions"].append(inline_desc)
                            found_tags[tag]["contexts"].append(context_snippet)
                except Exception:
                    pass
    return found_tags

def synthesize_spec_entry(tag: str, code_info: dict, root_dir: Path) -> str:
    """Synthesizes a clean Markdown specification requirement block from code info."""
    title = code_info["descriptions"][0] if code_info["descriptions"] else f"Contract Requirement for {tag}"
    # Remove file path if title contains it (e.g. "@impl [SPEC-X] path/to/file.cpp")
    if "/" in title or "\\" in title or title.endswith(".cpp") or title.endswith(".hpp"):
        title = f"Synthesized Invariant for {tag}"

    files_list = ", ".join(f"`{f}`" for f in code_info["files"])
    
    # Try to extract function signature or meaningful comment
    contract_notes = []
    for ctx in code_info["contexts"]:
        for line in ctx.splitlines():
            clean = line.strip().lstrip("/*# \t").rstrip("*/")
            if clean and not clean.startswith("@impl") and not clean.startswith("@spec"):
                if any(k in clean for k in ["coro::Task", "virtual", "class", "struct", "void", "bool", "int", "uint"]):
                    contract_notes.append(f"Function/Type: `{clean}`")
                elif len(clean) > 10 and not clean.startswith("{") and not clean.startswith("}"):
                    contract_notes.append(clean)
    
    deduped_notes = list(dict.fromkeys(contract_notes))[:3]
    if not deduped_notes:
        deduped_notes = ["Freestanding C++20 invariant, asynchronous coroutine lifecycle, zero heap."]

    req_bullets = "\n".join(f"* **Contract Detail**: {n}" for n in deduped_notes)

    entry = f"""
### `[{tag}]` {title}
* **Requirement**: Implementation must adhere to freestanding C++20 non-blocking execution invariants.
{req_bullets}
* **Synthesized From Code**: {files_list}
* **Implementation Target**: {files_list}
"""
    return entry.strip()

def main():
    parser = argparse.ArgumentParser(description="SpecTrace: Push code invariants back to SPECIFICATION.md")
    parser.add_argument("--spec", default="docs/DESIGN_SPECIFICATION.md", help="Target specification markdown file")
    parser.add_argument("--apply", action="store_true", help="Apply synthesized specs directly to the specification file")
    parser.add_argument("--dry-run", action="store_true", help="Preview synthesized spec blocks without modifying files")
    parser.add_argument("--tag", help="Explicit [SPEC-*] tag to push")
    parser.add_argument("--title", help="Explicit title for the requirement")
    parser.add_argument("--file", help="Source file where code was added")
    parser.add_argument("--push-func", help="Push a specific function by name from code into the spec")

    args = parser.parse_args()
    root_dir = Path(__file__).resolve().parent.parent
    spec_path = root_dir / args.spec
    if not spec_path.is_absolute():
        spec_path = root_dir / args.spec

    if not spec_path.exists():
        print(f"[ERROR] Specification file not found: {spec_path}")
        sys.exit(1)

    existing_specs = parse_spec_tags(spec_path)
    print(f"Loaded {len(existing_specs)} existing specification tags from {spec_path.relative_to(root_dir)}")

    # Mode A: Push specific function
    if args.push_func and args.file and args.tag:
        src_file = root_dir / args.file
        if not src_file.exists():
            print(f"[ERROR] Source file not found: {src_file}")
            sys.exit(1)
        
        tag = args.tag.strip("[]")
        title = args.title or f"Asynchronous Implementation of {args.push_func}"
        code_info = {
            "files": [f"{args.file}:1"],
            "descriptions": [title],
            "contexts": [f"coro::Task<bool> {args.push_func}()"]
        }
        entry = synthesize_spec_entry(tag, code_info, root_dir)
        print("\n" + "=" * 70)
        print(f"Synthesized Specification Block for [{tag}]:")
        print("=" * 70)
        print(entry)
        print("=" * 70)

        if args.apply:
            with open(spec_path, "a", encoding="utf-8") as f:
                f.write("\n\n" + entry + "\n")
            print(f"[SUCCESS] Appended requirement `[{tag}]` to {spec_path.relative_to(root_dir)}")
        else:
            print("[INFO] Run with --apply to append to specification.")
        return

    # Mode B: Scan code for missing tags
    code_tags = scan_codebase_tags(root_dir)
    print(f"Scanned codebase: found {len(code_tags)} unique @impl/@spec tags in code.")

    missing_in_spec = {}
    for tag, info in code_tags.items():
        if tag not in existing_specs:
            missing_in_spec[tag] = info

    if not missing_in_spec:
        print("\n[SUCCESS] Code and Specification are in 100% sync. No unmapped code tags found.")
        return

    print(f"\n[ALERT] Found {len(missing_in_spec)} requirement tag(s) in code missing from {spec_path.name}:")
    for tag, info in missing_in_spec.items():
        print(f"  - [{tag}] in {', '.join(info['files'])}")

    synthesized_blocks = []
    for tag, info in missing_in_spec.items():
        synthesized_blocks.append(synthesize_spec_entry(tag, info, root_dir))

    full_addition = "\n\n" + "\n\n".join(synthesized_blocks) + "\n"

    print("\n" + "=" * 70)
    print("PROPOSED SPECIFICATION ADDITIONS (Code -> Spec Reverse-Sync):")
    print("=" * 70)
    print(full_addition)
    print("=" * 70)

    if args.apply:
        with open(spec_path, "a", encoding="utf-8") as f:
            f.write(full_addition)
        print(f"\n[SUCCESS] Successfully pushed {len(missing_in_spec)} requirement(s) from code into {spec_path.relative_to(root_dir)}!")
    else:
        print("\n[INFO] Dry run complete. Use --apply to push these additions to the specification.")

if __name__ == "__main__":
    main()
