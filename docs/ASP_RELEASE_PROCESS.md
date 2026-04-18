# ASP Release Process

This document defines release checkpoints and decision rules for ASP-related milestones in AbstractX.

## Scope

Applies to:

- protocol-impacting changes (`ASP_PROTOCOL.md`),
- SPI transport-impacting changes (`ASP_SPI_TRANSPORT.md`),
- requirement/gate changes (`ASP_REQUIREMENTS.md`, `ASP_VALIDATION_MATRIX.md`).

## Release types

- **Patch**: clarifications, non-breaking bug fixes, no wire/profile semantic change.
- **Minor**: additive capability or non-breaking behavioral expansion.
- **Major**: breaking wire/profile semantics, default-profile switch, or incompatible operation/result changes.

## Pre-release checklist

Before candidate tag:

1. Required validation gates pass per `ASP_VALIDATION_MATRIX.md`.
2. Evidence artifacts are generated and linked.
3. Protocol/transport/requirements docs are mutually consistent.
4. Compatibility statement is explicit (`asp-compat-v1` vs `asp-native`).
5. Known deltas are documented with owner and milestone.

## Candidate workflow

1. **Cut candidate branch/tag** (e.g., `asp-vX.Y.Z-rc1`).
2. **Run validation suites** and generate evidence bundle.
3. **Review gate table**:
   - any REQUIRED gate failing => NO-GO,
   - stale/missing evidence => NO-GO.
4. **Protocol owner sign-off** for semantics.
5. **Release owner sign-off** for readiness decision.

## Release evidence bundle

Recommended minimum bundle:

- validation summary markdown,
- machine-readable results (json/junit),
- commit SHA + date,
- simulator/toolchain versions,
- declared active profile and capability version.

Suggested path conventions:

- `docs/evidence/releases/<version>/summary.md`
- `docs/evidence/releases/<version>/results.json`

## Versioning and compatibility notes

- Default profile remains `asp-compat-v1` unless explicitly changed in a gated release.
- Switching default to `asp-native` requires:
  - documented rationale,
  - compatibility/migration plan,
  - successful gate evidence,
  - explicit major/minor decision note.

## Post-release tasks

- Update protocol revision lines where applicable.
- Update changelog/release notes with gate evidence references.
- Record follow-up deltas and deferred items.

---

*Revision: 1.0 (Apr 2026)*
