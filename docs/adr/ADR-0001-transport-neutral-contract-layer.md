# ADR: Transport-neutral contract layer

- **Status:** Approved
- **Date:** 2026-08-06

## Context

Current service implementations expose transport-library details too far upward.
In particular, D-Bus-facing code currently relies on `dbus-cxx` types close to
domain-facing logic and codecs. That makes transport replacement or transport
addition unnecessarily invasive.

A plausible near-future requirement is either:

1. supporting a different D-Bus library, or
2. supporting a second transport such as MQTT, gRPC/RPC, REST, or another
   message bus.

The design goal is not to predict every future transport perfectly. The goal is
to contain transport-specific change to adapter layers, so domain logic and
wire-facing contracts do not need to be rewritten when transport choices change.

## Decision

Introduce a shared, transport-neutral contract layer for wire-facing types,
contract constants, and schema-directed codecs.

This layer is separate from:

- domain/service behavior and runtime state, and
- transport-specific adapter code.

The architecture is explicitly three layers:

1. **Domain layer**
   - strong domain types
   - behavioral logic
   - lifecycle/runtime state

2. **Contract/port layer**
   - wire-facing DTOs
   - contract constants
   - neutral value model
   - schema-directed codecs and validation
   - neutral error/result model

3. **Transport adapter layer**
   - mapping between the neutral contract layer and concrete transport/library
     types
   - D-Bus library bindings, MQTT payload mapping, RPC/protobuf mapping, etc.

## Rules

1. **Three-layer rule**
   - The architecture is domain -> contract -> adapter/wire for data flow;
     dependencies point inward per Rule 4.
   - Domain logic must not depend directly on transport-library types.

2. **Domain working-model rule**
   - Domain code uses strong domain types as its working model.
   - The neutral value/object model is not the internal representation of domain
     logic.
   - Neutral DTO/value types exist only at the port boundary between codecs and
     transport adapters.

3. **Residency rule**
   - A type lives in the contract layer if and only if it appears on the wire.
   - Wire-facing types and their codecs live in the contract layer.
   - Behavioral/runtime state stays in the service/domain layer.

4. **Dependency-direction rule**
   - Dependency direction is `service -> interop_contract <- adapter`.
   - The contract library depends on nothing above it.

5. **Transport-isolation rule**
   - The contract layer must not expose third-party transport/library types.
   - D-Bus, MQTT, gRPC, REST, or other transport-specific types are confined to
     adapter layers.

6. **Error-boundary rule**
   - No transport-specific exception or error type may cross an adapter
     boundary.
   - Adapters map transport/library failures into a neutral error/result model.
   - Error codes are stable API; error messages are diagnostic text.

7. **Schema-directed fidelity rule**
   - The neutral contract model preserves semantic values, not every
     transport-native width/detail.
   - Exact wire-format fidelity for arbitrary foreign payloads is a non-goal.
   - Integer width, null/absence handling, and similar wire details are
     re-encoded from schema rules, not inferred solely from the neutral value.

8. **Determinism rule**
   - Encoders produce canonical output for a given contract object.
   - Canonical ordering of maps/sets is part of the contract-layer behavior and
     supports characterization/golden tests.

9. **Bounded-ingress rule**
   - Contract decoding and validation must apply bounded recursion depth and
     bounded collection/object sizes on ingress.

10. **Build-enforcement rule**
    - Neutrality must be enforced first by the build target graph, then by CI
      checks.
    - Transport-library dependencies are linked only to adapter targets.
    - The contract layer must not include or link transport-specific libraries.
    - CI/source checks are a backstop, not the primary mechanism.

## Consequences

### In scope for the contract layer

Examples of types/components that belong in the contract layer when they appear
on the wire:

- `InventoryIssue`
- `InventoryFields`
- contract constants
- wire-facing DTOs
- schema-directed encode/decode logic for wire-facing DTOs

### Out of scope for the contract layer

Examples that remain domain/service-local:

- manager implementations
- source implementations
- watcher internals
- thread/lifecycle state
- refresh scheduling and caches
- service-host runtime behavior

Wire-facing changes now carry a deliberate test cost across schema/codecs,
characterization or golden tests, and adapter conformance tests. This is an
intended consequence of making wire contracts explicit and stable.

### Allowed transport adapters

This decision permits transport-specific adapters such as:

- `dbus_cxx_adapter`
- a future alternate D-Bus adapter
- `mqtt_adapter`
- `grpc_adapter`
- `rest_adapter`

This decision does **not** require pre-designing a universal runtime transport
API.

## Non-goals

- Creating a universal `ITransportAdapter` abstraction spanning object buses,
  RPC, and pub/sub runtimes.
- Moving domain behavior into string-keyed neutral object bags.
- Guaranteeing signature-faithful round-trip of arbitrary foreign wire payloads.
- Providing a zero-cost abstraction. The neutral contract/value layer is
  allocation-heavy by design and is acceptable for current inventory/observation
  payload sizes.

## Phase A review rubric

Phase A implementations should be reviewed against the rules above:

- Which numbered rules does this change exercise?
- Does it violate any numbered rule?
- Does it move any transport-specific type above an adapter boundary?
- Does it move any non-wire behavioral type into the contract layer?

The ADR is a review aid, not only a descriptive note.

## Reopen conditions

This decision is reopened by either:

1. the first implementation of a second transport, or
2. any proposed breaking wire-format change.

Expected model adjustment when a second transport arrives is not, by itself, a
failure of this design. The success criterion is containment: needed changes
should remain within the contract layer and adapter implementations. If changes
must leak into domain/service logic, the layering decision should be revisited.
