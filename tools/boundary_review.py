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

It also checks the wake handshake ([SPEC-HAL-10], 2026-10-28): a waiter slot is a
std::atomic<void*> / std::atomic<std::coroutine_handle<>> named *waiter* / *coroutine* / *handle*
(class members included). Publishing yourself as waiter and then checking for data, or publishing
data and then looking for a waiter, is a store followed by a load of another variable: without a
std::atomic_thread_fence(std::memory_order_seq_cst) between them both sides can miss each other
and the reader is never woken (TSan does not see it). Each such store must be followed by that
fence before the next read, or carry a waiver with its reason on that line or the line above:
    // handshake: <why no fence is needed>   (e.g. same hart, the waiter unmasks the IRQ after
                                             publishing and the line is level-triggered)
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
WAITER_DECL = re.compile(
    r"std::atomic<\s*(?:void\s*\*|std::coroutine_handle<[^>]*>)\s*>\s+(\w*(?:waiter|coroutine|handle)\w*)\b")
STORE = re.compile(r"(?:\b|->|\.)(\w+)\s*\.\s*store\s*\(")
FENCE = re.compile(r"atomic_thread_fence\s*\(\s*std::memory_order_seq_cst\s*\)")


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


def call_args(code, open_paren):
    depth = 0
    for i in range(open_paren, len(code)):
        if code[i] == "(":
            depth += 1
        elif code[i] == ")":
            depth -= 1
            if depth == 0:
                return code[open_paren + 1:i], i + 1
    return code[open_paren + 1:], len(code)


def rest_of_block(code, pos):
    depth = 0
    for i in range(pos, len(code)):
        if code[i] == "{":
            depth += 1
        elif code[i] == "}":
            depth -= 1
            if depth < 0:
                return code[pos:i]
    return code[pos:]


# @impl [SPEC-AUDIT-15] docs/SPECTRACE.md
def handshake_scan(rel, text, waiters):
    """Store then read of a different atomic, where one side is a waiter slot, with no seq_cst fence."""
    code = strip_comments(text)
    names = waiters | set(WAITER_DECL.findall(code))
    if not names:
        return []
    out = []
    reads_waiter = re.compile(r"\b(?:%s)\s*\.\s*(?:load|exchange|compare_exchange_\w+)\s*\(|\bwake\w*\s*\("
                              % "|".join(map(re.escape, sorted(names))))
    for m in STORE.finditer(code):
        var = m.group(1)
        args, end = call_args(code, code.index("(", m.end() - 1))
        if "memory_order_seq_cst" in args or not re.search(r"memory_order_(?:release|relaxed)", args):
            continue                                   # seq_cst store (explicit or default)
        tail = rest_of_block(code, end)
        f = FENCE.search(tail)
        upto = tail[:f.start()] if f else tail
        if var in names:
            if re.match(r"\s*(?:nullptr|\{\s*\}|0\s*,)", args):
                continue                               # clearing the slot publishes nothing
            hit = re.search(r"\.\s*load\s*\(|\b(?:satisfied|available|empty|ready)\s*\(", upto)
            role = "waiter published, then data checked"
        else:
            hit = reads_waiter.search(upto)
            role = "data published, then waiter checked"
        if hit:
            ln = code.count("\n", 0, m.start()) + 1
            src = text.splitlines()
            if any("handshake:" in l for l in src[max(0, ln - 2):ln]):
                continue
            out.append((rel, ln, var, role))
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
    waiters = set()
    files = []
    for base in args.paths:
        for p in sorted((ROOT / base).rglob("*")):
            if p.suffix in EXTS and p.is_file() and not (set(p.relative_to(ROOT).parts) & SKIP_PARTS):
                text = p.read_text(encoding="utf-8", errors="ignore")
                files.append((p.relative_to(ROOT).as_posix(), text))
                waiters |= set(WAITER_DECL.findall(strip_comments(text)))
    handshakes = []
    for rel, text in files:
        handshakes += handshake_scan(rel, text, waiters)
    missing = [i for i in items if not i[4]]
    for rel, ln, name, why, note in items:
        tag = "ok     " if note else "MISSING"
        print(f"  {tag} {rel}:{ln}: {name} ({', '.join(why)})" + (f" -- {note}" if note else ""))
    print(f"Shared state: {len(items)} variable(s), {len(missing)} without an ownership note")
    for rel, ln, var, role in handshakes:
        print(f"  HANDSHAKE {rel}:{ln}: {var}.store(release/relaxed): {role} with no seq_cst fence between")
    print(f"Wake handshakes: {len(handshakes)} store->load without a seq_cst fence")
    rc = 0
    if args.check and missing:
        print("[FAIL] answer the boundary-race-review checklist and add '// shared: <owner>; <sync>' "
              "to each MISSING declaration")
        rc = 1
    if args.check and handshakes:
        print("[FAIL] put std::atomic_thread_fence(std::memory_order_seq_cst) between the store and the "
              "read on both sides of each HANDSHAKE ([SPEC-HAL-10] wake handshake)")
        rc = 1
    return rc


if __name__ == "__main__":
    sys.exit(main())
