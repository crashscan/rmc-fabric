# RMC Fabric Libraries

Shared libraries for the `rmc-fabric` ecosystem.

This directory contains reusable libraries used by RMC Fabric services and clients, including generic D-Bus helpers, service contracts, and typed client APIs.

## Goals

- Provide a clean boundary between generic infrastructure and service-specific APIs
- Avoid duplicating D-Bus connection, variant, and signal handling logic
- Offer stable typed client interfaces for RMC Fabric services
- Keep internal implementation details out of downstream consumers

## Library Layout

### `dbus-support`
Generic D-Bus helper utilities.

Examples of what belongs here:
- bus connection helpers
- proxy/object helper wrappers
- variant/map readers and writers
- signal subscription helpers
- generic serialization/deserialization helpers
- common error handling utilities

This library should remain service-agnostic.

### `fabric-contracts`
Shared RMC Fabric service contracts.

Examples of what belongs here:
- bus names
- object paths
- interface names
- public DTO/value types used across service boundaries
- version constants
- shared wire-format conversion helpers

This library defines the public contract surface for RMC Fabric services.

### `network-observation-client`
Typed client API for the `network-observation` D-Bus service.

Examples of what belongs here:
- `NetworkObservationClient`
- typed wrappers for service methods
- typed wrappers for service signals
- conversion between D-Bus payloads and public domain types

This library is service-specific and should depend on:
- `dbus-support`
- `fabric-contracts`

## Design Principles

### 1. Keep generic and service-specific code separate
Code that knows about:
- `org.rsc.NetworkObservation`
- `RemoteCandidate`
- `LocalNetworkSnapshot`

does **not** belong in `dbus-support`.

### 2. Share contracts, not internals
Consumers should depend on:
- public DTOs
- public client APIs
- stable D-Bus contracts

They should not depend on:
- transport internals
- model engine internals
- classifier internals
- service-private data structures

### 3. Prefer small focused libraries
Do not combine all libraries into one large shared object unless there is a clear runtime or packaging need.

Recommended approach:
- internal helper libraries may be static
- public client/contracts libraries may be shared if consumed externally

## Intended Consumers

These libraries are intended for:
- RMC Fabric services
- CLI tools
- discovery/inventory components
- future component-specific tools such as BMC or PCI-switch management

## Example Dependency Flow

```text
network-observation-client
  ├── fabric-contracts
  └── dbus-support
```

Service implementations may also depend on:
- `fabric-contracts`
- `dbus-support`

but should avoid depending on client libraries.

## Versioning Guidance

If these libraries are consumed outside this repository:

- treat `fabric-contracts` as a stable public interface
- evolve D-Bus APIs additively where possible
- deprecate old methods/signals before removing them
- avoid breaking DTO changes without version coordination

## Status

This layout is intentionally minimal and expected to evolve as additional RMC Fabric services and clients are added.