# ADR 0003: Block Version 1 and Genesis Block

## Status

Accepted on 2026-09-03.

## Context

BBC needs a canonical block representation shared by Proof of Work, chain state,
storage, and networking. A block must commit to its ordered transactions,
identify its parent, name the account that receives the block reward, and carry
the fields required by mining.

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
- The block does not encode a reward amount. A full node derives the subsidy
  from consensus rules and block height, then adds transaction fees.
- Genesis has height zero, zero previous hash, no reward recipient, timestamp
  zero, the maximum 256-bit target, mining nonce zero, and no transactions.
- Genesis issues no BBC. The first non-Genesis block can be reward-only, allowing
  issuance to start even when no account has funds.

The target and mining nonce are serialized in the canonical header so mining
does not change the block format. Proof-of-Work validation decides whether a
non-Genesis block satisfies its target.

## Consequences

- Every node has one byte-for-byte identical Genesis Block and chain origin.
- A block can be parsed with fixed limits before allocating transaction storage.
- Transaction order and any transaction modification change the transaction root
  and therefore the block ID.
- Empty transaction lists remain valid, which permits reward-only blocks and
  mining while the mempool is empty.
- Changing any field, Merkle rule, limit, or Genesis constant is a consensus
  change and requires a new protocol decision.
- Proof of Work and the chain-state layer separately define parent validation,
  timestamp rules, account balances, transaction nonces, fees, and rewards.
