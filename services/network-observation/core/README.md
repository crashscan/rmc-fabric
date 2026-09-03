# core

`core/` owns the transport-neutral observation model. The internal static target `observation-model` correlates netlink and LLDP observations into publishable remote device candidates. It is linked into the daemon and is not installed or exported as an independent library.

## Responsibilities
- track local interfaces, MACs, and IPs
- reject artifacts and local-self observations
- merge multi-source evidence into `RemoteCandidate`
- classify candidates
- manage lifecycle:
    - provisional
    - confirmed
    - aged
    - expired
    - removed
- emit model events for visible candidate changes

## Main pieces
- `ObservationModelEngine`
- `LocalStateTracker`
- `HardFilter`
- `DefaultInterfacePolicy`
- bundled classifiers:
    - `RuleBasedClassifier`
    - `ScoringClassifier`

## Lifecycle model
### Initializing
Candidates stay provisional while startup/restart evidence is rebuilt.

### Live
Candidates with sufficient classification are promoted to `Confirmed`.

### Aging
Aging transitions:
- `Confirmed` -> `Aged`
- `Aged` / `Confirmed` -> `Expired`

## Restart model
`prepareForRestart()`:
- clears local state
- clears source evidence
- preserves candidate identity/timestamps
- demotes candidates to provisional
- emits removal events for previously visible candidates

`markLive()`:
- promotes worthy provisional candidates
- erases non-worthy provisional candidates

## Publishability
A candidate is publishable only if:
- status is `Confirmed` or `Aged`
- classification is not `Artifact`, `LocalSelf`, or `Unknown`