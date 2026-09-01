# service_framework

`service_framework` is intentionally small and policy-light.

## Supported surface

- `ServiceBase` — owns transport registration, ordered startup, rollback, reverse-order stop, and ready-state fan-out.
- `IServiceTransport` — runtime-owned transport interface for service-local adapters such as D-Bus or stdout.
- `DaemonRunner` — wraps process lifecycle around a `Startable` service.
- `IServiceConfig` / `GflagsConfig` — configuration access for executable composition roots.

## Ownership rules

- Apps are composition roots: build the service, create concrete inputs/transports, and register transports.
- Services own orchestration, worker threads, query bindings, and readiness transitions.
- `ServiceBase::start()` runs after service-specific initialization has bound query interfaces and prepared owned state; it then starts transports in registration order.
- `ServiceBase::stop()` publishes `ready=false` once per transition and stops transports in reverse registration order.
- Services must stop worker threads and input runtimes before calling `ServiceBase::stop()` so transport adapters detach safely through `ServiceBinding`.
- No worker thread is ever detached. A lifecycle call made from a service's own worker thread (or from a worker callback) is rejected with a stable diagnostic instead of detaching or faking a clean stop.

## Adopted lifecycle path

There is exactly one adopted lifecycle path, split across `lifecycle_runner` and
`service_framework`:

```text
LifecycleCoordinator serializes complete service lifecycle transitions.
ManagedWorker owns service worker-thread mechanics.
ServiceBase owns transport ordering, readiness, and query quiescence.
Services own domain work, dependencies, health, issues, and restart policy.
```

Both production services — `InventoryService` and `ObservationService` — adopt
`lifecycle_runner::LifecycleCoordinator` and `lifecycle_runner::ManagedWorker`.
Neither owns a raw `std::jthread` for its worker.

- `ManagedWorker` provides mechanics only: launch, cooperative stop request,
  wake, serialized join, exit capture, and reap/restart. It contains no domain
  knowledge and never decides worker health.
- `LifecycleCoordinator` provides service-epoch serialization only:
  `stopped`/`starting`/`running`/`stopping`, concurrent lifecycle-call
  coordination, and RAII transition completion. It never decides worker health
  either.
- Services keep their own restart and degradation policy. Inventory requires an
  explicit `stop()` before restarting after a refresh-worker crash
  ("Policy A"); observation treats an aging-worker crash as degradation and
  keeps readiness. These differences are deliberate and service-owned.

`lifecycle_runner` must not grow an event bus, a generic component
orchestrator, a publication queue, or domain knowledge.

## Query quiescence is a structural safety barrier

`IServiceTransport::quiesceQueries()` is `noexcept` and every concrete override
must be `noexcept`. It performs local synchronization only: it closes query
admission and waits for admitted query handlers to finish. It performs no
D-Bus/network I/O, no object unregistration, no disconnect, and destroys no
publication resources — that work belongs to `stop()`.

Worker wake and exit-handler callbacks are different: they are signalling
mechanisms, so violations are caught and logged rather than treated as
structural defects. A swallowed or failed wake may delay stop until the
worker's natural poll/CV wake interval (up to inventory's reconcile interval or
observation's aging interval). That containment is degradation tolerance, not
free recovery.

## Intentional two-level state

Two lifecycle states exist on purpose and are **not** merged:

- `LifecycleCoordinator::State` — complete service-epoch serialization.
- `ServiceBase` running/readiness state — transport and readiness lifecycle.

Unifying them is explicitly out of scope for this migration.

## Not part of the adopted surface

- `ServiceTransportFactory` is not part of the adopted framework surface;
  services use local factories when transport construction needs
  domain-specific dependencies.
- Service-specific health/event semantics stay service-owned unless multiple
  services truly share them.
