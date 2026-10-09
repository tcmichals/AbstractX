#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
"""
SpecTrace Anti-Redundancy & Deduplication Engine (spectrace_dedupe.py)
====================================================================
Audits the specification markdown files for semantic and literal
redundancy to enforce the "Single Source of Truth" invariant.
"""

import os
import re
import sys
import argparse
from pathlib import Path

try:
    import spectrace_drift
except ImportError:
    # Allow running standalone
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import spectrace_drift

# ANSI Color Codes
GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
BLUE = "\033[94m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"

def get_words(text):
    """Normalize and tokenize text for similarity comparison."""
    text = text.lower()
    # Remove punctuation
    text = re.sub(r'[^\w\s]', '', text)
    return set(text.split())

def jaccard_similarity(set1, set2):
    if not set1 and not set2:
        return 1.0
    if not set1 or not set2:
        return 0.0
    intersection = len(set1.intersection(set2))
    union = len(set1.union(set2))
    return intersection / union

def find_duplicates(all_specs, threshold=0.55):
    """
    Find pairs of specs that are highly similar in title/text.
    """
    tags = sorted(list(all_specs.keys()))
    duplicates = []
    
    # Pre-tokenize
    tokenized_specs = {}
    for tag in tags:
        title = all_specs[tag].get("title", "")
        tokenized_specs[tag] = get_words(title)

    for i in range(len(tags)):
        for j in range(i + 1, len(tags)):
            tag1 = tags[i]
            tag2 = tags[j]
            words1 = tokenized_specs[tag1]
            words2 = tokenized_specs[tag2]
            
            # Avoid comparing empty sets or very short strings which falsely flag high Jaccard
            if len(words1) < 4 or len(words2) < 4:
                continue

            sim = jaccard_similarity(words1, words2)
            if sim >= threshold:
                duplicates.append({
                    "tag1": tag1,
                    "tag2": tag2,
                    "sim": sim,
                    "spec1": all_specs[tag1],
                    "spec2": all_specs[tag2]
                })

    # Sort by similarity descending
    duplicates.sort(key=lambda x: x["sim"], reverse=True)
    return duplicates

def main():
    parser = argparse.ArgumentParser(description="SpecTrace Anti-Redundancy & Deduplication Engine")
    parser.add_argument("--threshold", type=float, default=0.55, help="Jaccard similarity threshold (0.0 to 1.0)")
    args = parser.parse_args()

    root_dir = spectrace_drift.get_root_dir()
    all_specs_files = spectrace_drift.discover_all_specs(root_dir)
    
    all_specs = {}
    for sf in all_specs_files:
        parsed = spectrace_drift.parse_specification_file(sf, root_dir)
        all_specs.update(parsed)

    print(f"\n{BOLD}{CYAN}=== SpecTrace Anti-Redundancy Audit ==={RESET}")
    print(f"Auditing {len(all_specs)} formal requirements for redundancy...")
    
    duplicates = find_duplicates(all_specs, threshold=args.threshold)
    
    if not duplicates:
        print(f"\n{BOLD}{GREEN}VERDICT: [PASS - NO DUPLICATES FOUND]{RESET}")
        print("No requirements met the redundancy threshold.")
        sys.exit(0)
        
    print(f"\n{BOLD}{YELLOW}⚠️ POTENTIAL DUPLICATES DETECTED ⚠️{RESET}\n")
    
    for dup in duplicates:
        s1 = dup["spec1"]
        s2 = dup["spec2"]
        print(f"{BOLD}Similarity: {dup['sim']:.2f}{RESET}")
        print(f"[{s1['tag']}] ({s1['rel_spec']}:{s1['line']}): {s1['title']}")
        print(f"[{s2['tag']}] ({s2['rel_spec']}:{s2['line']}): {s2['title']}")
        print(f"-> {CYAN}Recommendation: Consolidate if they describe the same invariant.{RESET}")
        print("-" * 80)
        
    print(f"\n{BOLD}{YELLOW}VERDICT: [FAILED - {len(duplicates)} REDUNDANCY WARNINGS]{RESET}")
    sys.exit(1)

if __name__ == "__main__":
    main()
