#!/usr/bin/env python3
# Copyright (C) 2026 Tim Michals
# SPDX-License-Identifier: GPL-3.0-or-later
# @impl [SPEC-AUDIT-15] docs/SPECTRACE.md
"""
boundary_review.py - inventory of state shared across execution contexts (ISR, other core,
DMA/worker thread, coroutine domain) in MCU and transport code.

A file-scope variable is *shared* when it is
  * written inside an ISR (a function named *_isr / *_irq_handler / *_IRQHandler, or marked
    __attribute__((interrupt))),
  * declared volatile, or
  * handed to IsrDispatcher::post / Dispatcher::post / isr_post_resume.

Every shared variable needs an ownership note on its declaration line or the line above:
    // shared: <who writes / who reads>; <how it is synchronised>
e.g. // shared: msgbox ISR writes, main loop reads; std::atomic acq/rel

  python3 tools/boundary_review.py              # inventory with notes (for the review)
  python3 tools/boundary_review.py --check      # exit 1 if any shared variable has no note
  python3 tools/boundary_review.py --paths targets/allwinner_e906   # narrow the scope

The note is written by whoever answers the boundary-race-review checklist for that variable;
the script only proves that nobody skipped one.
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PATHS = ["include/abstractx", "src", "targets/allwinner_e906", "targets/pico2w_rp2350",
                 "targets/esp32p4", "apps"]
EXTS = {".c", ".cpp", ".h", ".hpp"}
SKIP_PARTS = {"third_party", "micropython", "build", "lib", "tests", "sim"}

ISR_DEF = re.compile(
    r"^[^;{}\n]*?\b(\w*(?:_isr|_irq_handler|IRQHandler))\s*\([^;{}]*\)[^;{}]*\{"
    r"|^[^;{}\n]*__attribute__\(\(\s*interrupt[^\n]*\n?[^;{}]*?\b(\w+)\s*\([^;{}]*\)[^;{}]*\{",
    re.M)
DECL = re.compile(
    r"^(?P<indent>[ \t]*)(?!return\b|using\b|typedef\b|friend\b|case\b)"
    r"(?:(?:static|inline|extern|thread_local|alignas\([^)]*\)|__attribute__\(\([^)]*\)\))\s+)*"
    r"(?P<vol>(?:volatile\s+)?)"
    r"(?P<type>(?:std::atomic<[^;=]+?>|[A-Za-z_][\w:]*(?:<[^;=]*?>)?)[\s\*&]+(?:volatile\s+)?)"
    r"(?P<name>[A-Za-z_]\w*)\s*(?:\[[^\]]*\]\s*)*(?:\{[^;]*\}|=[^;]*)?;", re.M)
POST = re.compile(r"\b(?:IsrDispatcher::post|Dispatcher::post|isr_post_resume)\s*\(\s*(?:[\w:]+\()?(\w+)")
NOTE = "shared:"


def strip_comments(text):
    """Same length, comments blanked (keeps offsets and line numbers)."""
    def blank(m):
        return re.sub(r"[^\n]", " ", m.group(0))
    return re.sub(r"//[^\n]*|/\*.*?\*/", blank, text, flags=re.S)


def file_scope_mask(code):
    """For each offset: True if at file/namespace scope (not inside a function, class or initializer)."""
    mask = bytearray(len(code))
    stack = []  # True for namespace / extern "C" braces
    i = 0
    for i, ch in enumerate(code):
        if ch == "{":
            head = code[max(0, i - 120):i]
            stack.append(bool(re.search(r'(\bnamespace\b[\w\s:]*|extern\s+"C"\s*)$', head)))
        elif ch == "}":
            if stack:
                stack.pop()
        mask[i] = all(stack)
    return mask


def body(code, open_pos):
    depth = 0
    for i in range(open_pos, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth == 0:
                return code[open_pos:i + 1]
    return code[open_pos:]


def scan(rel, text):
    code = strip_comments(text)
    mask = file_scope_mask(code)
    lines = text.splitlines()
    globals_ = {}
    for m in DECL.finditer(code):
        if not mask[m.start("name")]:
            continue
        decl = m.group(0)
        if re.search(r"\b(const|constexpr|consteval)\b", decl) and "volatile" not in decl:
            continue
        if "(" in m.group("type"):
            continue
        globals_.setdefault(m.group("name"), (m.start("name"), "volatile" in decl))
    shared = {}
    for name, (pos, vol) in globals_.items():
        if vol:
            shared.setdefault(name, set()).add("volatile")
    for m in ISR_DEF.finditer(code):
        isr = m.group(1) or m.group(2)
        b = body(code, code.index("{", m.start()))
        for name in globals_:
            if re.search(rf"(?<![\w.>]){re.escape(name)}\s*(?:\[[^\]]*\]\s*)?"
                         rf"(?:=(?!=)|\+=|-=|\|=|&=|\+\+|--|\.(?:store|exchange|fetch_\w+|compare_exchange_\w+)\()",
                         b) or re.search(rf"(?:\+\+|--){re.escape(name)}\b", b):
                shared.setdefault(name, set()).add(f"written in {isr}")
    for m in POST.finditer(code):
        if m.group(1) in globals_:
            shared.setdefault(m.group(1), set()).add("posted to dispatcher")
    out = []
    for name, why in sorted(shared.items(), key=lambda kv: globals_[kv[0]][0]):
        ln = code.count("\n", 0, globals_[name][0]) + 1
        window = lines[max(0, ln - 2):ln]
        note = next((l.split(NOTE, 1)[1].strip() for l in window if NOTE in l), None)
        out.append((rel, ln, name, sorted(why), note))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--paths", nargs="*", default=DEFAULT_PATHS)
    ap.add_argument("--check", action="store_true", help="exit 1 when a shared variable has no note")
    args = ap.parse_args()
    items = []
    for base in args.paths:
        for p in sorted((ROOT / base).rglob("*")):
            if p.suffix in EXTS and p.is_file() and not (set(p.relative_to(ROOT).parts) & SKIP_PARTS):
                rel = p.relative_to(ROOT).as_posix()
                items += scan(rel, p.read_text(encoding="utf-8", errors="ignore"))
    missing = [i for i in items if not i[4]]
    for rel, ln, name, why, note in items:
        tag = "ok     " if note else "MISSING"
        print(f"  {tag} {rel}:{ln}: {name} ({', '.join(why)})" + (f" -- {note}" if note else ""))
    print(f"Shared state: {len(items)} variable(s), {len(missing)} without an ownership note")
    if args.check and missing:
        print("[FAIL] answer the boundary-race-review checklist and add '// shared: <owner>; <sync>' "
              "to each MISSING declaration")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
