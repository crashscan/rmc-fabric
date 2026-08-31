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

## Asynchronous Refresh shutdown semantics

A D-Bus `Refresh()` call admitted before query quiescence may finish enqueueing its asynchronous
work request.  A refresh already running may complete and publish results before the terminal
`ReadyChanged(false)`.  A refresh that is merely pending in the event queue may be discarded once
the worker has been stopped.

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
