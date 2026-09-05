# ADR 0014: Cumulative-Work Fork Choice and Reorganization

- Status: Accepted
- Date: 2026-09-05

## Context

BBC can propagate and synchronize an active chain, but simultaneous miners
can legitimately produce different valid blocks with the same parent. Rejecting
the second block loses information needed to follow a branch that later becomes
the strongest chain. Account state and the local mempool must also follow the
selected branch rather than whichever block arrived most recently.

BBC currently requires one fixed Proof-of-Work target per network profile. All
valid non-Genesis blocks on a profile therefore represent equal work.

## Decision

- Store every fully validated block whose parent is already stored, including a
  valid block on a side branch.
- Reject duplicate blocks and blocks whose parent is unknown. Orphan buffering
  is not implemented.
- Associate each stored block with its parent, the state after that block, and
  cumulative work. Genesis has zero work; each valid Version 1 block adds one
  work unit because the target is fixed.
- Select the branch with strictly greater cumulative work. Equal work never
  replaces the current active branch, making the first selected branch stable
  until another branch proves more work.
- On a change of branch, find the common ancestor, detach the old suffix, attach
  the new suffix, and publish the state snapshot of the new tip atomically.
- Rebuild the mempool against the new state by reconsidering transactions from
  detached blocks in original chain order before the existing pending queue.
  This preserves eligible nonce descendants; normal mempool policy still
  decides which transactions are retained.
- Keep `blocks.dat` as the authoritative append-only record stream. Records are
  stored in validated arrival order and every non-Genesis parent must precede
  its child. Replaying the records reproduces the same tie choice and active tip.
- Upgrade the derived SQLite cache to schema version 2. Index every stored block
  by hash and record its height, parent, cumulative work, active-chain flag, and
  file location.

## Consequences

A full node can retain two blocks at the same height and later reorganize to the
branch that gains another valid block. Its balances, nonces, advertised tip, and
mining templates follow only the active branch. Side blocks remain available
after restart.

With fixed difficulty, comparing cumulative work is currently equivalent to
comparing branch height. A future variable-difficulty protocol must define an
exact wider work calculation before targets can vary; it must not silently keep
the one-unit calculation.

The original single-locator synchronization protocol could not discover a
common ancestor across a healed partition. ADR 0015 adds multi-entry locators,
active-branch serving from the common ancestor, partition controls, and an
end-to-end reorganization scenario without changing this fork-choice rule.
