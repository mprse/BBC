# ADR 0010: Network Profiles and the First Mining Race

- Status: Accepted
- Date: 2026-09-04

## Context

The normal development target is intentionally slow enough to observe two
miners racing, but it makes deterministic integration tests impractical. Mining
workers also need a clear authority for template creation and block acceptance.

## Decision

- Keep chain 1 as `development`, with magic `BBC 01` and the existing 24-bit
  zero-prefix target.
- Add isolated chain 2 as `regtest`, with magic `BBC 02` and a 12-bit target.
- Derive a distinct Genesis Block ID from each chain ID.
- Use one full node as template provider and block authority in the first
  vertical slice; connect two wallet miners to it as outbound-only peers.
- Mine in bounded 10,000-attempt batches and cancel stale work.
- Add template request, template response, block submission, accepted block,
  and explicit result messages to P2P protocol version 1.
- Treat a late valid competing solution as stale, not as peer misconduct.

## Consequences

Automated tests finish quickly while the development demonstration remains
visibly slow. The first winner receives the height-1 reward and the other miner
stops. This is still a linear-chain prototype: fork storage, cumulative-work
selection, reorganization, transaction propagation, and synchronization remain
later stages.
