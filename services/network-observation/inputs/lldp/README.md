# lldp-observer

`lldp-observer` is an internal static target providing LLDP-backed topology observation. It is linked into `network-observationd` and is not a separately installed runtime library.

## Responsibilities
- consume LLDP data from `lldpd` via `liblldpctl`
- emit typed `LldpObservation` events
- cache MAC-like LLDP identities per interface
- flush interface-scoped LLDP state on link-down / interface removal

## Main classes
- `LldpObserver`
- `LldpdSource`

## Identity handling
LLDP identity resolution currently:
- prefers MAC-like chassis ID
- falls back to MAC-like port ID
- normalizes to lowercase MAC form

Non-MAC LLDP identities are passed through in observations but are not used as cache keys.

## Known limitations
- non-MAC chassis/port identity support is incomplete
- backend currently depends on `lldpd` / `liblldpctl`