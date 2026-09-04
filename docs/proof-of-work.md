# BBC Proof of Work Version 1

## Consensus target

Every non-Genesis Block v1 must encode this exact 32-byte target:

```text
000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
```

The target and the SHA-256 block hash are interpreted as unsigned 256-bit
big-endian integers. The validity condition is strict:

```text
block_hash < difficulty_target
```

A hash equal to the target is invalid. Checking only a visual zero prefix is not
the consensus rule; the complete 32-byte values are compared.

Genesis is never mined. Its all-`FF` target remains part of its fixed canonical
encoding and is accepted only because the complete Genesis Block matches the
protocol constant.

## Mining procedure

For one candidate block:

1. Validate the block format and require the fixed network target.
2. Serialize its 165-byte header.
3. Encode the current mining nonce as a little-endian `uint64` at offset 153.
4. Calculate `SHA-256(header)`.
5. Stop successfully when the hash is numerically less than the target.
6. Otherwise increment the nonce and repeat.

The transaction list is not hashed separately for each attempt. Its Merkle root
is already present in the header and remains fixed during the mining run. Any
change to a transaction, its order, the timestamp, reward recipient, parent, or
another header field creates a different candidate and invalidates earlier work.

The miner may stop after an explicit attempt limit. A stopped run returns the
next untested nonce so work can resume without repeating the same range. No
output block is produced until a valid nonce is found.

## Deterministic vector

The Stage 4 vector is a reward-only height-one block with:

```text
previous block hash:
10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2

reward recipient:
BBC_4400000000000000000000000000000000000000000000000000000000000000

timestamp: 1788393600
mining nonce: 7707263
transaction count: 0
```

Its block ID is:

```text
00000091566FACB46701A1D0633E0C89506B11C1FF4F150611BEBD9E6EC2FF58
```

Nonce `7707262` does not meet the target. Starting at nonce zero, the known
solution requires 7,707,264 attempts when every nonce is tested sequentially.

## Proof of Work verification boundary

Stage 4 block verification checks:

- canonical block and transaction encoding,
- transaction signatures and Merkle root,
- the exact fixed target,
- and the Proof of Work hash comparison.

Proof of Work verification alone does not prove that the parent is known or
canonical, the height follows the parent, the timestamp is acceptable,
transactions have sufficient balances and correct state nonces, or rewards are
applied correctly. Those checks belong to the Stage 5 chain and state layer.
