# Dependencies

## Policy

- Keep direct dependencies explicit and reviewable.
- Prefer distro packages for local development where available.
- Keep local development usable without mandatory network scanners.
- Update dependency versions intentionally together with test/CI verification.

The machine-readable direct dependency inventory is in [`dependencies.json`](dependencies.json).

Two current non-vendored environment dependencies are still expected from the build environment:

- `rsc_util`
- `lldpctl.hpp` compatible with the LLDP observer sources

## Ownership expectations

- Build/test dependency updates must keep the default presets working.
- D-Bus contract or client-surface changes must update fixtures and compatibility tests.
- CI-only dependency changes must keep PR CI bounded and deterministic.

## Update policy

1. update the declared dependency inventory;
2. update CI/install paths if package names changed;
3. run targeted build/test coverage for affected presets;
4. update release/governance notes when externally downloaded artifacts or workflows change.

## Current scope

Phase 7 adds the dependency inventory and CI fixes needed for current Ubuntu runners.
SBOM generation, dependency vulnerability submission, and broader release-governance automation are
tracked as follow-on work rather than being introduced speculatively in this change.
