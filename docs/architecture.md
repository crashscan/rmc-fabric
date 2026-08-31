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
| `inventory_dbus_codec`             | `rmc_fabric::inventory_dbus_codec`             | `include/rmc_fabric/inventory/`              |
| `network_observation_client`       | `rmc_fabric::network_observation_client`       | `include/rmc_fabric/network_observation/`    |
| `network_observation_dbus_codec`   | `rmc_fabric::network_observation_dbus_codec`   | `include/rmc_fabric/network_observation/`    |

All other targets (`inventory_service`, `inventory_transport`, `network_observation_service`, transports, inputs, apps) are internal and are **not** exported.

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

- `start()` — starts transports and worker threads; idempotent after a successful start.
- `stop()` — stops all components in reverse order; idempotent.
- Destructor — calls `stop()` to prevent dangling threads.
- Readiness — published via the D-Bus `ReadyChanged` signal. Emitted at most once per `start()`/`stop()` cycle.

**Failed `start()`:** any transport or worker-thread creation failure rolls back already-started components and leaves the service in a clean state. A subsequent `start()` is allowed unless the service documents that restart is unsupported.

**Shutdown ordering:** transports are stopped before worker threads. `ServiceBinding` ensures in-flight D-Bus handler calls complete before service pointers are cleared.

## Error/result policy (ADR-0001)

- Adapter boundaries must not let `dbus-cxx` exceptions escape.
- The contract layer (`lib/interop_contract/`) must not include transport-specific types.
- Codec decode failures return empty/default values and log at `ERROR`.
- "Service unavailable" returns empty/default values and logs at `WARNING`.

See [ADR-0001](adr/ADR-0001-transport-neutral-contract-layer.md).

## Building and testing

### Prerequisites

```
cmake >= 3.25
ninja
libglog-dev libgflags-dev libjsoncpp-dev
libdbus-cxx-dev libsigc++-2.0-dev
liblldpctl-dev
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
