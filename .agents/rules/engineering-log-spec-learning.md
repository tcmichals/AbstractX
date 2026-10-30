# Rule: SpecTrace Continuous Learning Loop (Engineering Log to Specification SSOT)

This rule defines the operational execution of **SpecTrace** ([`docs/SPECTRACE.md`](../../docs/SPECTRACE.md)), governing how mistakes, bugs, hardware errata, and architectural refinements are captured in `engineering_log.md` and permanently codified into the Markdown specifications (`SPECIFICATION.md`) and agent invariant rules (`AGENTS.md`).

---

## 1. The Core Principle: Specifications Learn from Mistakes
In AbstractX, **Markdown Drives the Code (SSOT)**:
* When a bug, race condition, or timing glitch occurs, the root cause is almost always an **underspecified, missing, or ambiguous requirement in the Markdown specification**.
* If we only fix the code, the institutional memory is lost, and future developers or AI agents will make the exact same mistake again.
* **Every mistake must result in a stronger, more explicit specification.**

---

## 2. The 4-Step Learning Cycle

1. **Mistake, bug or hardware erratum observed.**
2. **Chronicle it in `engineering_log.md`**: what broke (symptoms and error), root cause and
   lesson (why it happened), and the spec contract to add or update (`[SPEC-*]`).
3. **Update the Markdown specification first (SSOT)**: add or update `[SPEC-*]` in the
   `targets/` or `apps/` `SPECIFICATION.md`; add diagrams (`[SPEC-AUDIT-16]`), timing
   constraints or memory boundaries.
4. **Implement the code fix and a regression test**: tag the code `// @impl [SPEC-*] <file_path>`,
   add a CTest or cocotb test that would have caught it, and run the requirement's
   `Verified by` skills.

---

## 3. Standard `engineering_log.md` Entry Format for Mistakes & Fixes

When addressing an error, bug, or architectural lesson, use this clean format:

```markdown
## [YYYY-MM-DD] - <Component / Issue Title>

### 1. Mistake / Bug Observed
* **Target / Subsystem**: e.g., `targets/allwinner_e906`, `include/asp_tlp64.hpp`
* **Symptoms**: What failed, hung, crashed, or caused jitter?

### 2. Root Cause & Lesson Learned
* **Why did it happen?**: Technical explanation of the failure or incorrect assumption.
* **Architectural Invariant Missed**: (e.g. Posted write interconnect flush, FIFO vs bus busy, non-blocking drain).

### 3. Specification Update (SSOT Defense)
* **Target Spec File**: e.g., `targets/SPECIFICATION.md` or `apps/gps_imu_app/SPECIFICATION.md`
* **Requirement Tag Added / Updated**: `[SPEC-XXX-YY]`
* **Contract Added**: Exact requirement text added to the spec so this mistake cannot happen again.

### 4. Implementation & Regression Test
* **Code Changes**: Tagged with `// @impl [SPEC-XXX-YY]`
* **Regression Test**: Test added in `sim/` or `tests/` reproducing and verifying the fix.
* **Verification**: `python3 tools/audit_specs.py` -> 100% Traceability verified.
```

---

## 4. Helper Tooling
To scaffold a new lesson into `engineering_log.md`:
```bash
python3 tools/log_mistake.py --title "SPI CS Deassertion Race" --target "targets/pico2w" --spec "targets/SPECIFICATION.md" --tag "SPEC-HAL-02"
```
Or run the traceability verification:
```bash
python3 tools/audit_specs.py
```
