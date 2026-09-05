# BBC Block Synchronization Protocol Version 1

- Status: Accepted

## 1. Purpose

This protocol lets a BBC full node download blocks missing from its local block
tree. Synchronization transfers canonical blocks, never SQLite state or database
files. The receiver decodes, validates, applies, and persists every block through
the normal `ChainStore` path before it advertises a new active tip.

A multi-entry block locator lets two nodes discover the newest block shared by
their active chains after a network partition. The serving node then sends its
active-chain suffix. Valid tied blocks are retained as a side branch, and a
strictly stronger received branch triggers the normal cumulative-work
reorganization rules.

## 2. Starting synchronization

Every `HELLO` advertises the sender's current active-chain height and block ID.
After a handshake between two full nodes:

- equal heights and equal block IDs mean the receiver is up to date;
- a greater remote height starts synchronization;
- equal heights with different block IDs report divergence and preserve both
  active tips until one branch becomes stronger;
- a lower remote height requires no request because the remote peer can initiate
  its own synchronization.

Only one synchronization request is outstanding per node. The selected peer and
request identifier must match each response. Unsolicited or stale responses are
ignored. The target height and ID are a snapshot of the peer's handshake tip.

## 3. Block locator

A receiver builds the locator from the branch ending at its synchronization
cursor. The first entries walk parents one block at a time. After ten entries,
the distance doubles to bound the locator size for long chains. Genesis is
included as the final fallback whenever the entry limit permits it.

Entries are ordered newest first and contain both height and block ID. A server
accepts the first entry that exactly matches its active chain at that height;
side-branch matches are not serving points. The maximum locator length is 32.

The initial cursor is the receiver's active tip. After a non-empty batch, the
cursor becomes the last received block even if that block is currently on a
side branch. This permits a later batch to extend that branch until it becomes
strictly stronger and active.

## 4. `GET_BLOCKS` payload

Message type `30` has a variable payload from 52 through 1,292 bytes:

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 8 | request ID | Receiver-local correlation value |
| 8 | 2 | locator count | `1` through `32` |
| 10 | `count * 40` | locator entries | Height plus block ID, newest first |
| following | 2 | maximum block count | `1` through `4` |

Each locator entry contains an 8-byte height followed by its 32-byte block ID.
Integers use unsigned little-endian encoding. The payload length must exactly
match the locator count. Invalid structure or counts return invalid-request
status; no active-chain match returns locator-not-found status.

## 5. `BLOCKS` payload

Message type `31` contains an 11-byte prefix followed by zero or more canonical
blocks:

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 8 | request ID | Exact ID from `GET_BLOCKS` |
| 8 | 1 | status | `0` success, `1` locator not found, `2` invalid request |
| 9 | 2 | block count | Number of following block records |

Each block record is:

| Size | Field |
| ---: | --- |
| 4 | Canonical block byte length |
| variable | Canonical serialized block |

A successful response contains at most four consecutive active-chain blocks
immediately after the matched locator. A non-success response contains zero
blocks. Four maximum-size blocks produce a 644,687-byte payload, below the
1,048,576-byte P2P frame limit.

## 6. Receiver validation and persistence

The receiver validates the complete response structure before applying blocks:
matching request and peer, known status, bounded count, every record length,
canonical block decoding, and absence of trailing bytes. Each new block then
passes normal chain ID, height, parent, timestamp, Proof-of-Work, transaction,
reward, account-state, and durable-storage checks. A block already stored by ID
is accepted as known rather than appended twice.

Every new valid block is stored. A tied branch does not replace the active tip.
When a branch becomes strictly stronger, the node activates it, reconciles its
persistent mempool, returns eligible detached transactions, and relays restored
transactions to peers. Synchronization completes only when the receiver's
active height and ID equal the target snapshot.

An empty successful batch before the target, an invalid block, a storage error,
or a mismatching target tip fails synchronization. Already stored blocks remain
durable after failure. Restart reconstructs the block tree, active state, and
mempool from local storage.

## 7. Observable state

Full-node control status exposes a `sync` object with `state`, `received_blocks`,
`target_height`, and `target_tip`. States are `idle`, `syncing`, `complete`,
`up_to_date`, `diverged`, and `failed`. Structured events identify the locator,
matched height, stored side blocks, applied active blocks, reorganizations,
completion, and failure.

## 8. Required verification

Unit tests enforce payload bounds and locator ancestry. Multi-process scenarios
must cover both:

1. a late node synchronizing missing linear history in bounded batches and
   restarting without downloading it again; and
2. two full nodes sharing block 1, partitioning, creating competing height-2
   branches, extending one branch to height 3, healing, discovering the common
   ancestor, reorganizing, restoring a detached transaction to both mempools,
   and retaining the complete block tree after restart.
