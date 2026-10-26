---
name: static-analysis
runs_on: host
verifies: "First-party C++ is free of the bug classes compilers and analyzers can prove ([SPEC-AUDIT-15]): clang-tidy bugprone/concurrency/clang-analyzer checks on the host build, GCC -fanalyzer on the E906 build, and first-party warnings (-Wall -Wextra -Wshadow -Wconversion -Wsign-conversion)."
description: "Check skill. Runs clang-tidy (bugprone-*, concurrency-*, clang-analyzer-*) on first-party code from the host compile database and, with --fanalyzer, GCC -fanalyzer on the E906 build; findings are compared with a shrink-only baseline. Run when changing C++ under include/, src/, targets/, applib/ or apps/, or when a requirement names this skill."
---

# Skill: static-analysis

**What it verifies**: bugs that a compiler or analyzer can prove without running the code:
use-after-move, null dereference along a path, uninitialised members, integer narrowing, misuse of
non-thread-safe functions, unchecked returns, and so on. It does **not** prove that the concurrency
design is correct; that is `thread-sanitizer` (host) and `boundary-race-review` (MCU and transports).

## How to run

```bash
cmake --preset host -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build --preset host -j          # warnings from [SPEC-AUDIT-15] appear here
python3 tools/run_static_analysis.py    # clang-tidy, against tools/static_analysis_baseline.txt
python3 tools/run_static_analysis.py --fanalyzer   # plus GCC -fanalyzer on the E906 build (needs ~/.tools)
```

The first run on a tree with no baseline: triage the findings, fix the real ones, then
`--write-baseline` for the rest and replace each `# first triage` reason with a real one.

## Pass criteria

`[PASS] static analysis: N known finding(s), no new ones` with exit code 0. A new finding fails, and
so does a baseline entry that no longer occurs (the list only shrinks). With `-DABSTRACTX_WERROR=ON`,
the build must also have no first-party warnings.

## Evidence

The script's output and the build's warning count, in the commit message.
