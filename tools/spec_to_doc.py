#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-ARCH-01] docs/DESIGN_SPECIFICATION.md
"""
AbstractX Markdown Documentation Generator (spec_to_doc.py)
------------------------------------------------------------
Generates user-friendly developer guides, READMEs, and architecture references
directly from canonical SPECIFICATION.md files (SSOT).

Enforces Markdown-Drives-Code (SSOT) and prevents drift between formal
aerospace requirements and human-readable documentation.

Usage:
  # Generate a developer guide from an application specification:
  python3 tools/spec_to_doc.py --spec apps/e906_repl_app/SPECIFICATION.md --output apps/e906_repl_app/GUIDE.md

  # Generate README summary:
  python3 tools/spec_to_doc.py --spec targets/allwinner_e906/SPECIFICATION.md --output targets/allwinner_e906/README.md --format readme

  # Check mode for CI (fails if generated document has drifted):
  python3 tools/spec_to_doc.py --spec apps/e906_repl_app/SPECIFICATION.md --output apps/e906_repl_app/GUIDE.md --check
"""

import sys
import os
import re
import argparse
from pathlib import Path

REQ_PATTERN = re.compile(
    r"^###\s+`\[(SPEC-[A-Z0-9\-_]+)\]`\s+(.*?)$", re.MULTILINE
)

def parse_spec(spec_text: str) -> dict:
    """Parses a canonical AbstractX SPECIFICATION.md document."""
    lines = spec_text.splitlines()
    title = "AbstractX Component Documentation"
    for line in lines:
        if line.startswith("# "):
            title = line[2:].strip()
            break

    # Extract Mermaid diagrams
    mermaid_blocks = []
    in_mermaid = False
    cur_block = []
    for line in lines:
        if line.strip() == "```mermaid":
            in_mermaid = True
            cur_block = []
        elif in_mermaid:
            if line.strip() == "```":
                in_mermaid = False
                mermaid_blocks.append("\n".join(cur_block))
            else:
                cur_block.append(line)

    # Extract Requirements
    requirements = []
    sections = re.split(r"(?=^###\s+`\[SPEC-[A-Z0-9\-_]+\]`)", spec_text, flags=re.MULTILINE)
    for sec in sections:
        m = REQ_PATTERN.search(sec)
        if not m:
            continue
        req_id = m.group(1)
        req_title = m.group(2).strip()

        # Extract verified by
        v_match = re.search(r"\*\s+\*\*Verified by\*\*:\s*([^\n]+)", sec)
        verified_by = v_match.group(1).strip() if v_match else "unspecified"

        # Extract implementation target
        t_match = re.search(r"\*\s+\*\*Implementation Target\*\*:\s*([^\n]+)", sec)
        impl_target = t_match.group(1).strip() if t_match else "unspecified"

        # Extract requirement bullets
        req_bullets = []
        for line in sec.splitlines():
            line_str = line.strip()
            if line_str.startswith("* ") and not line_str.startswith("* **Verified") and not line_str.startswith("* **Implementation"):
                req_bullets.append(line_str[2:])

        requirements.append({
            "id": req_id,
            "title": req_title,
            "verified_by": verified_by,
            "impl_target": impl_target,
            "bullets": req_bullets,
            "full_text": sec.strip()
        })

    return {
        "title": title,
        "mermaid_blocks": mermaid_blocks,
        "requirements": requirements
    }

def generate_guide(parsed: dict, spec_rel_path: str) -> str:
    """Generates a developer guide from the parsed specification."""
    out = []
    out.append(f"# {parsed['title']} — Developer Guide")
    out.append("")
    out.append(f"> **SSOT Notice**: This documentation is automatically generated from the authoritative specification [`{spec_rel_path}`]({spec_rel_path}). Do not edit manually.")
    out.append("")
    out.append("## 1. Architectural Overview")
    out.append("")
    out.append(f"This guide provides operational and implementation details for `{parsed['title']}`. Total governing requirements: **{len(parsed['requirements'])}**.")
    out.append("")

    if parsed["mermaid_blocks"]:
        out.append("## 2. System Architecture Diagrams")
        out.append("")
        for idx, block in enumerate(parsed["mermaid_blocks"], 1):
            out.append(f"### Diagram {idx}: Dataflow & Architecture")
            out.append("```mermaid")
            out.append(block)
            out.append("```")
            out.append("")

    out.append("## 3. Normative Requirements Matrix")
    out.append("")
    out.append("| Requirement ID | Title | Verification Skill | Implementation Target |")
    out.append("| :--- | :--- | :--- | :--- |")
    for req in parsed["requirements"]:
        out.append(f"| `{req['id']}` | **{req['title']}** | `{req['verified_by']}` | `{req['impl_target']}` |")
    out.append("")

    out.append("## 4. Requirement Specifications Detail")
    out.append("")
    for req in parsed["requirements"]:
        out.append(f"### `{req['id']}`: {req['title']}")
        out.append(f"- **Verification Skill**: `{req['verified_by']}`")
        out.append(f"- **Implementation Target**: `{req['impl_target']}`")
        if req["bullets"]:
            out.append("- **Key Contract Invariants**:")
            for b in req["bullets"][:5]:
                out.append(f"  * {b}")
        out.append("")

    return "\n".join(out)

def main():
    parser = argparse.ArgumentParser(description="AbstractX Specification to Markdown Documentation Generator")
    parser.add_argument("--spec", required=True, help="Path to input SPECIFICATION.md")
    parser.add_argument("--output", required=True, help="Path to output Markdown file (e.g. GUIDE.md or README.md)")
    parser.add_argument("--format", choices=["guide", "readme"], default="guide", help="Output format style")
    parser.add_argument("--check", action="store_true", help="Check mode: exit with error if output file has drifted from spec")

    args = parser.parse_args()

    spec_path = Path(args.spec)
    if not spec_path.exists():
        print(f"Error: Specification file not found: {spec_path}", file=sys.stderr)
        sys.exit(1)

    with open(spec_path, "r", encoding="utf-8") as f:
        spec_text = f.read()

    parsed = parse_spec(spec_text)
    doc_content = generate_guide(parsed, spec_path.name)

    out_path = Path(args.output)
    if args.check:
        if not out_path.exists():
            print(f"Drift Error: Target documentation file '{out_path}' does not exist.", file=sys.stderr)
            sys.exit(1)
        with open(out_path, "r", encoding="utf-8") as f:
            existing = f.read()
        if existing.strip() != doc_content.strip():
            print(f"Drift Error: Documentation '{out_path}' has drifted from '{spec_path}'. Run without --check to regenerate.", file=sys.stderr)
            sys.exit(2)
        print(f"[OK] Documentation '{out_path}' is up-to-date with '{spec_path}'.")
        return

    out_path.parent.mkdir(parents=True, exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(doc_content)
    print(f"[SUCCESS] Generated documentation: {out_path} ({len(parsed['requirements'])} requirements)")

if __name__ == "__main__":
    main()
