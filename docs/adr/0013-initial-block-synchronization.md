# ADR 0013: Initial Block Synchronization

- Status: Accepted
- Date: 2026-09-05

## Context

Stage 7.3 can relay blocks created while a peer is connected, but a full node
that starts later remains at Genesis. Copying another node's `blocks.dat` or
SQLite cache would bypass peer framing, validation, and storage boundaries and
would not represent real network synchronization.

Stage 8 will add competing branches and reorganization. Stage 7.4 needs a small
mechanism that is correct for the currently implemented single active chain
without prematurely selecting a fork-choice design.

## Decision

- Use the current tip in `HELLO` to detect a remote full node that is ahead.
- Add bounded `GET_BLOCKS` and `BLOCKS` P2P messages with request correlation.
- Use an exact height-and-block-ID locator for the current linear chain.
- Return at most four canonical serialized blocks per response.
- Decode, validate, persist, and apply every received block through
  `ChainStore`; never transfer database files or derived SQLite state.
- Revalidate the local mempool and update the advertised tip after every block.
- Stop with an observable failure on locator mismatch, malformed data, invalid
  chain extension, storage failure, empty progress, or target-tip mismatch.
- Extend the scenario runner with staged actor startup and a synchronization
  completion wait. Verify a late join followed by restart from the same local
  storage.

## Consequences

Late full nodes can now converge deterministically and retain downloaded blocks
across restart. Bounded batches remain below the existing frame limit and expose
validation progress in the scenario console.

The single-entry locator deliberately cannot resolve forks. A peer at the same
height with another tip reports divergence and leaves local state unchanged.
Branch discovery, cumulative-work selection, rollback, reorganization, and
mempool reinsertion remain Stage 8 work.
