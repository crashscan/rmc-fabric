# inventory-agentd

`inventory-agentd` is the authoritative source of stable RMC identity/inventory
metadata.

It provides:
- a current inventory snapshot
- semantic invalidation events for source-owned fields
- source health/state
- readiness state for deployment and health checks

It does **not** own:
- interface/IP state
- LLDP
- link changes
- runtime network topology

That remains the responsibility of `network-observationd`.

## Source ownership

V1 sources:
- `device-meta-file` -> `deviceClass`, `deviceModelId`, `deviceProject` (optional, JSON, monitored)
- `node-name-file`   -> `nodeName` (optional, monitored)
- `firmware-file`    -> `firmwareVersion` (mandatory-by-design, non-fatal, monitored, readiness-gating)
- `uuid-file`        -> `uuid` (mandatory-by-design, non-fatal, monitored, readiness-gating)
- `software-file`    -> `softwareVersion` (optional, reconcile-refreshed)

## Semantic truth table — source outcomes

### Invariants

- Merge happens per source: on successful collect, the source's owned fields are erased, then replaced by the collected set. On failed collect, owned fields are untouched (last-known-good retained).
- Signals fire only on change: `InventoryChanged` for added/changed/removed source-owned fields; `SourceStateChanged` only on `health` / `stale` / `lastError` transitions (never on timestamp movement).
- Readiness latches once (all required sources OK at least once → ready forever in v1).
- Version/timestamp bump only on non-empty field diff.

### A. Collect outcomes (per refresh cycle)

| # | Trigger | Source health | Owned fields in snapshot | Signals | Readiness |
|---|---------|---------------|--------------------------|---------|-----------|
| 1 | Success, full owned set returned | OK, stale=false, lastError cleared | Replaced with new values | `InventoryChanged(f)` per changed/added field | Required: counts toward latch |
| 2 | Success, subset returned (owned field omitted) | OK | Omitted owned fields removed; rest replaced | `InventoryChanged` for changed + removed | Counts toward latch |
| 3 | Success, empty set returned | OK | All owned fields removed | `InventoryChanged` per removed field | Counts toward latch |
| 4 | Malformed content (bad JSON, empty scalar) | FAILED + lastError | Retained (last-known-good) | `SourceStateChanged` on transition only | Required: blocks latch if never succeeded |
| 5 | Missing file | FAILED + lastError | Retained | `SourceStateChanged` only | Same as 4 |
| 6 | Undeclared extra fields in result | OK (with warning log) | Undeclared fields dropped; declared set merged per rows 1–3 | Only for declared-field changes | Counts toward latch |
| 7 | Reserved metadata key in result (`version` / `timestamp` / `ready` / `phase`) | FAILED + lastError | Retained | `SourceStateChanged` only | Blocks latch if required |
| 8 | First-ever success after FAILED (recovery) | FAILED → OK | Replaced | `SourceStateChanged` + `InventoryChanged` if values differ from retained | Required: may complete latch |

Recovery does not itself imply `InventoryChanged`; only field delta does.

### B. Startup / absence / staleness

| # | Situation | Behavior |
|---|-------|----------|
| 9 | Optional source file missing at first boot | FAILED, field simply absent (no last-known-good exists). No `InventoryChanged` (nothing to remove). Ready still latches once required sources succeed. |
| 10 | Required source file missing at first boot | FAILED. Daemon keeps running; GetReady()==false and GetPhase()=="initializing" indefinitely — until the source succeeds at least once. Periodic reconcile does not resolve this by itself; the latch completes only on a successful collect (typically via the inotify watch firing when the file appears, or the next reconcile after restore). Monit does not restart: supervision checks D-Bus responsiveness, not readiness. On recovery expect SourceStateChanged, then InventoryChanged if values differ, then ReadyChanged(true) — in that order. |
| 11 | `now - lastSuccessTs > staleAfterSec` (manager-side, scheduled) | Health → DEGRADED, `stale=true`. Values retained. `SourceStateChanged` on transition. |
| 12 | inotify watch init/re-arm fails | Log only. Source fully functional via periodic reconcile; no health impact. Watch is an optimization, not a dependency. |

### C. Explicitly rejected alternatives

| Alternative | Verdict | Reason |
|---|---|---|
| Missing optional file ⇒ field removal | Rejected | Conflates "source broken" with "field intentionally absent"; transient filesystem races must not erase identity data. |
| Source failure ⇒ drop fields | Rejected | Failure must never amplify into data loss. |
| Timestamps trigger `SourceStateChanged` | Rejected | Self-DoS signal storm. |

### D. Source strictness requirement

V1 sources must distinguish **invalid content** from **intentional omission** conservatively.

- Empty scalar files are invalid and must fail collection.
- Mandatory-by-design scalar files such as `/etc/rmc/uuid` and `/etc/rmc/firmware` must contain non-empty values when present.
- `device-meta.json` is optional; its fields may be omitted intentionally, but if present they must be non-empty strings of the expected type.
- A source may omit owned fields only when omission is an intentional, valid representation of state.

This preserves the contract that:
- success may remove fields
- failure never removes fields

## End-to-end expectations

### Startup
Expected:
- `InventoryChanged` for each initial source-owned field that is successfully collected
- `SourceStateChanged` for each initial source state appearance
- `ReadyChanged(true)` emitted last, but only after all readiness-gating sources (`firmware-file`, `uuid-file`) have succeeded at least once

### Rename-replace node name
Expected:
- `InventoryChanged: nodeName`

### Delete firmware file
Expected with current semantics:
- `SourceStateChanged: firmware-file`
- `GetField("firmwareVersion")` still returns last-known-good value
- no `InventoryChanged`

### Restore firmware file
Expected:
- `SourceStateChanged: firmware-file`
- `InventoryChanged: firmwareVersion` if value differs from retained one

### Missing required files at startup
Expected:
- daemon starts and answers D-Bus
- `GetReady()==false`
- `GetPhase()=="initializing"`
- `GetIssues()` contains `firmware-file` and/or `uuid-file` with severity `error`
- Monit does not restart the daemon solely because readiness is false

## Readiness vs liveness

`GetReady()` and `GetPhase()` describe the **data currently being served**.
They do **not** guarantee that the refresh pipeline is still running.

Operationally:

- `GetReady()` is a **latch**: once all required sources have succeeded at
  least once, readiness stays true in v1.
- Loop thread death does **not** clear readiness.
- `GetPhase()` remains the last known phase; v1 defines no dedicated
  `"degraded"` phase.
- Pipeline liveness is reported through `GetIssues()`.

### Loop failure issue

If the background refresh loop terminates unexpectedly, `GetIssues()` exposes
a synthetic service-level issue under the stable code:

- `inventory.loop.stopped`

This means:

- the daemon process may still be alive
- D-Bus queries may still succeed
- the served inventory may become arbitrarily stale

Consumers requiring freshness must combine signals, for example:

- usable = `GetReady()` && !hasIssue(`inventory.loop.stopped`)

Issue codes beginning with `inventory.` are stable API. Consumers should match
on the issue code, never on the free-form message text.

### Recovery

Loop failure recovery requires an explicit daemon restart (`stop()` then
`start()`). In v1, calling `start()` again without teardown is rejected.

## Sandbox test
Example sandbox layout mirroring the production file split:

```bash
mkdir -p /tmp/inv-test/rmc /tmp/inv-test/info
echo '{"device_class":"compute-node","device_model_id":"711","device_project":"ocean2"}' \
  > /tmp/inv-test/info/device-meta.json
echo "rack12-node7"  > /tmp/inv-test/info/node-name
echo "2.7.1"         > /tmp/inv-test/rmc/firmware
echo "1234-5678"     > /tmp/inv-test/rmc/uuid
echo "1.0.21"        > /tmp/inv-test/rmc/software

./inventory-agentd --transport=stdout \
  --device_meta_path=/tmp/inv-test/info/device-meta.json \
  --node_name_path=/tmp/inv-test/info/node-name \
  --firmware_path=/tmp/inv-test/rmc/firmware \
  --uuid_path=/tmp/inv-test/rmc/uuid \
  --software_path=/tmp/inv-test/rmc/software
```

Then:
- `echo "rack12-node8" > /tmp/.n && mv /tmp/.n /tmp/inv-test/info/node-name`
- observe `InventoryChanged: nodeName = rack12-node8`

## GetIssues()

Curated operator-facing view of *active* problems — one issue per source,
`a{sa{sv}}` keyed by source name with `severity` / `message` / `origin`.

Severity: FAILED+required → `error`; FAILED+optional → `warning`; stale → `warning`.
OK (not stale) → no issue. No dedicated signal: subscribe `SourceStateChanged`,
refetch `GetIssues()` on it.