# Rule: SpecTrace Module Review & Quality Gate Execution

This rule defines the standardized, cross-agent workflow for running the SpecTrace adversarial review and static invariant quality gate across VS Code / GitHub Copilot and Antigravity.

---

## 1. The Canonical Entry Point (`[SPEC-AUDIT-05]`)

All AI agents (Antigravity, GitHub Copilot) and human engineers MUST use the single canonical CLI command rather than creating ad-hoc, divergent review workflows:

```bash
python3 tools/spectrace.py --review [options]
```

### Supported Command Invocations

1. **Review current working tree diff (default)**:
   ```bash
   python3 tools/spectrace.py --review
   ```
2. **Review a specific module with associated tests**:
   ```bash
   python3 tools/spectrace.py --review --module <path/to/module> [--related-path <path/to/test>]
   ```
3. **Shift-Left Spec Review (audit the SPECIFICATION.md itself)**:
   ```bash
   python3 tools/spectrace.py --spec-review --module <path/to/module>
   ```
4. **Anti-Drift & Parity Check (Spec <-> Code synchronization)**:
   ```bash
   python3 tools/spectrace.py --drift [--module <path/to/module>]
   ```
5. **Review branch changes relative to a base ref**:
   ```bash
   python3 tools/spectrace.py --review --base-ref origin/main
   ```
6. **Explicit full sweep of maintained modules**:
   ```bash
   python3 tools/spectrace.py --review --all-modules
   ```

---

## 2. Invariants for Agents (Antigravity & Copilot)

1. **Shared Contract (`[SPEC-AUDIT-05]`)**: Copilot slash commands (e.g. `/spectrace-review`) and Antigravity agent tasks MUST delegate directly to `python3 tools/spectrace.py --review`. Neither agent may implement independent or competing invariant checks.
2. **Bounded Scope (`[SPEC-AUDIT-01]`, `[SPEC-AUDIT-02]`)**: Audit scope is strictly bounded to the selected module or changed files. Unrelated Linux host sources (`targets/linux`) and generated/vendor directories (Buildroot, kernel) are excluded by default unless explicitly requested via `--include-linux` or `--module targets/linux`.
3. **Exit Code Propagation**: `tools/spectrace.py --review` returns the underlying auditor's real exit code. A non-zero return code indicates a failing invariant gate that blocks commit and pull requests.
4. **Spec-First Resolution**: If an invariant or traceability audit flags an issue, agents MUST follow `AGENTS.md` and [SPECTRACE.md](../../docs/SPECTRACE.md): update the Markdown specification (`SPECIFICATION.md`) with explicit `[SPEC-*]` requirements first before modifying C++ or RTL code.
