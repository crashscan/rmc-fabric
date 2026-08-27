# Phase A checklist — transport-neutral contract layer

Guiding sequence:

> Prove the boundary, freeze the wire, then refactor behind frozen tests.

## Global tripwires

1. **PR 3 asserts structure, not raw wire bytes**
   - Characterization fixtures must validate deterministic structure from the extracted pure seam:
     - type signatures
     - key sets and ordering
     - value types
     - value contents
   - Do not capture raw live-bus bytes that include unstable headers or serials.

2. **PR 4 fixtures are immutable**
   - Any change to a PR 3 fixture during PR 4 is presumed to be a regression.
   - If a fixture must change, the PR description must explicitly justify why, and the change must be treated as a contract-impacting event, not routine refactor fallout.

---

## PR 1 — `interop_contract` skeleton

**ADR rules exercised:** 4, 5, 10

### Acceptance criteria
- Contract target exists and builds.
- Contract target has zero transport linkage.
- Contract tests link only `interop_contract`.
- CI/source gate is live for transport-isolation backstop.
- Existing service code builds unchanged.

### Must not contain
- Wire-facing type moves.
- Transport includes in contract code.
- Generic helpers with no immediate PR 2 consumer.
- Full speculative neutral variant model unless directly required next.

---

## PR 2 — move issue contract types

**ADR rules exercised:** 1, 3, 4

### Acceptance criteria
- Diff is move/rename only, verifiable by inspection.
- Existing tests pass unmodified.
- PR description includes the residency-rule predecision.

### Must not contain
- Behavior edits mixed into the move.
- `deriveIssues(...)` relocation.
- `SourceState`-driven logic in `interop_contract`.
- Any `SourceState` reference inside `interop_contract`.

---

## PR 3 — characterization/golden tests

**ADR rules exercised:** 8

### Acceptance criteria
- Fixtures are captured from pre-refactor behavior.
- Assertions are against the extracted pure seam, not raw live-bus bytes.
- Assertions cover:
  - signature
  - keys
  - ordering
  - value types
  - value contents
- Production changes are limited to behavior-preserving seam extraction needed for testability.

### Must not contain
- Adapter moves.
- Translation changes.
- Byte-level live-wire captures.
- Contract behavior edits disguised as test setup.

---

## PR 4 — adapter extraction

**ADR rules exercised:** 2, 5, 6, 10

### Acceptance criteria
- Two-step change:
  1. relocate D-Bus hosting/adapter code unchanged
  2. translate leaf mappings to contract-layer types
- PR 3 fixtures remain unmodified.
- `dbus-cxx` usage exists only under the adapter path.
- CI/source gate proves transport isolation still holds.
- PR 3 characterization tests stay green throughout.

### Must not contain
- Fixture edits unless explicitly justified as regression/contract-impacting.
- Move and translate mixed together in one commit.
- Transport-library leakage above adapter boundary.
