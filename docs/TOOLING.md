# AbstractX Tooling: How It Works

AbstractX is three projects in one repository:

| Project | What it is | Lives in |
| :--- | :--- | :--- |
| **SpecTrace** | Spec-driven development: requirements, code generation by AI agents, testing, evidence and the feedback loop | `docs/SPECTRACE*.md`, `tools/`, `.agents/skills/`, `docs/verification/` |
| **Framework** | The runtime: C++20 coroutines, zero-copy transports, HAL and targets (E906, Pico 2 W, ESP32-P4, Linux), and the RTL fabric (`asp_*`) | `include/`, `src/`, `targets/`, `rtl/`, `hw/` |
| **Studio** | Observability GUI: live trace, topology, metrics | `tools/visualizer/` |

This page is about the tooling: what each tool does, how they fit together, and
how the results are tracked over time. The model behind it is in
[`SPECTRACE_GUIDE.md`](SPECTRACE_GUIDE.md); the rules every AI agent follows are in
[`AGENTS.md`](../AGENTS.md) (section 0).

---

## 1. The Trac families

Every check and measurement belongs to one family. Each family answers one
question and its results are tracked over time in git.

| Family | Question | Covers |
| :--- | :--- | :--- |
| **testTrac** | Is it **correct**? | host tests (CppUTest, sim, Python), ASan/UBSan, TSan, RTL lint and synthesis checks, cocotb RTL simulation, E906 layout and disassembly checks, traceability (no untraced code), board functional tests |
| **sizeTrac** | Does it **fit**? | ELF sections per target vs. memory budget, FPGA LUT/FF/BSRAM/IO use per board |
| **perfTrac** | Is it **fast and steady enough**? | FPGA timing (routed Fmax vs. clock), protocol timing (DShot, WS2812, SPI), loop period and jitter, IPC latency (RPMsg, UIO), CPU load |

**SpecTrace** ties the families to the requirements: a requirement names the
Trac skills that verify it (`Verified by`), and its verdict comes from their
latest recorded results.

---

## 2. Tool map

Status: **exists** = in the repo and in use; **planned** = described in
`SPECTRACE_GUIDE.md` §9, not built yet.

### SpecTrace (traceability, review, feedback)

| Tool | What it does | Status |
| :--- | :--- | :--- |
| `tools/spectrace_matrix.py --matrix` | requirement → code → skills matrix with verdicts; writes `docs/verification/TRACEABILITY_MATRIX.md` | exists |
| `tools/spectrace_matrix.py --changes [--prompt] [--staged --strict]` | maps a diff to its governing requirements, flags untraced code, builds the review bundle | exists |
| `tools/git-hooks/pre-commit` | blocks commits with untraced code (opt in: `git config core.hooksPath tools/git-hooks`) | exists |
| `tools/spectrace_drift.py`, `spectrace_dedupe.py` | repo-wide requirement parity, duplicate requirement text | exists |
| `tools/audit_specs.py` | tag presence for `DESIGN_SPECIFICATION.md` only; **not** evidence of correctness | exists (legacy) |
| `tools/log_mistake.py` | engineering-log entry template | exists |
| `tools/render_diagrams.py [--check]` | renders `*.diagram.yaml` block diagrams to SVG; `--check` fails on stale SVGs and ASCII box art in Markdown (`[SPEC-AUDIT-16]`, skill `doc-diagrams`) | exists |
| `tools/trac.py size\|perf\|report\|export-benchmark` | runs sizeTrac / perfTrac checks, appends evidence to `docs/verification/evidence/<YYYY-MM>/<host>.jsonl`, reports latest result and change, exports github-action-benchmark JSON | exists |
| status report, skill scorecard | "are things working, are we getting better" | planned |

### testTrac

| Tool | Checks | Status |
| :--- | :--- | :--- |
| `ctest` (Debug, ASan/UBSan fatal, TSan builds) | C++ and Python tests | exists (runner skill planned) |
| `tools/run_cocotb.py` | every registered cocotb target | exists |
| `tools/check_e906_layout.py` | E906 segments inside remoteproc windows (runs after every E906 link) | exists |
| Verilator lint, Yosys `check` | RTL structure (multi-drivers, widths) | commands in skills; runner planned |
| E906 disassembly check | trap entry, `mhcr`, idle wait in the binary | manual; runner planned |

### sizeTrac

| Tool | Measures | Status |
| :--- | :--- | :--- |
| `tools/trac.py size --elf <target>=<elf>` | ELF sections vs. budgets (`tools/membrowse-targets.json`, E906 192 KB SRAM) | exists |
| `tools/trac.py size --fpga <board>=<nextpnr.log>` | FPGA utilisation | exists |

### perfTrac

| Tool | Measures | Status |
| :--- | :--- | :--- |
| `tools/trac.py perf --fpga <board>=<nextpnr.log>` | routed Fmax per clock (last line; the first is a placement estimate) | exists |
| cocotb timing tests | DShot600 / WS2812 timing at 12/27/50 MHz | exists |
| cubie-a5e `run_full_sweep.py` | RPMsg / UIO round trip on silicon | exists in cubie-a5e |
| E906 loop jitter (`mcycle`) | on-board loop period and jitter | planned |

### Linux / board helpers

| Tool | What it does | Status |
| :--- | :--- | :--- |
| `tools/e906_rpmsg_bridge.py` | `/dev/rpmsgN` ↔ UDP for the E906 `abstractx-trace` / `abstractx-tlp` endpoints | exists (host-tested) |
| `hw/tang_nano20k/gen_fitonly_cst.py` | fit-only pin file from the package pin list | exists |

---

## 3. How results are tracked over time

1. Every run of a Trac tool produces a result with its numbers (test counts,
   bytes per section, LUT %, Fmax, latency, jitter).
2. Results are committed to git with the change they prove; today in commit
   messages and the engineering log, next in the evidence ledger
   (`SPECTRACE_GUIDE.md` §4), following the format cubie-a5e's
   `test_sweep_history.json` already uses.
3. Generated reports (matrix, status report, scorecard, charts) are built from
   those records, never edited by hand, so GitHub history can be graphed.
4. Anything missed (a bug found late, a check that gave a wrong answer, code no
   requirement covered) is recorded in `engineering_log.md` with a category and
   feeds back into the requirements and checks (`AGENTS.md` section 0).

---

## 4. Keeping it small: pruning and the local cache

**Pruning.** The tree must not grow without limit. A periodic `review-prune` pass
(monthly, or when a report shows it):

- merges duplicate requirements (`spectrace_dedupe.py`) and duplicate rules;
- retires superseded docs to `docs/tier1_vision/historical/` or deletes them,
  with an engineering-log entry saying why;
- removes tools and skills with no runs, no catches and no owner (scorecard);
- drops dead tags (code tags naming requirements that no longer exist).

**Local cache (token efficiency).** Agents should not read whole documents to
find one requirement. `spectrace_matrix.py --build-cache` (planned) writes
`.spectrace/cache/` (git-ignored, rebuilt when `HEAD` changes) with compact
indexes:

- requirements: tag, title, file:line, `Verified by`, implementing code, verdict;
- skills: name, family, what it verifies, how to run it;
- lessons: date, title, category, requirement tags.

The context builder then hands an agent only the requirements, skills and
lessons that a change touches, instead of whole specification files.

---

## 5. License

The tooling and its documentation are part of this repository and are licensed
like the rest of it (`LICENSE`, GPLv3; source files carry
`SPDX-License-Identifier: GPL-3.0-or-later`). The owner also offers a commercial
license (dual licensing, see `engineering_log.md` 2026-04-19).
