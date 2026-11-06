#!/usr/bin/env bash
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# Copy the evidence records this CI job wrote (lines added to docs/verification/evidence since the
# checkout, in new or changed files) into one file the report job merges. CI never commits them.
# @impl [SPEC-AUDIT-20] docs/SPECTRACE.md
set -euo pipefail
out="${1:?usage: collect_new_records.sh <out.jsonl>}"
mkdir -p "$(dirname "$out")"
: > "$out"
dir=docs/verification/evidence
git ls-files --others --exclude-standard -- "$dir" | grep '\.jsonl$' | while read -r f; do cat "$f" >> "$out"; done || true
git diff --no-color -U0 -- "$dir" | grep '^+{' | cut -c2- >> "$out" || true
echo "collected $(wc -l < "$out") record(s) into $out"
