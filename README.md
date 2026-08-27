# rmc-fabric

`rmc-fabric` is a collection of system services, libraries, and tools for discovering, correlating, and managing RMC-connected devices on a host.

The project is intended to provide a shared fabric of runtime information across multiple components, including network discovery, inventory, and future device-specific management services.

## Overview

`rmc-fabric` brings together multiple cooperating services that communicate over D-Bus and share a common view of local and remote system state.

Examples of responsibilities include:

- local network state observation
- remote device discovery
- inventory collection
- topology and relationship tracking
- event publication for higher-level management components

The long-term goal is to make it easier for RMC components on a system to discover each other, exchange structured state, and build higher-level management workflows on top of shared system knowledge.

## Repository Structure

This repository is organized as a modular monorepo.

Typical top-level structure:

```text
services/
  network-observation/
  rmc-discovery/
  rmc-inventory/

libs/
  dbus-support/
  fabric-contracts/
  <service-specific clients>

tools/
  <cli and debug utilities>

tests/
  unit/
  integration/
```

> Exact directory names may evolve as the project grows.

## Design Principles

### Modular monorepo
The repository is intended to keep closely related services and shared contracts together, while preserving clear boundaries between:
- service implementations
- public contracts
- reusable infrastructure
- tools and tests

### D-Bus as the integration boundary
Services communicate over D-Bus using typed methods and signals.

This allows components to:
- evolve independently at runtime
- expose query and event APIs
- avoid tight in-process coupling

### Shared contracts, not shared internals
Libraries in this repository should expose:
- stable service contracts
- typed client APIs
- reusable transport helpers

They should avoid exposing:
- internal model engine details
- service-private implementation classes
- tightly coupled internal state

### Service-oriented composition
The repository is expected to support both:
- core fabric services
- future component-specific consumers such as BMC, PCI-switch, or inventory/management tools

## Current / Expected Components

### `network-observation`
Observes local and remote network state and publishes typed query/event interfaces over D-Bus.

Examples:
- local interfaces
- IP/MAC relationships
- FDB evidence
- LLDP evidence
- remote candidate correlation

### `rmc-discovery`
Higher-level service responsible for discovering and correlating RMC-related devices using available system signals and fabric state.

### `rmc-inventory`
Inventory-oriented service for exposing structured component and device information.

### Shared libraries
Examples include:
- generic D-Bus support utilities
- public D-Bus contracts
- typed service clients
- common helpers used across services

## When to Add New Components Here

A component is a good fit for `rmc-fabric` if it:

- participates in the shared runtime discovery/management model
- depends on common D-Bus contracts or shared service state
- is deployed together with the rest of the RMC system
- evolves closely with existing fabric services

Component-specific tools may later move to separate repositories if they become independently owned or released, but the default approach is to keep tightly related services together until a real need to split emerges.

## Build and Packaging

Build, packaging, and deployment details are expected to be defined per service and per library.

Recommended general approach:
- keep internal helper libraries small and focused
- publish stable contracts and client APIs intentionally
- avoid one large catch-all shared library unless there is a strong packaging/runtime reason

## Future Direction

`rmc-fabric` is intended to become the common coordination layer for RMC-related services on a system.

Possible future areas include:
- richer device discovery workflows
- topology-aware correlation
- component-specific management integrations
- inventory synchronization
- event-driven orchestration across services

## Status

This repository is under active development, and both structure and APIs may evolve as the fabric model becomes more clearly defined.