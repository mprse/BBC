# ADR 0009: P2P Framing and Handshake

- Status: Accepted
- Date: 2026-09-04

## Context

Stage 7.1 introduces the first untrusted peer TCP connection. TCP does not
preserve message boundaries, so BBC needs canonical framing, strict allocation
limits, network separation, version negotiation, duplicate-connection handling,
and a minimal liveness exchange before transaction or block relay is added.

## Decision

- Adopt the canonical version 1 format in
  [`docs/p2p-protocol-v1.md`](../p2p-protocol-v1.md).
- Use a fixed 44-byte header and a maximum 1 MiB payload.
- Protect each payload with its full SHA-256 digest. Treat this only as an
  integrity check, never as peer authentication.
- Require a bidirectional `HELLO` exchange before other messages.
- Check both chain ID and Genesis Block ID during the handshake.
- Use a random process-session nonce for self-connection detection and
  deterministic duplicate-connection selection.
- Begin with only `HELLO`, `PING`, and `PONG`; later message types require their
  own canonical payload specifications.
- Keep explicit peer topology in the scenario runner and perform connections
  over the real P2P listener, never through the control data path.

## Consequences

- Frame parsing and message serialization become compatibility-sensitive code
  and require fixed byte-vector tests.
- Development-network nodes disconnect cleanly from other network identities or
  unsupported protocol versions.
- A full SHA-256 checksum adds 32 bytes to every frame but reuses the existing
  primitive and avoids introducing a second checksum algorithm.
- The first transport is intentionally unauthenticated and unencrypted. Object
  signatures and consensus validation remain mandatory, and private wallet
  material is never sent to peers.
- Regtest still needs a separately approved network identity, Genesis Block, and
  Proof-of-Work target before deterministic mining scenarios are implemented.
