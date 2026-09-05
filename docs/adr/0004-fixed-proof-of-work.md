# ADR 0004: Fixed Proof of Work for the Initial Network

## Status

Accepted on 2026-09-03.

## Context

BBC needs a deterministic Proof of Work rule that is visible on one laptop
but remains inexpensive to verify. Block v1 already stores a 256-bit target and
a 64-bit mining nonce in its canonical header.

Allowing a miner to choose its own easier target would make Proof of Work
meaningless. A node must derive the expected target from consensus rules and
compare that value with the target committed to the block header.

## Decision

The initial BBC network uses one fixed target for every non-Genesis block:

```text
000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
```

- The block hash is the existing Block ID: one SHA-256 digest of the complete
  165-byte canonical header.
- Hash and target are unsigned 256-bit big-endian integers.
- A non-Genesis block is valid only when its encoded target equals the fixed
  network target and `block_hash < target`. Equality is invalid.
- Genesis is the exact constant from ADR 0003 and is exempt from Proof of Work.
- Mining changes only the little-endian 64-bit mining nonce at header offset
  153. The candidate's transactions, Merkle root, parent, reward recipient,
  timestamp, target, and transaction count remain fixed during one mining run.
- Mining begins at the nonce already stored in the candidate and increments it
  by one after every failed attempt.
- The reusable mining operation accepts an attempt limit. Reaching that limit
  reports the next nonce without producing a mined block. Exhausting nonce
  `UINT64_MAX` is a distinct failure.
- Difficulty adjustment is not part of the initial network. Introducing it will
  require a later consensus decision that derives the expected target from chain
  history.

The fixed target gives each independent hash an approximate success probability
of one in `2^24`, or one expected success per 16,777,216 attempts. The CLI reports
the actual attempts, elapsed time, and hash rate for each successful run.

## Consequences

- Verification requires one SHA-256 calculation and one 256-bit comparison.
- Miners cannot make their own blocks easier by changing the target field.
- Mining time varies randomly even at a constant hash rate; `2^24` is an
  expectation, not a schedule.
- Multiple local miner processes combine their work probabilistically and can
  find blocks faster than one process.
- This low fixed difficulty is suitable only for an educational local network,
  not for protecting real value or an unrestricted public network.
- Chain linkage, balances, account nonces, rewards, timestamps relative to a
  parent, cumulative work, and fork choice are validated by the chain layer.
