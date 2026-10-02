#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
AbstractX Engineering Log Mistake & Lesson Learned Logger
---------------------------------------------------------
Captures mistakes, bugs, and hardware errata into engineering_log.md
and ensures the lesson learned updates the Markdown specification (SSOT).
"""

import sys
import os
import argparse
import re
from datetime import datetime
from pathlib import Path

def main():
    parser = argparse.ArgumentParser(
        description="Log a mistake and lesson learned to engineering_log.md and update specifications."
    )
    parser.add_argument("--title", type=str, help="Short title describing the mistake / bug")
    parser.add_argument("--target", type=str, default="General", help="Affected target or subsystem (e.g. targets/allwinner_e907)")
    parser.add_argument("--symptoms", type=str, default="", help="What failed, glitched, or broke?")
    parser.add_argument("--cause", type=str, default="", help="Root cause explanation")
    parser.add_argument("--lesson", type=str, default="", help="Lesson learned / invariant missed")
    parser.add_argument("--spec", type=str, default="targets/SPECIFICATION.md", help="Markdown specification file to update")
    parser.add_argument("--tag", type=str, default="", help="Requirement tag added/updated (e.g. SPEC-HAL-02)")
    parser.add_argument("--contract", type=str, default="", help="Exact requirement rule added to the specification")
    parser.add_argument("--append", action="store_true", help="Directly append entry to engineering_log.md")
    args = parser.parse_args()

    root_dir = Path(__file__).resolve().parent.parent
    log_file = root_dir / "engineering_log.md"

    today_str = datetime.now().strftime("%Y-%m-%d")
    title = args.title or "<Component / Issue Title>"
    symptoms = args.symptoms or "<Describe what failed, hung, or caused jitter>"
    cause = args.cause or "<Technical explanation of why the failure occurred>"
    lesson = args.lesson or "<Architectural assumption that fell short>"
    spec = args.spec
    tag = args.tag or "[SPEC-XXX-YY]"
    contract = args.contract or "<Requirement rule added to the spec so this mistake never repeats>"

    entry = f"""
## [{today_str}] - Mistake & Lesson: {title}

### 1. Mistake / Bug Observed
* **Target / Subsystem**: `{args.target}`
* **Symptoms**: {symptoms}

### 2. Root Cause & Lesson Learned
* **Why it happened**: {cause}
* **Architectural Lesson**: {lesson}

### 3. Specification Update (SSOT Defense)
* **Target Spec File**: [`{spec}`]({spec})
* **Requirement Tag Added/Updated**: `{tag}`
* **Rule Added to Spec**:
  > {contract}

### 4. Implementation & Verification
* **Code Implementation**: Tagged with `// @impl {tag}`
* **Regression Test**: Verified via CTest / Cocotb simulation
"""

    if args.append:
        if not log_file.exists():
            log_file.write_text("# AbstractX Engineering Log\n\n", encoding="utf-8")
        with open(log_file, "a", encoding="utf-8") as f:
            f.write(entry)
        print(f"\033[92m[SUCCESS] Appended new lesson to {log_file.name}\033[0m")
    else:
        print("\033[94m=======================================================================\033[0m")
        print("\033[1m\033[92m  Generated Engineering Log Entry (pass --append to write to file):   \033[0m")
        print("\033[94m=======================================================================\033[0m")
        print(entry)

    # Check if spec file exists
    spec_path = root_dir / spec
    if spec_path.exists():
        spec_text = spec_path.read_text(encoding="utf-8", errors="ignore")
        clean_tag = tag.strip("[]`")
        if clean_tag and clean_tag not in spec_text:
            print(f"\033[93m[NOTE] Requirement `{clean_tag}` is not yet found in {spec}. Make sure to add it!\033[0m")
        else:
            print(f"\033[92m[OK] Requirement `{clean_tag}` is present in {spec}.\033[0m")

if __name__ == "__main__":
    main()
