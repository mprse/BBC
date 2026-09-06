# BBC Documentation

This directory describes the current BBC implementation. Each reference
document is written to stand on its own and links to more specialized material
when a topic crosses subsystem boundaries.

## Start here

- [`glossary.md`](glossary.md) explains abbreviations and domain terms.
- [`architecture.md`](architecture.md) explains the components, data flow,
  process roles, APIs, persistence, and validation boundaries.
- [`application-config.md`](application-config.md) defines persistent wallet,
  node, miner, peer, and local-command settings.
- [`rpc.md`](rpc.md) explains how another terminal inspects and controls a
  running local node without exposing wallet secrets.
- [`toolchain.md`](toolchain.md) explains supported development tools and build
  procedures for Windows, Linux, and an eventual EC2 host.
- [`local-network-test-plan.md`](local-network-test-plan.md) explains actor roles,
  scenario files, execution, assertions, and artifacts.
- [`persistent-network-test.md`](persistent-network-test.md) provides a manual
  two-node flow through normal configuration and RPC commands.
- [`lan-operation.md`](lan-operation.md) explains safe P2P testing between two
  Windows computers on one private network.

## Data and consensus

- [`wallet-file-format.md`](wallet-file-format.md) — encrypted private-key
  storage;
- [`transaction-format.md`](transaction-format.md) — canonical signed
  transaction bytes;
- [`block-format.md`](block-format.md) — canonical blocks, Genesis, and the
  transaction Merkle root;
- [`proof-of-work.md`](proof-of-work.md) — mining and block-hash validity;
- [`chain-state.md`](chain-state.md) — balances, account nonces, block rewards,
  fork choice, and reorganizations;
- [`chain-store.md`](chain-store.md) — append-only block history and the derived
  SQLite cache;
- [`mempool.md`](mempool.md) — pending-transaction policy and persistence;
- [`network-profiles.md`](network-profiles.md) — isolated development and test
  networks.

## Networking

- [`p2p-protocol-v1.md`](p2p-protocol-v1.md) — TCP frames, peer identity,
  handshake, limits, and connection lifecycle;
- [`transaction-protocol-v1.md`](transaction-protocol-v1.md) — signed
  transaction submission and relay;
- [`mining-protocol-v1.md`](mining-protocol-v1.md) — templates, workers,
  submitted solutions, and cancellation;
- [`sync-protocol-v1.md`](sync-protocol-v1.md) — block locators, bounded block
  transfer, and fork-aware synchronization;
- [`node-control.md`](node-control.md) — loopback-only test control and the
  scenario runner.
- [`rpc.md`](rpc.md) — authenticated local commands for a persistent node.

## Architecture decisions

The [`adr`](adr/) directory contains Architecture Decision Records. An ADR
captures why a durable technical or protocol choice was made, alternatives that
were rejected, and known consequences. Reference documents describe the current
result; ADRs preserve the reasoning that led to it.

## Documentation status

Tested code and these English reference documents are the source of truth for
implemented behavior. [`../BBC_PROJECT.md`](../BBC_PROJECT.md) is the original
Polish concept and roadmap. It is useful background but may describe ideas that
have not been implemented.
