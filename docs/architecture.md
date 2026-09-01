# rmc-fabric Architecture

This document describes the current (post-Phase 3) architecture of `rmc-fabric`.

## Service topology

```
Public consumers
  -> rmc_fabric::interop_contract          (wire types, D-Bus constants)
  -> rmc_fabric::inventory-client          (typed D-Bus proxy for inventory-agentd)
  -> rmc_fabric::network_observation_client (typed D-Bus proxy for network-observationd)
  -> rmc_fabric::inventory_dbus_codec      (low-level inventory D-Bus map codec)
  -> rmc_fabric::network_observation_dbus_codec (low-level observation D-Bus map codec)

Internal services
  apps        -> service + selected inputs + transport_factory
  service     -> core + service_ports
  transports  -> service_ports + interop_contract + dbus_transport_base
  clients     -> interop_contract + dbus_client_support + codec
```

No public consumer should require internal service, input, server-transport, or daemon targets.

## Public libraries

| CMake target                       | Exported as                                    | Install include path                         |
|------------------------------------|------------------------------------------------|----------------------------------------------|
| `interop_contract`                 | `rmc_fabric::interop_contract`                 | `<cmake install prefix>/include/`            |
| `inventory-client`                 | `rmc_fabric::inventory-client`                 | `include/rmc_fabric/inventory/`              |
| `inventory_dbus_codec`             | `rmc_fabric::inventory_dbus_codec`             | `include/rmc_fabric/inventory/`              |
| `network_observation_client`       | `rmc_fabric::network_observation_client`       | `include/rmc_fabric/network_observation/`    |
| `network_observation_dbus_codec`   | `rmc_fabric::network_observation_dbus_codec`   | `include/rmc_fabric/network_observation/`    |

The primary supported consumer surface is `interop_contract` plus the two typed
client libraries. The codec targets are also exported intentionally as
low-level helpers for package consumers that need direct D-Bus variant-map
encoding/decoding without the higher-level client wrappers.

All other targets (`inventory_service`, `inventory_transport`, server transports,
`network_observation_service`, inputs, apps) are internal and are **not**
exported.

## Selective consistency policy

The repository intentionally keeps a **shared coarse-grained service shape**
without forcing identical fine-grained structure inside every service.

- Shared expectation:
  - `core` owns domain logic
  - `service` owns orchestration and service-local ports
  - `inputs` own external ingestion
  - `transports` own service publication adapters
  - `clients` own public consumer-side proxies
  - `apps` are thin composition roots
- Intentional specialization:
  - `network-observation` keeps a more decomposed internal model layout
    (`core/public`, `engine`, `classifier`, `policy`) because it owns a richer
    runtime and source pipeline
  - `rmc-inventory` remains flatter because its current domain is smaller and
    centered on source merge, readiness, and change publication
- Drift to keep corrected with tests and review:
  - package exports must match the documented public surface exactly
  - post-start transport/source registration is closed by default
  - ambiguous artifacts should either be documented as auxiliary or removed

The goal is therefore **selective specialization with explicit invariants**,
not full internal uniformity.

## Internal target dependency graph (simplified)

```
inventory-agentd
  └─ inventory_service
       ├─ inventory_core
       └─ inventory_service_ports
  └─ inventory_transport
       ├─ inventory_dbus_codec ← interop_contract
       └─ dbus_transport_base
  └─ inventory_file_inputs
  └─ daemon_support

network-observationd
  └─ network_observation_service
       ├─ observation-model  (core)
       ├─ network_observation_service_ports
       └─ service_framework
  └─ network_observation_transport_factory
       ├─ network_observation_dbus_transport
       │    └─ network_observation_dbus_codec ← interop_contract
       └─ network_observation_stdout_transport
  └─ daemon_support
```

## D-Bus contracts

Contracts are defined in `lib/interop_contract/` and are the only layer allowed to cross the service boundary.

| Service                | Bus name                   | Object path                   | Interface                    |
|------------------------|----------------------------|-------------------------------|------------------------------|
| `inventory-agentd`     | `org.rsc.Inventory`        | `/org/rsc/Inventory`          | `org.rsc.Inventory`          |
| `network-observationd` | `org.rsc.NetworkObservation` | `/org/rsc/NetworkObservation` | `org.rsc.NetworkObservation` |

All wire-level types, method names, signal names, and field key constants are in `lib/interop_contract/`.

## Service lifecycle primitives

Service lifecycle mechanics live in `lib/lifecycle_runner/` and are shared by both production
services.  The responsibility split is strict:

| Component | Owns | Never owns |
| --- | --- | --- |
| `ManagedWorker` | worker-thread mechanics: launch, cooperative stop request, wake, serialized join, exit capture, reap/restart | health policy, domain state, service ordering |
| `LifecycleCoordinator` | service-epoch transition serialization: `stopped`/`starting`/`running`/`stopping`, concurrent lifecycle-call coordination, RAII transition completion | worker health, transports, readiness |
| `ServiceBase` | transport registration/startup/rollback/reverse-order close, query quiescence, ready state, terminal `ReadyChanged(false)` fan-out | worker mechanics, epoch serialization |
| Inventory / observation services | domain work loops, runtime/source dependencies, error/issue policy, readiness meaning, restart and crash policy | thread mechanics, epoch serialization |

`lifecycle_runner` deliberately contains no event bus, no generic component orchestrator, no
publication queue, and no domain knowledge.

### Primitive mechanics versus service policy

The primitives never decide whether a service should restart.  Both services observe worker exit
through a `ManagedWorker` exit handler and then apply their own policy:

- **Inventory Policy A** — a repeated `start()` on a *healthy* running service returns `true`, but a
  repeated `start()` while the refresh worker has crashed throws `std::logic_error` and requires an
  intervening `stop()` to reap and reset the failed epoch.  Inventory owns this through its
  `loopFailed_` flag; the coordinator only reports that the epoch is already running.
- **Observation degradation policy** — an aging-worker crash is service-owned degradation.  It is
  surfaced as an issue (`observation.worker.aging.stopped`), readiness is not cleared, and a
  repeated `start()` remains a no-op.

### `ManagedWorker` contract highlights

- `start()`, `stop()`, and `join()` are internally serialized; concurrent `stop()`/`join()` callers
  cannot double-join and both return only after the worker has finished.
- `start()` defines all three prior states: running (returns `false`), finished-but-unjoined (reaps
  the completed thread, then launches), and never-started (launches).  A finished thread is reaped
  with an explicit join rather than move-assignment, because move-assigning a `std::jthread`
  implicitly requests stop and joins *without* invoking the configured wake callback.
- `requestStop()` is safe from any thread including the worker, and never joins.
- `join()` and `stop()` reject execution from the worker thread with a deterministic
  `std::logic_error`.  **No detach path exists anywhere.**
- Worker exceptions never escape the thread entry point; they are captured into `Exit`.
- The exit handler runs **on the worker thread** after `running=false` and `lastExit` are recorded,
  and the worker's thread id stays valid for the whole handler invocation so `isCurrentThread()` and
  self-operation detection work inside it.
- The exit handler must not call `start()`, `stop()`, or `join()` on its own worker.
- `Wake` and `ExitHandler` must not throw and must not block; `std::function` cannot enforce
  `noexcept`, so violations are caught and logged.
- `ExitReason::returned` versus `ExitReason::stop_requested` is **advisory only**: a worker return
  can race a stop request.  Services must not treat it as an authoritative synchronization fact.
- Thread construction failure propagates from `ManagedWorker::start()` and leaves the object
  restartable.  Services catch it, roll startup back, log, and return `false`.

### Member declaration and destruction order

> If worker callbacks capture the owning object, the `ManagedWorker` member must be declared **after**
> every sibling member those callbacks access, so reverse member destruction destroys the worker
> first.

Both `InventoryService` and `ObservationService` keep their `ManagedWorker` as the last relevant
member with an explanatory comment at the declaration site.

### `LifecycleCoordinator` contract highlights

- `beginStart()` claims `starting` from `stopped`; from `running` it returns an unowned transition so
  the service can apply its own health policy; during `starting`/`stopping` it waits for resolution
  and re-evaluates.
- `beginStop()` claims `stopping` from `running`; from `stopped` it returns unowned; during
  `starting` it waits for startup to resolve and then claims stop only if startup succeeded.
- **Asymmetric abandoned transitions**: an abandoned or unresolved *start* resolves `starting →
  stopped`, and an abandoned or unresolved *stop* resolves `stopping → stopped`.  An abandoned stop
  is never rolled back to `running`, and state is never left stuck in `stopping`.  This is safe
  because teardown is structurally non-throwing: quiescence is `noexcept`, self-stop is rejected,
  worker join is serialized, and final cleanup failures are isolated per step.
- The coordinator mutex is never held while `ServiceBase`, runtime, worker, transport, or domain code
  runs, and every success/failure/exception path notifies waiters.

### Intentional two-level state

`LifecycleCoordinator::State` (complete service-epoch serialization) and `ServiceBase` running/ready
state (transport and readiness lifecycle) both exist **on purpose**.  Merging them is explicitly out
of scope; see `lib/service_framework/README.md`.

## Service lifecycle invariants

Both services inherit `ServiceBase` which provides:

- `start()` — validates configuration, initializes service-owned state, starts transports, then starts service-owned workers.
- `stop()` — stops service-owned workers before transport teardown; idempotent.
- Destructor — calls `stop()` to prevent dangling threads.
- Readiness — published via the D-Bus `ReadyChanged` signal. Emitted at most once per `start()`/`stop()` cycle.
- Health/issues — service-owned degradation is surfaced through service-specific issue queries rather than
  by redefining readiness semantics.
- Registration mutation — transports and sources are registered before `start()`; adding them after start is rejected unless a service explicitly opts into dynamic registration.
- Port boundaries — service ports stay adapter-free; concrete D-Bus/stdout/input types remain outside `service/ports`.

**Failed `start()`:** any transport or worker-thread creation failure rolls back already-started transports in reverse order exactly once, clears service-owned startup state, and leaves the service restartable.

**No callback-originated synchronous owner lifecycle calls:** a service worker, worker exit handler,
LLDP callback, netlink callback, or runtime callback must never synchronously call `stop()` on its
own owner.  Such calls are rejected with a stable diagnostic (never detached, never silently treated
as a clean stop); a component that needs shutdown must raise an external shutdown request instead.

**Shutdown ordering:** service-owned workers stop before state they access is destroyed. `ServiceBinding` is detached from D-Bus transports during transport shutdown, before service dependencies are destroyed. Cleanup failures are logged per step and do not stop later cleanup actions.

## Error/result policy (ADR-0001)

- Adapter boundaries must not let `dbus-cxx` exceptions escape.
- The contract layer (`lib/interop_contract/`) and installed public client headers must not expose transport-specific types.
- Public client `try*` APIs return `interop_contract::ClientResult<T>` with stable codes:
  `service_unavailable`, `timeout`, `transport_error`, `decode_error`, and `invalid_response`.
- `ClientError.message` is diagnostic text only; callers must branch on `ClientErrorCode`.
- Compatibility getters keep their legacy empty/default fallback behavior by delegating through the new `try*` APIs.
- Transport publication fan-out is isolated per transport. Failures are logged with stable service/transport context and later transports still receive the event.
- Codec ingress is bounded by `IngressLimits.hpp` and malformed required fields, wrong types, unknown enums, or oversized collections now raise neutral `DecodeError`s internally.

See [ADR-0001](adr/ADR-0001-transport-neutral-contract-layer.md),
[compatibility policy](compatibility.md), and
[ADR-0002](adr/ADR-0002-public-contract-and-client-compatibility.md).

## Building and testing

### Prerequisites

```
cmake >= 3.25
ninja
libgoogle-glog-dev libgflags-dev libjsoncpp-dev nlohmann-json3-dev libunwind-dev
libsigc++-3.0-dev liblldpctl-dev
# DBusCxx is required and may need to be built from source where no distro package exists.
# network-observation also expects rsc_util and the LLDP C++ wrapper header (lldpctl.hpp).
```

### Using CMake presets

```bash
# Developer build (Debug + warnings-as-errors + all tests)
cmake --preset dev
cmake --build --preset dev
ctest --preset dev

# Optimised release
cmake --preset release
cmake --build --preset release

# Address/UBSan
cmake --preset asan
cmake --build --preset asan
ctest --preset asan
```

Available presets: `dev`, `release`, `asan`, `coverage`.

Additional opt-in presets:

- `integration`
- `fuzz`
- `release-package`
- `benchmark`

CTest labels:

- `unit`
- `architecture`
- `integration`
- `resilience`
- `soak-short`
- `package`
- `fuzz-smoke`

### Manual configure

```bash
cmake -B build -G Ninja \
      -DCMAKE_BUILD_TYPE=Debug \
      -DBUILD_TESTING=ON \
      -DRMC_FABRIC_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

### Architecture verification (no build required)

```bash
sh tools/check_no_transport_in_interop_contract.sh
sh tools/check_component_boundaries.sh .
```

## Consuming installed packages

After `cmake --install build --prefix /install/dir`:

```cmake
find_package(rmc_fabric REQUIRED)
target_link_libraries(my_target PRIVATE rmc_fabric::inventory-client)
```

The generated package config is installed to `<prefix>/lib/cmake/rmc_fabric/`.

CTest now verifies both build-tree and install-tree package consumption by configuring three external projects against:

- `rmc_fabric::interop_contract`
- `rmc_fabric::inventory-client`
- `rmc_fabric::network_observation_client`

Those consumers intentionally fail if internal service or D-Bus transport targets are exported.

See also:

- [testing](testing.md)
- [performance baselines](performance.md)
- [security boundaries](security.md)
- [operations](operations.md)
- [resilience](resilience.md)
- [dependency policy](dependencies.md)
