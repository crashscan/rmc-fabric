# TODO

## High priority
- [ ] Check `DBus::Connection::request_name()` result and fail if not primary owner
- [ ] Replace hard-coded `liblldpctl.so` usage in CMake with `pkg-config` / imported targets
- [ ] Re-enable bulk `GetRemoteCandidates` D-Bus API and client path
- [ ] Add explicit `<sys/socket.h>` includes to tests using `AF_INET`

## Feature work
- [ ] Implement `GatewayLike` classification
    - [ ] observe default routes / gateway IPs
    - [ ] feed gateway IPs into the model
    - [ ] classify matching candidates as `GatewayLike`
- [ ] Implement daemon flags for `candidateAgeout` / `candidateExpire`
- [ ] Add `GetStats` query surface
    - [ ] counts by status
    - [ ] counts by classification
    - [ ] current phase
    - [ ] last age run timestamp

## Model/runtime improvements
- [ ] Erase long-expired candidates after retention threshold
- [ ] Make `StdoutTransport::running_` atomic
- [ ] Emit visibility updates on `setClassifier()` if classifier swapping becomes externally exposed
- [ ] Add reconnect logic for netlink monitor failure
- [ ] Add reconnect / restart policy for LLDP source failure

## API / transport improvements
- [ ] Add bulk remote candidate fetch to reduce N+1 client sync
- [ ] Consider event coalescing / dirty-set batching if signal volume becomes high
- [ ] Decide whether D-Bus startup should tolerate `AlreadyOwner` or fail hard unless `PrimaryOwner`

## Data model improvements
- [ ] Support multi-port FDB fidelity: key by `(MAC, port)` instead of MAC only
- [ ] Support non-MAC LLDP identities
- [ ] Revisit `emitDeviceIfIpOnly` config: implement or remove

## Tests
- [ ] Add expiry signaling test (`Expired` -> `CandidateRemoved`)
- [ ] Add `prepareForRestart()` idempotency test
- [ ] Add restart/local snapshot reset test
- [ ] Add D-Bus startup failure/name-conflict test