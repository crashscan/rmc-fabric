# ADR: Public contract and client compatibility

- **Status:** Approved
- **Date:** 2026-08-31

## Context

Phase 4 and 5 exposed installed public headers, CMake package exports, transport-neutral
result/error types, and bounded codec ingress. Those surfaces now constrain future
repository-wide changes because external consumers may compile against them and may
persist or branch on error codes, issue codes, and wire DTO keys.

## Decision

Treat the following as stable public API/contract surfaces unless a deliberate breaking
change is approved:

- exported targets `rmc_fabric::interop_contract`, `rmc_fabric::inventory-client`,
  and `rmc_fabric::network_observation_client`
- installed public headers for the contract layer and clients
- `ClientErrorCode`, `DecodeErrorCode`, contract constants, and stable inventory issue codes
- D-Bus object names, interface names, method names, signal names, and payload semantics

Additive changes are allowed when they preserve existing semantics and source compatibility.
Breaking changes require:

1. updating the relevant version constant,
2. updating the API snapshot and golden fixtures,
3. updating `docs/compatibility.md`,
4. documenting migration impact in a new ADR or ADR revision.

## Consequences

- Numeric enum and error-code stability is test-enforced.
- Golden fixtures become part of the review surface for any contract change.
- Static public clients are checked for source compatibility and contract behavior rather than shared-library ABI.
