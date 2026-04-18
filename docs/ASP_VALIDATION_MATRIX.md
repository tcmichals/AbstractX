# ASP Validation Matrix

This document defines reproducible validation gates and evidence expectations for ASP in the AbstractX repository.

## Purpose

- Make verification status objective and reviewable.
- Tie protocol requirements to concrete tests.
- Provide release gate criteria with evidence links.

## Validation levels

1. **Block-level**: parser/CRC/router/FIFO/peripheral units.
2. **Subsystem-level**: protocol + transport seams (e.g., parser→router, stream packetizer→framer).
3. **Integration-level**: end-to-end host intent → hardware behavior.
4. **Release gate**: objective pass/fail based on required suites and evidence freshness.

## Merge policy gate (mandatory)

| ID | Policy | Must prove | Gate |
|---|---|---|---|
| M-1 | New/changed code has tests | Every production code change is accompanied by relevant testbench updates | REQUIRED |

Pass criteria (merge policy):

- M-1 is satisfied for all merged changes in the release window.

---

## A) Protocol-critical block gates

| ID | Block | Must prove | Typical test style | Gate |
|---|---|---|---|---|
| B-1 | Frame parser | sync detect, length handling, resync under noise | cocotb block tests | REQUIRED |
| B-2 | CRC validator | pass good frames, drop bad CRC deterministically | cocotb block/composition | REQUIRED |
| B-3 | AXID router | correct route/drop semantics | cocotb block/composition | REQUIRED |
| B-4 | RX/TX FIFOs | backpressure, overflow signaling, metadata integrity | cocotb block tests | REQUIRED |
| B-5 | TX arbiter | priority policy + frame atomicity | cocotb block tests | REQUIRED |
| B-6 | TX framer | exact wire encoding + CRC correctness | cocotb composition | REQUIRED |
| B-7 | SPI frontend | byte ingress/egress correctness under CS/SCLK behavior | cocotb/top tests | REQUIRED |
| B-8 | AbstractX AXIS seam | valid/ready behavior at ingress/egress seams under backpressure | cocotb block tests | REQUIRED |
| B-8a | AXIS tag bundle coherence | route/context tags and timestamp metadata remain aligned with frame data | cocotb block/composition | REQUIRED |
| B-8b | Dual timestamp semantics | `ts_ingress` and `ts_egress` are preserved/injected per profile without inversion or cross-frame mismatch | cocotb block/composition | PROFILE-OPTIONAL |
| B-9 | SPI translator -> fabric packet | correct target/command/sub-id extraction and packet emission | cocotb block/composition | REQUIRED |
| B-10 | Fabric arbitration | deterministic endpoint arbitration under contention | cocotb subsystem tests | REQUIRED |

Pass criteria (block level):

- All REQUIRED block gates pass.
- No deterministic reproducible failures remain open.

---

## B) Control and stream operation gates

| ID | Path | Must prove | Gate |
|---|---|---|---|
| O-1 | CONTROL `READ_BLOCK` | valid decode, deterministic response, safe handling on unmapped addresses | REQUIRED |
| O-2 | CONTROL `WRITE_BLOCK` | deterministic write path + response behavior | REQUIRED |
| O-3 | STREAM (`axid=0x05`) | in-order tunnel delivery and packetized return stream | REQUIRED |
| O-4 | Discovery `HELLO/GET_CAPS` | stable capability response and profile reporting | REQUIRED |

Pass criteria (operation level):

- All REQUIRED operation gates pass on current main branch baseline.

---

## C) Transport profile gates (SPI)

| ID | Transport behavior | Must prove | Gate |
|---|---|---|---|
| T-1 | Byte-stream semantics | frame parsing independent of SPI burst boundaries | REQUIRED |
| T-2 | Multiplex semantics | interleaved control/background traffic remains valid | REQUIRED |
| T-3 | Pad-byte isolation | dummy/pad bytes do not alter ASP wire semantics | REQUIRED |
| T-4 | Resync robustness | parser recovers after malformed/noisy sequences | REQUIRED |
| T-5 | ISR + auto-DMA behavior | `INT_REQ` notification and threshold/event-driven DMA triggering behave deterministically | PROFILE-OPTIONAL |

Pass criteria (transport level):

- All REQUIRED transport gates pass in simulation.
- No unresolved regressions in parser/transport paths.

---

## D) Integration and parity gates

| ID | Scenario | Must prove | Gate |
|---|---|---|---|
| I-1 | E2E register read/write | host intent reaches peripheral and response returns deterministically | REQUIRED |
| I-2 | E2E stream tunnel | stream payload loop behavior is stable under burst/backpressure | REQUIRED |
| I-3 | Legacy parity spot-checks | critical legacy workflows map to equivalent ASP outcomes | REQUIRED |
| I-4 | Profile declaration | runtime reports selected profile (`asp-compat-v1` / `asp-native`) consistently | REQUIRED |
| I-5 | DMA looping stream | continuous sensor stream writes with circular-buffer wrap and deterministic burst reads | REQUIRED |
| I-6 | Autonomous stream start | once configured via Wishbone, peripheral can stream without per-frame host trigger | REQUIRED |
| I-7 | Timer auto-trigger stream | periodic trigger produces bounded-rate stream behavior and valid status signaling | PROFILE-OPTIONAL |

Pass criteria (integration level):

- All REQUIRED integration gates pass.
- Parity deltas (if any) are documented with owner and ETA.

---

## E) Evidence artifact checklist

For each release candidate, include:

- test run summary (suite-by-suite pass/fail),
- failing-test list (if any),
- protocol profile under test,
- commit SHA and date,
- toolchain/simulator versions.

Recommended artifact paths (suggested conventions):

- `docs/evidence/validation/<date>-<sha>-summary.md`
- `docs/evidence/validation/<date>-<sha>-results.json`

---

## F) Release gate decision table

| Gate | Condition | Decision |
|---|---|---|
| G1 | All REQUIRED block/operation/transport/integration gates pass | PASS |
| G2 | Any REQUIRED gate failing | NO-GO |
| G3 | Required evidence artifacts missing or stale | NO-GO |
| G4 | Known parity deltas documented + accepted for milestone scope | CONDITIONAL PASS |

Staleness guidance:

- Evidence should be regenerated for release candidates and any protocol-impacting change.

---

## G) Ownership and maintenance

- Protocol owner updates this matrix when requirements or profiles change.
- Test owners keep gate IDs mapped to actual suites.
- Release owner signs off PASS/NO-GO with evidence references.

---

*Revision: 1.0 (Apr 2026)*
