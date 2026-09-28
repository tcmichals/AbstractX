# AbstractX ASP Requirements

## Mission

Provide deterministic hardware offload protocol and control paths using ASP over SPI (primary profile), with equivalent ASP semantics over alternate links when needed.

## Role split and migration intent

- **Legacy validation path**: MSP/legacy workflows may be used to baseline expected user-visible behavior.
- **AbstractX runtime target**: ASP is the canonical runtime protocol for deterministic transport/performance.
  - Primary ASP physical layer: **SPI**
   - Current deployment profile: **SPI-only (no USB data path)**
   - Secondary/alternate ASP layers (future/optional): serial/UART/USB-CDC and other link adapters

Migration rule:

1. Validate baseline behavior on legacy path where needed.
2. Preserve identical feature intent on ASP.
3. Move runtime traffic to ASP once acceptance gates pass.

## Must-have requirements

1. Canonical ASP ownership in this repository:
   - `docs/ASP_PROTOCOL.md`
   - `docs/ASP_SPI_TRANSPORT.md`
2. Deterministic control-path viability using RTL fast-path offload (no per-byte control CPU bottleneck in normal operation).
3. RTL path handles framing sync/length/CRC/AXID routing/FIFO.
4. Control endpoint handles policy/state transitions and result codes.
4a. Internal architecture uses a transport-translator into an AbstractX switch fabric that routes to Wishbone and DMA endpoints.
4a-note. This generalizes legacy protocol bus access by keeping AbstractX-to-Wishbone conversion transport-agnostic.
4b. Chip ISR (`INT_REQ`) signaling and threshold/event-driven auto-trigger DMA behavior are supported as peripheral capabilities for asynchronous streaming operation.
5. Unified dynamic I/O model via ASP block operations:
   - PWM, DSHOT, LED, NeoPixel and related peripherals through discoverable spaces.
6. Discovery support:
   - required `HELLO` + `GET_CAPS`
   - optional transport discovery metadata for multi-link deployments.
7. FPGA implementation language/tooling:
   - FPGA RTL code should remain **SystemVerilog**
   - simulation/testbench stack should remain **Verilator + cocotb**
8. Verification granularity requirement:
   - design must be decomposed into independently testable blocks before subsystem/integration sign-off.
9. Dual testbench requirement:
   - maintain RTL block/subsystem tests,
   - maintain Python protocol simulator/golden-model tests for frame/op/result validation.
10. Mandatory test coverage rule:
   - no production RTL/C/C++/bridge code should be merged without corresponding tests,
   - each new module or significant behavior change must include at least one focused testbench update.

## Legacy mapping stability requirements

To preserve migration safety, these behavior classes must remain stable:

- passthrough enter/exit/scan intent mapping,
- motor speed/control command mapping,
- ESC settings/flash tunnel behavior via stream AXID (`0x05`),
- discovery/capabilities parity via `HELLO` + `GET_CAPS`,
- dynamic I/O control/state via `READ_BLOCK`/`WRITE_BLOCK` spaces.

Stability rules:

1. Same user-visible outcomes for critical workflows across legacy baseline and ASP path.
2. Passthrough safety semantics remain equivalent.
3. Error/result behavior remains deterministic and documented.

## Cross-repo contract

- Host tooling repos consume ASP as client/adapter.
- Wire/protocol changes are defined in this repo first, then propagated.
- Avoid duplicated canonical ASP spec copies in companion repos.

## Quality gates

- New code paths must include testbench coverage before merge.
- Every protocol-critical block has a dedicated test with documented pass criteria.
- Parser/framing/CRC/router tests pass in simulation.
- Python protocol simulator/golden-model tests pass for encode/decode, CRC, resync, and op-level mapping.
- Capability/discovery behavior is deterministic and versioned.
- Backward-safe migration path remains documented.

Canonical gate mapping and evidence expectations are maintained in `ASP_VALIDATION_MATRIX.md`.

Release decision flow and sign-off checkpoints are maintained in `ASP_RELEASE_PROCESS.md`.

## Definition of done (execution checklist)

### A) Legacy baseline complete

- Legacy workflows for critical operations are validated and documented as reference outcomes.

### B) ASP protocol readiness complete

- ASP parser/resync/CRC/AXID behavior validated in simulation.
- Protocol simulator bench validates frame semantics and operation/result mapping.
- `CONTROL` migration-critical operations implemented.
- `HELLO` + `GET_CAPS` implemented with versioned capability payloads.
- `READ_BLOCK`/`WRITE_BLOCK` spaces defined and tested.
- Cross-transport semantic equivalence validated (SPI primary, alternate links optional).

### C) Deterministic control-path target complete

- Control-plane viability demonstrated at target clock profile without soft-CPU dependency in hot path.
- RTL owns byte-stream fast path (sync/length/CRC/routing/FIFO).
- Deterministic latency and error handling observed in integration tests.
- Block-level suite passes for major blocks before full integration sign-off.

### D) Next-stage gate (go/no-go)

Proceed only when:

1. Sections A, B, and C are satisfied.
2. Legacy baseline vs ASP outcomes are parity-checked for critical workflows.
3. Remaining deltas are documented with owners and closure plan.
