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

## Worker and lifecycle resilience

Both services share `lifecycle_runner::ManagedWorker` and
`lifecycle_runner::LifecycleCoordinator`.  Neither primitive decides service health; restart and
degradation policy stays service-owned.

- **Inventory (Policy A)** — a repeated `start()` on a healthy running service returns `true`.  If
  the refresh worker has crashed, `start()` throws `std::logic_error` and an explicit `stop()` is
  required to reap and reset the failed epoch before the service can start again.
- **Observation** — an aging-worker crash is degradation, not a hard failure.  It is reported as
  `observation.worker.aging.stopped`, readiness is preserved, and a repeated `start()` remains a
  no-op.
- **Worker exceptions** are captured, never propagated out of the thread entry point.  The exit
  handler runs on the worker thread with the worker identity still valid, and must not drive its own
  worker's lifecycle.
- **Wake and exit-handler exceptions** are contained and logged.  A swallowed wake degrades stop
  latency to the worker's natural wake interval; it cannot deadlock or lose the stop request.
- **`ExitReason::returned` versus `stop_requested`** is advisory only — a worker return can race a
  stop request — so services must not use it as an authoritative synchronization fact.
- **No detach path exists.**  Self-stop from a worker thread or worker callback is rejected before
  shutdown is claimed.
- **Concurrent `stop()` waits for actual completion**, and abandoned transitions always resolve to
  `stopped`, so lifecycle state cannot wedge in `starting` or `stopping`.

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
