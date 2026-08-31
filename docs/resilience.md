# Resilience

## Failure model

- Inventory keeps its existing readiness latch and reports loop death via `inventory.loop.stopped`.
- Network-observation keeps readiness independent from runtime degradation and now reports active
  runtime issues through `GetIssues()`.
- Transport publication failures are isolated per transport and logged/reported on transition.

## Event delivery / backpressure

Current event publication remains synchronous fan-out.

- Ordering is preserved exactly as exercised by the existing tests.
- Later transports still receive an event after an earlier transport throws.
- Repeated failures are logged on transition instead of every event loop iteration.

Phase 7 does **not** introduce an asynchronous queue because the current tests/benchmarks rely on
ordered synchronous delivery and this change has not yet been justified by measurement.

## Short soak

Short PR-CI soak coverage now runs `service_soak_short_tests`.

It repeatedly starts and stops `ObservationService`, then reports:

- cycle count
- elapsed time
- file-descriptor count before/after
- file-descriptor growth

Manual/scheduled longer variants can increase `RMC_FABRIC_SOAK_CYCLES`.

## Client restart policy

- `network_observation` `DbusClient` does **not** auto-reconnect in the background.
- After disconnect or service restart, callers should explicitly call `tryConnect()` / `connect()`.
- Existing inventory client compatibility methods still keep their established fallback behavior.

## Deferred work

The following larger items remain intentionally deferred to future work:

- injected clock seams across all stale/aging logic
- deterministic transport backpressure benchmarking with a bounded async dispatcher
- package-upgrade container smoke coverage
- full long-run fuzz/performance governance beyond the new scheduled/manual jobs
