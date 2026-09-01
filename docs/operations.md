# Operations

## Runtime model

`rmc-fabric` now uses four distinct operator-facing terms:

- **liveness** — the process and its service-owned control loops can still make progress.
- **readiness** — the service's served data is currently usable under that service's existing rules.
- **health** — current component degradation/failure state inside the running service.
- **issues** — bounded active problem records intended for operators and automation.

Readiness does **not** imply full health, and liveness failures do **not** silently redefine
existing `GetReady()` / `GetPhase()` meanings.

## Shutdown semantics and signal ordering

Both services follow a strict externally observable shutdown order for each service epoch:

1. **Query quiescence** — D-Bus query admission closes; in-flight `Get*` and `Refresh` handlers
   complete before any service dependency is torn down.  Queries arriving after quiescence return
   safe defaults.
2. **Worker stop and drain** — service-owned worker threads and runtime input producers stop and
   drain.  Domain signals produced by already-admitted work may still be emitted at this stage
   because transports remain open.
3. **Terminal readiness transition** — after all event producers have drained, `ReadyChanged(false)`
   is emitted if the service was ready.  This is the final service-originated signal of the epoch.
4. **Transport close** — D-Bus object unregistration and connection release happen only after the
   terminal readiness signal.

**Intentional ordering note**: query methods become unavailable (step 1) *before* subscribers
receive the terminal `ReadyChanged(false)` (step 3).  Clients that rely on re-querying state after
observing `ReadyChanged(false)` will receive safe defaults or errors.

`ReadyChanged(true)` is rejected once shutdown has been claimed.  No domain signal or
readiness-true transition may occur after the terminal readiness-false transition.

### Query quiescence is a structural barrier

`quiesceQueries()` is `noexcept` on the transport interface and on every concrete override.  It
performs **local synchronization only**: it closes query admission and waits for admitted handlers to
finish.  It performs no D-Bus/network I/O, no object unregistration, no disconnect, and destroys no
publication resources — all of that belongs to `stop()`.  A quiescence violation is a structural
programming defect, not an ordinary recoverable transport error, so teardown must not appear
abortable at that step.

Worker wake and exit-handler callbacks are treated differently.  They are signalling mechanisms, so
callback exceptions are caught and logged and worker state still finalizes.  The cost of a swallowed
or failed wake is latency: stop may be delayed until the worker's natural poll/condition-variable
wake interval, potentially up to inventory's reconcile interval or observation's aging interval.
That containment is degradation tolerance, not free recovery.

### Concurrent `stop()` now waits for completion

**Behaviour change.**  Before the lifecycle-runner migration, a second concurrent `stop()` could
return while teardown was still active.  After the migration, every normal return from `stop()`
means the active teardown has completed and the service epoch is observably `stopped`.

Because a second `stop()` now waits, no in-process component may synchronously call `stop()` on its
own owner.  Worker loops, worker exit handlers, LLDP callbacks, netlink callbacks, and runtime
callbacks that attempt a self stop are **rejected** with a stable operational diagnostic
(`category=self_stop_rejected`).  No worker thread is ever detached, and a rejected self stop
never pretends a clean stop completed.  Components needing shutdown must raise an external shutdown
request instead.

### Worker stop mechanics

- Worker `start()`/`stop()`/`join()` are internally serialized, so concurrent stop and join callers
  cannot double-join.
- A worker that has finished but has not yet been joined is reaped before a new worker launches, so a
  restart after a worker failure still works at the primitive level.
- Fallible final cleanup (transport unregistration, disconnect, close, runtime/source cleanup) is
  exception-isolated per step: one failure cannot prevent later cleanup and cannot leave lifecycle
  coordination stuck in a transitional state.
- An abandoned or unresolved start resolves to `stopped`; an abandoned or unresolved stop also
  resolves to `stopped`.  Lifecycle state can never wedge in `starting` or `stopping`.

## Asynchronous Refresh shutdown semantics

A D-Bus `Refresh()` call admitted before query quiescence may finish enqueueing its asynchronous
work request.  A refresh already running may complete and publish results before the terminal
`ReadyChanged(false)`.  A refresh that is merely pending in the event queue may be discarded once
the worker has been stopped.

The refresh eventfd is closed only *after* the refresh worker has been joined, so no admitted
`Refresh()` can ever signal a closed descriptor, and no worker touches the inventory manager, file
watcher, eventfd, or transports after `stop()` returns.

## LLDP callback lifecycle

The LLDP watch callback captures only a `weak_ptr` to the internal callback state.  `stop()` on
the LLDP source closes the admission gate *before* destroying the watch handle and waits for all
active callback leases to drain before clearing cache state.

Observation callbacks must **not** synchronously drive source lifecycle (e.g. call `stop()` or
`refreshAll()` from within a callback).  Doing so is a programming error and may log a warning or
fail an assertion.

## Inventory semantics

- `GetReady()` remains the existing readiness latch.
- `GetPhase()` remains the existing last-known phase (`initializing` or `live`).
- `GetIssues()` remains the operator surface for active source issues plus the stable service issue:
  - `inventory.loop.stopped`

Loop failure does not clear the readiness latch. Consumers that require fresh data should combine
`GetReady()` with `GetIssues()`.

## Network-observation semantics

- `GetReady()` still means the service has completed its current startup path and can answer queries.
- `GetPhase()` remains `initializing`, `live`, or `stopped`; there is still no separate degraded phase.
- `GetIssues()` is now an additive operator surface for active runtime degradation without changing
  existing query semantics.

Stable network-observation issue codes:

- `observation.runtime.stopped`
- `observation.worker.aging.stopped`
- `observation.input.lldp.unavailable`

Transport publication failures are also surfaced through bounded service-owned issue records using
stable transport component identifiers such as `transport.dbus`.

## Diagnostics

Operational error logs now follow a stable field convention:

- `service`
- `component`
- `operation`
- `category`
- `identity`
- diagnostic `message`

These fields are sanitized and bounded before logging. Issue codes and client error codes are stable;
free-form diagnostic text is not.

## Supervision

Current deployment remains **Monit-first** for both daemons. Readiness probes must not be treated as
liveness probes:

- a daemon can be alive but degraded;
- a readiness failure should not be interpreted as "process is dead";
- repeated restarts should be governed by supervisor policy, not by application readiness alone.

`SIGTERM` is the normal shutdown path. Supervisors should allow a bounded graceful-stop window and
only then escalate.

## Troubleshooting

1. Check whether the service is reachable over D-Bus.
2. Check `GetReady()` / `GetPhase()` without assuming they describe all degradation.
3. Inspect `GetIssues()` for stable issue codes.
4. Use structured logs for component, operation, and bounded identity context.
