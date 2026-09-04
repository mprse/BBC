# BBC Block Version 1 Format

## Scope

This document defines the canonical binary encoding and format-level validation
of BBC Block v1. All offsets are zero-based. Scalar integer fields are unsigned
and little-endian. Hash and address fields are copied as raw 32-byte values. The
difficulty target is the sole exception: it is encoded as a 32-byte big-endian
unsigned integer for the numeric comparison introduced in Stage 4.

## Header

Every block begins with this exact 165-byte header:

| Offset | Size | Field | Required value or meaning |
|---:|---:|---|---|
| 0 | 4 | magic | ASCII `BBLK` |
| 4 | 1 | version | `1` |
| 5 | 4 | chain ID | `1` |
| 9 | 8 | height | Genesis is `0`; other blocks are greater than `0` |
| 17 | 32 | previous block hash | Block ID of the parent; all zero only for Genesis |
| 49 | 32 | transaction root | Merkle root defined below |
| 81 | 32 | reward recipient | Raw BBC address hash; all zero only for Genesis |
| 113 | 8 | timestamp | Unix time in seconds |
| 121 | 32 | difficulty target | Big-endian 256-bit target; fixed by Stage 4 consensus |
| 153 | 8 | mining nonce | Proof of Work search value, distinct from account nonce |
| 161 | 4 | transaction count | Number of following transactions, from `0` to `1000` |

The block ID is:

```text
SHA-256(header[0..164])
```

The `BBLK` magic and version already domain-separate a block header from other
BBC objects.

## Transaction body

The body immediately follows the header and consists of exactly
`transaction_count` canonical Transaction v1 encodings. Each transaction is 161
bytes. No offsets, padding, separators, or trailing bytes are permitted.

```text
encoded_block_size = 165 + transaction_count * 161
maximum_encoded_block_size = 161165 bytes
```

Transactions retain their existing signatures and transaction IDs. Every
transaction must use the same chain ID as the block, pass Transaction v1 format
and signature validation, and have a unique transaction ID within the block.

## Transaction Merkle root

The root commits to both transaction values and their order. SHA-256 is applied
once at every step.

For an empty transaction list:

```text
root = SHA-256(0x02)
```

For every transaction, create a leaf from its 32-byte transaction ID:

```text
leaf = SHA-256(0x00 || transaction_id)
```

For every pair of nodes:

```text
parent = SHA-256(0x01 || left || right)
```

When a level has an odd number of nodes, duplicate its final node before hashing
the pairs. Repeat until one node remains. The prefix bytes separate empty roots,
leaves, and internal nodes.

The empty transaction root is:

```text
DBC1B4C900FFE48D575B5DA5C638040125F65DB0FE3E24494B76EA986457D986
```

## Genesis Block

Genesis is one exact protocol constant:

| Field | Value |
|---|---|
| version | `1` |
| chain ID | `1` |
| height | `0` |
| previous block hash | 32 zero bytes |
| transaction root | empty transaction root |
| reward recipient | 32 zero bytes (`none`) |
| timestamp | `0` |
| difficulty target | 32 `FF` bytes |
| mining nonce | `0` |
| transaction count | `0` |

Its canonical block ID is:

```text
10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2
```

Genesis is not mined, grants no reward, and contains no transactions. Any
height-zero encoding that differs from this constant is invalid.

## Format validation order

An untrusted encoded block is validated in this order:

1. Require a size from 165 through 161165 bytes.
2. Require the `BBLK` magic, version `1`, and chain ID `1`.
3. Require at most 1000 transactions and an exact size derived from the count.
4. Decode every transaction, verify its signature and chain ID, and reject
   duplicate transaction IDs.
5. Recompute and compare the transaction root.
6. For height zero, require the exact canonical Genesis encoding.
7. For other heights, require a nonzero previous hash and reward-recipient hash.

Passing these checks means only that the block has a valid canonical format.
Stage 4 additionally requires the expected target and valid Proof of Work, as
specified in `docs/proof-of-work.md`. Later stages will validate the parent
relationship, height and timestamp relative to the parent, account state,
transaction nonces and balances, fees, and reward state transitions.
