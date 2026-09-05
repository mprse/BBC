# ADR 0013: Initial Block Synchronization

- Status: Accepted
- Date: 2026-09-05

## Context

Block relay handles blocks created while a peer is connected, but a full node
that starts later remains at Genesis. Copying another node's `blocks.dat` or
SQLite cache would bypass peer framing, validation, and storage boundaries and
would not represent real network synchronization.

The first synchronization design needed a small mechanism for downloading one
active chain without transferring another node's storage files.

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

The original single-entry locator could not resolve forks. ADR 0015 replaces it
with multi-entry locators; ADR 0014 defines cumulative-work fork choice,
reorganization, and mempool reinsertion.
