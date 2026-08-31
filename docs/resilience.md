# Resilience

## Failure model

- Inventory keeps its existing readiness latch and reports loop death via `inventory.loop.stopped`.
- Network-observation keeps readiness independent from runtime degradation and now reports active
  runtime issues through `GetIssues()`.
- Transport publication failures are isolated per transport and logged/reported on transition.

## Shutdown safety

Both services implement a structured shutdown ordering to prevent use-after-free and guarantee
signal ordering:

### ServiceBinding (query-admission gate)

The `ServiceBinding<T>` template now uses an explicit admission flag and active-count rather than
a shared mutex.  This is starvation-free: once `detach()` closes admission, a continuous stream of
new readers cannot delay it.  Callers hold only a reference count during calls; no mutex is held
when blocking on drain.

### Producer-drain postconditions

`ILldpSource::stop()` guarantees on return:
- no admitted callback is executing;
- no new callback will be admitted until a successful restart;
- watch/subscription handle is released;
- cached neighbor state is cleared.

`IObservationRuntime::stop()` guarantees on return:
- all producer threads (netlink, LLDP) are stopped and drained;
- no `IModelEventSink` call is active or will occur until restart;
- the event sink pointer is cleared.

### Terminal ReadyChanged(false) ordering

`ReadyChanged(false)` is emitted *after* all producers have drained and *before* transports are
closed.  `ReadyChanged(true)` is rejected once shutdown is claimed.  No domain signal or
readiness-true transition may occur after the terminal false transition.

## Event delivery / backpressure

Current event publication remains synchronous fan-out.

- Ordering is preserved exactly as exercised by the existing tests.
- Later transports still receive an event after an earlier transport throws.
- Repeated failures are logged on transition instead of every event loop iteration.

This PR does **not** introduce an asynchronous publication queue; that remains deferred work.

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
- full concurrent lifecycle state machine (start-during-stop, stop-during-start with wait semantics)
