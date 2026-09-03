# ADR 0003: Block Version 1 and Genesis Block

## Status

Accepted on 2026-09-03.

## Context

Stage 3 needs a canonical block representation before Proof of Work, chain state,
storage, or networking can be implemented. A block must commit to its ordered
transactions, identify its parent, name the account that will receive the future
block reward, and reserve the fields required by Stage 4 mining.

The concept document contained two possible bootstrapping models: a genesis
allocation and an empty genesis followed by mining rewards. The project selected
the second model. No wallet or private key is privileged by the protocol.

## Decision

BBC Block v1 uses the canonical binary format specified in
`docs/block-format.md`.

- The 165-byte header contains the `BBLK` magic, version, chain ID, height,
  previous block hash, transaction root, reward recipient, timestamp, 256-bit
  difficulty target, mining nonce, and transaction count.
- The block ID is the SHA-256 digest of the complete canonical header.
- The ordered transaction list contains zero to 1000 canonical 161-byte
  Transaction v1 values.
- A domain-separated binary Merkle tree commits the header to the ordered
  transaction list.
- The all-zero reward-recipient value is reserved for Genesis. Every other block
  must name a nonzero BBC address hash.
- The block does not encode a reward amount. A full node will derive the subsidy
  from consensus rules and block height in Stage 5, then add transaction fees.
- Genesis has height zero, zero previous hash, no reward recipient, timestamp
  zero, the maximum 256-bit target, mining nonce zero, and no transactions.
- Genesis issues no BBC. The first non-Genesis block can be reward-only, allowing
  issuance to start even when no account has funds.

The target and mining nonce are serialized in Stage 3 so Stage 4 does not change
block IDs. Stage 3 does not yet decide whether a non-Genesis block satisfies its
target.

## Consequences

- Every node has one byte-for-byte identical Genesis Block and chain origin.
- A block can be parsed with fixed limits before allocating transaction storage.
- Transaction order and any transaction modification change the transaction root
  and therefore the block ID.
- Empty transaction lists remain valid, which permits reward-only blocks and
  mining while the mempool is empty.
- Changing any field, Merkle rule, limit, or Genesis constant is a consensus
  change and requires a new protocol decision.
- Proof of Work, parent-chain validation, timestamp rules, account balances,
  transaction nonces, fees, and reward crediting remain outside Stage 3.
