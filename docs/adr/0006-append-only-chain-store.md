# ADR 0006: Append-Only Chain Store with a Rebuildable SQLite Cache

## Status

Accepted on 2026-09-04.

## Context

BBC can derive a valid chain from manually supplied block files, but
a full node needs a durable local history and must not require users to list
every block for each query. The store must retain canonical block bytes, detect
corruption and interrupted records, support indexed lookup, and rebuild account
state without making a local database part of consensus.

Storing one operating-system file per block is easy to inspect but scales poorly
and complicates atomic updates. Storing only derived balances would prevent a
node from independently replaying and verifying its history.

## Decision

- Each node owns an independent data directory.
- Canonical block bytes are stored in one append-only `chain/blocks.dat` file.
- Each record has `BBRC` magic, a storage version, reserved bytes, payload size,
  one canonical Block v1 payload, and its SHA-256 checksum.
- Canonical Genesis is the first stored record.
- `chain/chain.db` is a SQLite cache containing the block index, current tip,
  balances, and account nonces.
- The block file is authoritative. SQLite data is rebuilt transactionally from
  the fully validated block file whenever the store opens.
- New blocks are validated against a copy of the chain before any append. A
  runtime database failure rolls the block file back to its prior boundary.
- A missing cache is recreated. Truncated or corrupt authoritative history is
  reported and not modified automatically.
- One process may write a data directory. Valid side branches are stored and
  replayed; multiple writers, segmentation, pruning, and orphan buffering are
  not implemented.

The exact local record layout and recovery behavior are specified in
`docs/chain-store.md`.

## Consequences

- Normal balance and tip queries need only a data-directory path.
- Every node can reconstruct its index and account state from canonical block
  history.
- Local storage corruption is detected before the stored chain is used.
- SQLite provides transactional cache replacement without becoming a consensus
  dependency or source of monetary truth.
- Startup currently costs one complete chain replay; a trusted incremental
  index can be introduced later without changing block consensus.
