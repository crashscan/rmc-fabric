# apps

This directory contains executable applications for the network observation subsystem.

## Applications

### `network-observationd`
Daemon process that:
- owns the observation model
- starts source pipelines
- publishes state over configured transports
- runs periodic candidate aging

### `net-observe`
CLI tool that:
- queries local interface state
- queries remote candidates
- watches daemon events
- optionally filters to LLDP-backed candidates