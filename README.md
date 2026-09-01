# rmc-fabric

`rmc-fabric` is a collection of system services, libraries, and tools for discovering, correlating, and managing RMC-connected devices on a host.

The project is intended to provide a shared fabric of runtime information across multiple components, including network discovery, inventory, and future device-specific management services.

## Overview

`rmc-fabric` brings together multiple cooperating services that communicate over D-Bus and share a common view of local and remote system state.

Examples of responsibilities include:

- local network state observation
- remote device discovery
- inventory collection
- topology and relationship tracking
- event publication for higher-level management components

The long-term goal is to make it easier for RMC components on a system to discover each other, exchange structured state, and build higher-level management workflows on top of shared system knowledge.

## Repository Structure

This repository is organized as a modular monorepo.

Actual top-level structure:

```text
services/
  network-observation/
    core/                   — pure observation model and domain types
    service/                — runtime orchestration and query/transport ports
      ports/
    inputs/
      netlink/              — Linux netlink ingestion
      lldp/                 — LLDP ingestion
    transports/             — service transports
    clients/                — typed client adapters
    apps/                   — thin composition roots
  rmc-inventory/
    core/                   — inventory manager, diffs, readiness semantics
    service/                — service lifecycle and internal ports
      ports/
    inputs/
      files/                — file-backed inventory sources
    transports/             — service transports
    clients/                — typed client adapters
    apps/                   — thin composition roots

lib/
  interop_contract/         — header-only wire types and contracts
  dbus_transport_base/      — reusable D-Bus service adapter base
  dbus_client_support/      — reusable D-Bus client helpers (header-only)
  service_framework/        — adopted lifecycle surface (`ServiceBase`, `IServiceTransport`, `DaemonRunner`)
  lifecycle_runner/         — shared lifecycle primitives (`ManagedWorker`, `LifecycleCoordinator`, `Startable`)
  daemon_support/           — signal handling for daemon processes
  file_watcher/             — inotify file watcher
```

## Design Principles

### Modular monorepo
The repository is intended to keep closely related services and shared contracts together, while preserving clear boundaries between:
- service implementations
- public contracts
- reusable infrastructure
- tools and tests

The two current services intentionally share the same broad layer shape
(`core`, `service`, `inputs`, `transports`, `clients`, `apps`) while still
allowing different internal decomposition where their domains genuinely differ.
`network-observation` is expected to stay more decomposed internally because it
owns a richer model/runtime/input stack; `rmc-inventory` may remain flatter
until similar complexity appears.

### D-Bus as the integration boundary
Services communicate over D-Bus using typed methods and signals.

This allows components to:
- evolve independently at runtime
- expose query and event APIs
- avoid tight in-process coupling

### Shared contracts, not shared internals
Libraries in this repository should expose:
- stable service contracts
- typed client APIs
- reusable transport helpers

They should avoid exposing:
- internal model engine details
- service-private implementation classes
- tightly coupled internal state

That means the project prefers a small deliberate public package surface and
does not require every internal helper target to be installed or supported for
external consumers.

### Service-oriented composition
The repository is expected to support both:
- core fabric services
- future component-specific consumers such as BMC, PCI-switch, or inventory/management tools

## Current / Expected Components

### `network-observation`
Observes local and remote network state and publishes typed query/event interfaces over D-Bus.

Examples:
- local interfaces
- IP/MAC relationships
- FDB evidence
- LLDP evidence
- remote candidate correlation

### `rmc-discovery`
Higher-level service responsible for discovering and correlating RMC-related devices using available system signals and fabric state.

### `rmc-inventory`
Inventory-oriented service for exposing structured component and device information.

### Shared libraries
Examples include:
- generic D-Bus support utilities
- public D-Bus contracts
- typed service clients
- common helpers used across services

## When to Add New Components Here

A component is a good fit for `rmc-fabric` if it:

- participates in the shared runtime discovery/management model
- depends on common D-Bus contracts or shared service state
- is deployed together with the rest of the RMC system
- evolves closely with existing fabric services

Component-specific tools may later move to separate repositories if they become independently owned or released, but the default approach is to keep tightly related services together until a real need to split emerges.

## Build and Packaging

### Quick start

Requires: cmake ≥ 3.25, ninja, libgoogle-glog-dev, libgflags-dev, libjsoncpp-dev, nlohmann-json3-dev, libsigc++-3.0-dev, liblldpctl-dev, libunwind-dev, DBusCxx (distro package or source build), plus the externally provided `rsc_util` and `lldpctl.hpp` environment dependencies used by `network-observation`.

```bash
# Developer build with tests
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

# Optimised release
cmake --preset release
cmake --build --preset release
```

See [docs/architecture.md](docs/architecture.md) for all available presets (including ASan/UBSan and coverage) and install/package consumption instructions.

Recommended general approach:
- keep internal helper libraries small and focused
- publish stable contracts and client APIs intentionally
- avoid one large catch-all shared library unless there is a strong packaging/runtime reason

## Documentation

See [docs/architecture.md](docs/architecture.md) for:
- supported public libraries and include paths;
- internal target dependency graph;
- D-Bus contracts and wire semantics;
- service lifecycle invariants;
- how to configure, build, and test via presets;
- how to consume installed CMake packages.

Operational follow-up documents:

- [docs/operations.md](docs/operations.md)
- [docs/resilience.md](docs/resilience.md)
- [docs/dependencies.md](docs/dependencies.md)
- [docs/roadmap-status.md](docs/roadmap-status.md)

Architecture Decision Records are in [docs/adr/](docs/adr/).

## Future Direction

`rmc-fabric` is intended to become the common coordination layer for RMC-related services on a system.

Possible future areas include:
- richer device discovery workflows
- topology-aware correlation
- component-specific management integrations
- inventory synchronization
- event-driven orchestration across services

---

## Architecture Notes

### D-Bus Adapter Lifetime Invariant

Both `InventoryDbusAdapter` and `NetworkObservationDbusAdapter` expose service
query methods over D-Bus.  The D-Bus dispatcher calls handler methods on its
own thread(s) while the service lifecycle thread may call `onTransportStopping()`
concurrently.

**Invariant:** the query-service pointer must not be dereferenced after
`onTransportStopping()` returns.  Null-checking the pointer without
synchronization is insufficient because the check and dereference are not
atomic with respect to the racing write.

**Implementation:** `ServiceBinding<T>` (`lib/dbus_transport_base/ServiceBinding.h`)
uses a `std::shared_mutex`:
- `acquire()` takes a *shared* lock for the duration of one handler call.
- `detach()` takes a *unique* lock, blocking until all in-flight shared holders
  release.  When `detach()` returns, no handler can access the service.
- Deadlock risk is absent because service methods never call back into the
  transport layer.

### Service framework

See `lib/service_framework/README.md` for the supported framework surface and lifecycle ownership rules.
