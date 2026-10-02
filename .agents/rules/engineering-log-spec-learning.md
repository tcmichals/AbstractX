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

```text
  [1. Mistake / Bug / Hardware Errata Observed]
                         │
                         ▼
  [2. Chronicle in engineering_log.md]
      • What broke? (Symptoms & Error)
      • Root Cause & Lesson Learned (Why did it happen?)
      • Spec Contract to Add/Update ([SPEC-*])
                         │
                         ▼
  [3. Update Markdown Specification (SSOT First)]
      • Add or update [SPEC-*] in targets/ or apps/ SPECIFICATION.md
      • Add Mermaid diagrams, timing constraints, or memory boundaries
                         │
                         ▼
  [4. Implement Code Fix & Regression Test]
      • Tag implementing code: // @impl [SPEC-*] <file_path>
      • Add CTest or Cocotb unit test to permanently prevent regression
      • Verify: python3 tools/audit_specs.py (100% Traceability)
```

---

## 3. Standard `engineering_log.md` Entry Format for Mistakes & Fixes

When addressing an error, bug, or architectural lesson, use this clean format:

```markdown
## [YYYY-MM-DD] - <Component / Issue Title>

### 1. Mistake / Bug Observed
* **Target / Subsystem**: e.g., `targets/allwinner_e907`, `include/asp_tlp64.hpp`
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
