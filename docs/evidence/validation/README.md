# Validation Evidence

Validation run artifacts are stored here per release candidate.

## Naming Convention

```
<date>-<short-sha>-summary.md     -- Human-readable pass/fail summary
<date>-<short-sha>-results.json   -- Machine-readable gate results
```

## Current Status

| Gate Group | Status |
|---|---|
| Block-level RTL (B-1 to B-10) | NOT-RUN (no evidence artifact yet) |
| Control/Stream operations (O-1 to O-4) | NOT-RUN |
| SPI transport (T-1 to T-5) | NOT-RUN |
| Integration (I-1 to I-7) | NOT-RUN |
| C++ sim harnesses | PASS (all 4 harnesses pass on `feature/universal-async-framework`) |

## Next Step

Run cocotb testbenches and generate a dated evidence artifact here before any release candidate.
