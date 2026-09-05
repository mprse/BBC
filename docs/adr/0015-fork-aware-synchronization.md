# ADR 0015: Fork-Aware Synchronization and Partition Testing

- Status: Accepted
- Date: 2026-09-05

## Context

ADR 0014 lets one full node store side branches and reorganize locally, but the
original synchronization request named only one locator block. After a network
partition, that locator is normally absent from the other node's active chain,
so the nodes cannot find their common ancestor or exchange the stronger suffix.
The local scenario runner also needs deterministic partition controls and
assertions that prove the entire recovery path.

## Decision

- Replace the single synchronization locator with up to 32 `(height, block ID)`
  entries ordered newest first. Walk recent parents individually, then use
  exponentially increasing gaps, with Genesis as the final fallback.
- A serving node matches locators only against its active chain and returns
  consecutive active blocks after the newest match.
- Advance the receiver's synchronization cursor to the last received block,
  including a valid side block, so bounded batches can build a remote branch
  until it becomes stronger.
- Treat an already stored block in a synchronization batch as known rather than
  as a duplicate-block failure.
- Add authenticated loopback control for removing and restoring a selected
  outbound peer target. Removing a target closes its current session and
  disables automatic reconnect until an explicit connect command.
- Relay transactions restored from detached blocks after a reorganization so
  connected full-node mempools can converge.
- Add a `fork-reorg` multi-process scenario that creates a common funded block,
  partitions two full nodes, confirms a payment only on the weaker branch,
  extends the competing branch, heals the link, verifies reorganization and
  transaction restoration, restarts the reorganized node, and compares final
  consensus state.

## Consequences

Full nodes can now heal a tested partition when one advertised active branch is
strictly longer than the other. Side blocks remain durable and do not pollute
the active account state. Transactions removed solely by the abandoned branch
can return to the mempool and be propagated again.

Equal-height divergent peers intentionally remain divergent because the stable
first-seen tie rule provides no winner. A later block on either branch supplies
the strictly greater work required to initiate synchronization and convergence.
Orphan buffering, simultaneous multi-peer synchronization, dynamic difficulty,
and chain-tip changes during an active synchronization remain future work.

The `GET_BLOCKS` layout changes inside the pre-release Version 1 protocol. BBC
has no deployed compatibility guarantee yet; a future released wire protocol
must increment its version for incompatible changes.
