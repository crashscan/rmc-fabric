# rmc-fabric Architecture

This document describes the current (post-Phase 3) architecture of `rmc-fabric`.

## Service topology

```
Public consumers
  -> rmc_fabric::interop_contract          (wire types, D-Bus constants)
  -> rmc_fabric::inventory-client          (typed D-Bus proxy for inventory-agentd)
  -> rmc_fabric::network_observation_client (typed D-Bus proxy for network-observationd)

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
| `network_observation_client`       | `rmc_fabric::network_observation_client`       | `include/rmc_fabric/network_observation/`    |

All other targets (`inventory_service`, `inventory_transport`, D-Bus codecs/transports, `network_observation_service`, inputs, apps) are internal and are **not** exported.

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

## Service lifecycle invariants

Both services inherit `ServiceBase` which provides:

- `start()` — validates configuration, initializes service-owned state, starts transports, then starts service-owned workers.
- `stop()` — stops service-owned workers before transport teardown; idempotent.
- Destructor — calls `stop()` to prevent dangling threads.
- Readiness — published via the D-Bus `ReadyChanged` signal. Emitted at most once per `start()`/`stop()` cycle.

**Failed `start()`:** any transport or worker-thread creation failure rolls back already-started transports in reverse order exactly once, clears service-owned startup state, and leaves the service restartable.

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

See [ADR-0001](adr/ADR-0001-transport-neutral-contract-layer.md).

## Building and testing

### Prerequisites

```
cmake >= 3.25
ninja
libgoogle-glog-dev libgflags-dev libjsoncpp-dev nlohmann-json3-dev
libsigc++-3.0-dev liblldpctl-dev
# DBusCxx is required and may need to be built from source where no distro package exists.
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
