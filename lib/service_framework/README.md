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

## Deliberately not adopted as service orchestration

- `LifecycleManager` / `WorkerThread` remain lower-level primitives, not a second service lifecycle layer.
- `ServiceTransportFactory` is not part of the adopted framework surface; services use local factories when transport construction needs domain-specific dependencies.
- Service-specific health/event semantics stay service-owned unless multiple services truly share them.
