# Architecture Decision Records

This directory contains Architecture Decision Records (ADRs) for
cross-cutting, durable technical decisions.

An ADR captures:

- the decision that was made
- the context that forced the decision
- the rules and consequences that follow from it
- the conditions that would reopen the decision

ADRs are decision records, not general design documentation.

Use an ADR when a change:

- establishes a repository-wide or subsystem-wide rule
- constrains future implementations
- records non-obvious trade-offs or non-goals
- should be reviewable later as a policy decision, not rediscovered from code

Do not use an ADR for:

- routine implementation details
- temporary experiments
- feature usage documentation
- code walkthroughs

## Current ADRs

- [Transport-neutral contract layer](ADR-0001-transport-neutral-contract-layer.md)
  - Approved on 2026-08-06
