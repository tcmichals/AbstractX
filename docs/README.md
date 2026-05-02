# AbstractX Documentation Map

Use this page as the entry point for ASP protocol and governance documentation.

## Canonical protocol stack

1. **`ASP_PROTOCOL.md`**  
   Normative ASP wire/protocol behavior.
2. **`ASP_SPI_TRANSPORT.md`**  
   Normative SPI transport profile for ASP framing and stream handling.
3. **`ASP_SPI_REGISTER_MAP.md`**  
   Concrete SPI command/status/register byte-level contract.
4. **`ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md`**  
   Translator->fabric topology, internal packet routing, DMA/Wishbone endpoint model.
5. **`ASP_REQUIREMENTS.md`**  
   Mission, must-have requirements, quality gates, and definition-of-done.
6. **`ASP_VALIDATION_MATRIX.md`**  
   Gate-by-gate validation matrix and evidence expectations.
7. **`ASP_RELEASE_PROCESS.md`**  
   Candidate, sign-off, and release decision workflow.

## Strategy and migration context

- **`ASP_SPEC_DIRECTION.md`**  
   Protocol direction and legacy-to-ASP migration policy.

## Implementation scaffolding

- **`../include/asp_spi_protocol.h`**  
   C constants/helpers for SPI command bytes, status bits, and `READ_STATUS` parsing.
- **`../rtl/spi/asp_spi_reg_bank.sv`**  
   RTL command-decode/state-machine skeleton for SPI seam integration (`WRITE_DATA`, `READ_STATUS`, `READ_DATA`).
- **`../python/asp_tun_bridge.py`**  
   Unified Python userspace TUN bridge with SPI and DMA backend selection.
- **`../python/TUN_FRAMEWORK.md`**  
   Operational notes and rationale for the Python-first / Rust-next TUN + DMA path.
- **`../rust/tun_dma_bridge/README.md`**  
   Rust userspace scaffold notes for the long-term hardened TUN + DMA bridge.
- **`../hw/qmtech_zynq7020/README.md`**  
   QMTECH Zynq-7020 bring-up notes, Pico/XVC references, and build-output conventions.

## Testbench assets

- **`../sim/cocotb/test_abstractx_axis_cocotb.py`**  
   Initial AbstractX AXIS seam cocotb testbench for SPI reg-bank ingress/egress valid-ready behavior.

## Suggested reading order

- New implementers: `ASP_PROTOCOL.md` → `ASP_SPI_TRANSPORT.md` → `ABSTRACTX_SWITCH_FABRIC_ARCHITECTURE.md`
- Verification owners: `ASP_REQUIREMENTS.md` → `ASP_VALIDATION_MATRIX.md`
- Release owners: `ASP_RELEASE_PROCESS.md`
- Architecture/migration reviewers: `ASP_SPEC_DIRECTION.md`
