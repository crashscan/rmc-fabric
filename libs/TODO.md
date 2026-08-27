

Not by default. **Keep the current `dbus-client` in `rmc-fabric` if it is a client for `org.rsc.NetworkObservation` or other `rmc-fabric` services.** Move only the **generic D-Bus plumbing** to the utility library.

## Good split
### In utility library
Put only reusable, service-agnostic pieces:
- variant/map readers like `DbusVariantMapReader`
- connection/session helpers
- common proxy wrappers
- signal subscription helpers
- serialization helpers
- generic error translation
- constants only if they are framework-level, not service-level

### In `rmc-fabric`
Keep service-specific code:
- `DbusClient` for `org.rsc.NetworkObservation`
- bus name / object path / interface constants
- methods like:
    - `getLocalSnapshot()`
    - `getRemoteCandidateMacs()`
    - `getCandidateByMac()`
- DTO mapping between D-Bus payloads and `RemoteCandidate`, `LocalInterfaceState`
- service contract versioning

---

## Rule of thumb
If the code “knows” about:
- `RemoteCandidate`
- `LocalNetworkSnapshot`
- `org.rsc.NetworkObservation`
- object path `/org/rsc/NetworkObservation`

then it is **not generic utility code**.

If the code only knows about:
- D-Bus connection mechanics
- calling methods
- parsing `a{sv}`
- subscribing to signals

then it **can** live in the utility library.

---

## Best architecture
A 2-layer split:

### Utility library
Example:
- `dbus-support`
- `dbus-utils`
- `ipc-dbus`

Contains generic infrastructure.

### `rmc-fabric`
Example:
- `network-observation-client`
- `fabric-dbus-client`
- service contract wrappers

Contains typed clients for your services.

---

## Why this is better
If you move the whole `DbusClient` into the utility library, that library stops being a utility library and becomes coupled to:
- one service domain
- one product namespace
- one evolving contract

That makes it harder to reuse cleanly and harder to version independently.

---

## Recommended refactor
From what you showed, I would split like this:

### Move to utility lib
Potential candidates:
- `DbusUtils.h/.cpp`
- `DbusVariantMapReader`
- maybe generic invoke wrappers
- maybe generic signal wrapper helpers

### Keep in `rmc-fabric`
- `DbusClient.h/.cpp`
- service constants:
    - `DBUS_SERVICE`
    - `DBUS_PATH`
    - `DBUS_INTERFACE`
- `fromVariantMapIface`
- `fromVariantMapCandidate`

Although if you want, you can further split those mappers into a fabric contract module.

---

## Even better naming
Instead of one generic `DbusClient`, consider naming by role:

- `NetworkObservationClient`
- `FabricObservationClient`
- `RmcDiscoveryClient`

That makes it obvious it is **service-specific**, not a generic platform D-Bus abstraction.

---

## Final recommendation
**Do not move the whole current `DbusClient` to the utility library.**  
Move only the generic D-Bus support layer there, and keep the typed service client in `rmc-fabric`.

If you want, I can suggest a **clean directory split** between:
- generic utility D-Bus code
- `rmc-fabric` service contracts
- typed service clients.

Not necessarily. **Don’t make `libs/` one giant shared object by default.**  
A better default is:

- **separate libraries by responsibility**
- prefer **static libs for small internal components**
- use **shared `.so` only for stable, reused external/runtime-facing layers**

## Best practical recommendation
For your case, I’d structure it like this:

### Good default
- `libdbus_support` — generic D-Bus helpers
- `librmc_fabric_contracts` — service DTOs/constants/contracts
- `librmc_fabric_client` — typed client wrappers
- optionally other focused libs for shared subsystems

Not:
- one mega `librmc_all.so`

---

## Why not one single `.so`
A single shared object for all libs tends to:
- increase coupling
- blur ownership boundaries
- force unnecessary relinking/redeployment
- make ABI/versioning harder
- pull unrelated dependencies into all consumers

Especially if later you split out tools like BMC or PCI-switch management, you do **not** want them depending on a giant catch-all runtime library unless they truly need all of it.

---

## A useful rule
### Use a shared library when:
- multiple binaries use it
- you want one installed runtime artifact
- ABI stability matters
- it is a public or semi-public dependency layer
- startup/runtime footprint tradeoffs make sense

### Use a static library when:
- it is internal-only
- it is small
- ABI stability is not worth managing
- you want simple packaging
- it is mostly implementation sharing, not a public interface

---

## For your system specifically
I would likely do:

### 1. Generic D-Bus support
- **static lib** if only your internal services/clients use it
- **shared lib** only if many independently packaged tools consume it

Example:
- `libdbus-support.a` initially

### 2. Fabric contracts
If these are used across repos or by external component tools:
- this is the strongest candidate for a **small shared library**
- or a header-only/contracts package if mostly constants/types

Example:
- `librmc-fabric-contracts.so`
  or just installed headers if no real implementation

### 3. Typed clients
If other repos/tools will consume them:
- could be a **shared lib**
  Otherwise:
- static is fine

Example:
- `librmc-network-observation-client.so`
  or `.a`

---

## My preferred setup
### Inside the monorepo
Build most internal libs as:
- **OBJECT** or **STATIC** libraries

Then expose only a few intentional public libraries as `.so`.

This keeps internals flexible.

### Public surface
You probably only want a small number of installable public artifacts:
1. **contracts**
2. **client SDK(s)**

---

## Suggested layering
```text
libs/
  dbus-support/            -> static/internal
  fabric-contracts/        -> public, maybe shared
  network-observation-client/ -> public, maybe shared
  common-utils/            -> static/internal
```

### Typical build policy
- `common-utils`: static
- `dbus-support`: static
- `fabric-contracts`: header-only or small shared/static
- `network-observation-client`: shared if externally consumed, else static

---

## If you expect separate repos later
If `rmc-bmc` and `rmc-pci-switch` will live elsewhere and consume your platform:
- publish a **small stable shared client/contracts library**
- keep everything else internal

That means:
- not a single `.so` for all libs
- but maybe **one `.so` per supported SDK surface**

For example:
- `librmc-fabric-contracts.so`
- `librmc-fabric-client.so`

That is much cleaner than a monolith.

---

## Bottom line
**No — `libs/` should not automatically compile into one single shared object.**  
Better to build **multiple focused libraries**, with:

- **internal helpers as static libs**
- **public contracts/clients as small intentional shared libs only if needed**

If you want, I can sketch a **CMake layout** for this library split.