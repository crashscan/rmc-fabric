# PR 2 notes — copy-first inventory contract extraction

This PR copies transport-neutral inventory contract constants into
`interop_contract` and cuts over a small set of low-risk consumers.

## ADR rules exercised

- Rule 1: Three-layer rule
- Rule 3: Residency rule
- Rule 4: Dependency-direction rule

## Why copy-first

- `InventoryFields.h` currently mixes wire-facing and service/domain types
- `InventoryDbusCodec` is transport-specific because it depends on `DBus::Variant`
- `deriveIssues(SourceState)` is domain behavior, not contract codec logic

Therefore this PR extracts only the stable transport-neutral constants first.

## Explicit non-moves

The following remain outside `interop_contract` in this PR:

- `InventoryDbusCodec.h/.cpp`
- `deriveIssues(...)`
- `SourceState`
- `InventorySnapshot`
- `InventoryDiff`

## Declared shape

- copy new contract-owned inventory constants into `interop_contract`
- cut over low-risk constant consumers only
- no behavior changes
- no transport codec movement
- no domain derivation movement
