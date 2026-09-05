# ADR 0007: Local Persistent Mempool

- Status: Accepted
- Date: 2026-09-04

## Context

The chain store validates and persists blocks, but users would otherwise have
to name transaction files manually while constructing each block. A full node
also needs behavior for accepting pending transactions, preventing local
overspending and nonce conflicts, selecting transactions for mining, and
removing transactions after the chain advances.

## Decision

- Keep mempool policy outside consensus and derive it from the current confirmed
  `ChainState`.
- Limit a local mempool to 10,000 transactions and a block candidate to the
  existing consensus limit of 1,000 transactions.
- Require consecutive pending nonces per sender and reserve every pending
  outgoing amount plus fee against confirmed balance.
- Do not treat unconfirmed incoming transfers as spendable funds.
- Select the highest-fee nonce-eligible transaction, breaking equal-fee ties by
  transaction ID.
- Persist arrival-ordered canonical transactions in
  `<data-dir>/mempool/mempool.db` and revalidate them whenever the chain state is
  loaded or advanced.
- Keep selected transactions in the pool until a block is accepted. Candidate
  construction alone never removes them.

## Consequences

- Different nodes may safely have different mempools and produce different valid
  candidates.
- A high-fee transaction cannot bypass an earlier nonce from the same sender.
- Restarting a node preserves its valid local pending queue, while deleting the
  mempool database only loses replaceable local data.
- Peer relay and reorganization reinsertion build on this policy. Replacement
  by fee, eviction under load, expiration, and multi-process locking remain
  unsupported.
