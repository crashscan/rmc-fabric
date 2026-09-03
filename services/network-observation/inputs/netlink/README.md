# netlink-monitor

`netlink-monitor` is an internal static target providing low-level Linux network-state monitoring over netlink. It is linked into `network-observationd` and is not a separately installed runtime library.

## Responsibilities
- subscribe to live netlink events
- perform initial dumps
- parse:
    - link changes
    - interface address changes
    - neighbor entries
    - bridge FDB entries
- maintain a deduplicated in-memory snapshot

## Main types
- `LinkEvent`
- `InterfaceIpEvent`
- `NeighborEvent`
- `FdbEvent`
- `DeviceEvent`

## Main class
- `NetlinkNetworkMonitor`

## Behavior
- runs an internal worker thread
- delivers callbacks synchronously on the monitor thread
- supports point-in-time snapshots
- supports optional interface filtering for FDB/neighbor events

## Known limitations
- FDB aggregation is MAC-based, not `(MAC, port)`-based
- shared-IP neighbor removal semantics are simplified