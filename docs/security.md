# Security boundaries

## Trust boundaries

- D-Bus payloads are untrusted ingress and must decode through bounded codecs.
- Inventory file sources are trusted only after descriptor-based validation.
- Public clients must not receive transport-library exceptions across the adapter boundary.

## Inventory file-source policy

`FileBackedInventorySource` now opens files by descriptor and validates the opened
object instead of using a check-then-open path flow.

Accepted:

- regular files
- atomic rename-replacement of a watched file

Rejected:

- symlinks
- directories
- FIFOs and other non-regular files
- oversized files (greater than 64 KiB)

Failure to read a source must not erase last-known-good inventory values.

## Deployment assets

- D-Bus policy files allow only the intended well-known name ownership by `root`.
- Default policy allows request/response traffic only to the service names and signal receipt from those names.
- Packaging scripts and integration tests use temporary files under `/tmp` and remove them after use.
- Readiness helper files use `mktemp` rather than predictable names.

## Accepted risks

- Public clients are static libraries, so ABI drift is not independently versioned.
- Inventory source paths themselves may still appear in diagnostics; file contents must not.
- Optional package formats should only be claimed in CI when they are actually built and validated.
