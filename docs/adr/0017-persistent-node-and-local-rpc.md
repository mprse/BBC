# ADR 0017: Persistent Node and Authenticated Local RPC

- Status: Accepted
- Date: 2026-09-06

## Context

Scenario actors already compose chain storage, mempool policy, P2P relay,
synchronization, and mining inside a long-running process. A persistent
installation needs the same behavior but must not depend on generated actor
configuration, in-memory test wallets, or a token embedded in JSON.

A separate terminal must be able to inspect state, start one mining cycle,
submit an already signed transaction, and stop the process. Loading a private
key into the daemon would unnecessarily expand the impact of a node-process
compromise.

## Decision

- Reuse one internal node runtime for persistent applications and scenario
  actors while retaining separate public configuration and command boundaries.
- Start persistent services with `bbc node run --config`.
- Bind application RPC to exact IPv4 loopback and require a 256-bit random token
  stored in a separate file.
- Generate the token on first node start, reuse it afterwards, validate its
  exact encoding, and compare supplied authentication values in constant time.
- Keep wallet decryption and signing outside the long-running process. RPC
  accepts only canonical signed transaction bytes and sends them through normal
  mempool and P2P validation paths.
- Let a combined full node and miner create work from its local validated chain;
  a mining-only process still obtains templates from a full-node peer.
- Keep application RPC distinct from scenario orchestration even though the
  current transport framing is shared.

## Consequences

- The same tested consensus, storage, synchronization, and relay code serves
  scenarios and persistent installations.
- A stolen RPC token permits local node control but does not reveal a private
  wallet key because the daemon never receives one.
- Users operate the background node and short-lived CLI in separate terminals.
- P2P remains loopback-only at runtime until LAN and public-node hardening are
  designed and tested. ADR 0018 subsequently permits an explicit private-LAN
  boundary while public-node operation remains unsupported.
- Continuous mining, automatic configuration creation, and a combined
  create-sign-submit command remain separate product decisions.
