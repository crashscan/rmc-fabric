# Compatibility policy

`rmc-fabric` treats the following as public compatibility surfaces:

- installed public headers and CMake package targets
- D-Bus service names, object paths, interface names, method names, and signal names
- wire DTO field keys and payload semantics
- `interop_contract::ClientErrorCode` and `interop_contract::DecodeErrorCode`
- stable inventory issue codes beginning with `inventory.`

## Version constants

- `interop_contract::PUBLIC_CLIENT_API_VERSION`
- `interop_contract::inventory::CONTRACT_VERSION`
- `interop_contract::network_observation::CONTRACT_VERSION`

Current policy is major-version style compatibility: additive changes may ship within
contract version `1` as long as existing fields, enum values, error codes, method
names, signal names, and payload meanings remain intact.

## Allowed additive changes

- adding optional DTO fields
- adding new inventory issue codes
- adding new client APIs that do not change existing signatures or semantics
- adding new tests, fixtures, package metadata, and install-time validation

## Breaking changes

The following require a versioned compatibility review and ADR update:

- changing D-Bus signatures or removing methods/signals
- renaming/removing public headers or exported CMake targets
- changing numeric enum/error-code values
- changing payload meaning for an existing field
- removing stable inventory issue codes

## Fixture and snapshot update procedure

1. update or add the golden fixture under `tests/fixtures/`
2. update `tests/fixtures/api-contract-snapshot.json` if the public surface changed
3. update the version constant only for intentional breaking or compatibility-boundary changes
4. update this document and the compatibility ADR with the rationale
5. keep at least one older fixture per contract version readable by current decoders

## ABI note

Public clients are currently installed as static libraries. Header/source compatibility
and contract compatibility are enforced; there is no promise of stable shared-library
ABI for those static archives.
