# Roadmap status

## Delivered guarantees

- **Phase 1** — explicit component boundaries and transport-neutral contracts
- **Phase 2** — testable service/library extraction and thinner composition roots
- **Phase 3** — shared lifecycle structure around `ServiceBase`
- **Phase 4** — public install/export surfaces, lifecycle hardening, neutral client errors
- **Phase 5** — rollback/startup hardening, transport failure isolation, safer client/result APIs
- **Phase 6** — hermetic D-Bus integration, compatibility fixtures, fuzz smoke, package validation
- **Phase 7** — clarified runtime health semantics, additive observation issues, structured diagnostics
  conventions, resilience labeling, short soak coverage, explicit reconnect policy, and CI runner fixes

## Confirmed architecture debt removed

- removed the unused `ServiceHealthTracker` parallel abstraction
- eliminated ambiguity between readiness and active runtime issues for network-observation
- added explicit resilience/soak test groups and scheduled/manual longer runs

## Genuine future product work

- broad package install/upgrade/uninstall smoke coverage in disposable roots/containers
- SBOM generation and automated dependency vulnerability submission in release CI
- benchmark baseline comparison tooling and CI summaries
- measured async/backpressure rework only if synchronous fan-out proves insufficient
- broader deterministic failure-injection seams for every low-level Linux/D-Bus failure path
