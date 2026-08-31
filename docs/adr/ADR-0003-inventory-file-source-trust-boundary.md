# ADR: Inventory file-source trust boundary

- **Status:** Approved
- **Date:** 2026-08-31

## Context

Inventory data enters the daemon from filesystem sources that may be missing, stale,
replaced, or attacker-controlled. Phase 6 requires a durable policy for what kinds of
filesystem objects are accepted and how they are validated without TOCTOU-prone
pre-checks.

## Decision

Inventory file sources are opened first and then validated on the opened descriptor.

The daemon accepts only regular files and rejects:

- symlinks
- directories
- FIFOs and other non-regular file types
- files larger than the configured ingress ceiling (currently 64 KiB)

Atomic rename-replacement is explicitly supported. Read failures change source health
and issues, but must not delete last-known-good inventory values from the snapshot.

## Consequences

- Source tests must cover symlink, directory, FIFO, oversize, and rename-replace cases.
- Logs may describe the path and bounded failure text, but must not include source file contents.
- Future changes to the acceptance policy require updating this ADR and `docs/security.md`.
