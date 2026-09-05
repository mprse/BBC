# BBC Initial Synchronization Protocol Version 1

- Status: Accepted
- Scope: Stage 7.4 linear-chain synchronization

## 1. Purpose

This document specifies how a BBC full node that joins late downloads blocks
missing from its local active chain. Synchronization transfers canonical blocks,
not SQLite state or database files. The receiver decodes, validates, applies,
and persists every block through the normal `ChainStore` path before advancing
its advertised tip.

Version 1 synchronizes only a shared linear history. Competing branches,
cumulative-work fork choice, reorganization, and transaction reinsertion are
Stage 8 behavior.

## 2. Starting synchronization

Every `HELLO` advertises the sender's current active-chain height and block ID.
After a handshake between two full nodes:

- equal heights and equal block IDs mean the receiver is up to date;
- a greater remote height starts synchronization from the receiver's local tip;
- equal heights with different block IDs report divergence and do not modify
  either chain;
- a lower remote height requires no request because the remote peer can initiate
  its own synchronization.

Only one synchronization request is outstanding per node. The selected peer and
request identifier must match each response. Unsolicited or stale responses are
ignored.

## 3. `GET_BLOCKS` payload

Message type `30` has an exact 50-byte payload:

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 8 | request ID | Receiver-local correlation value |
| 8 | 8 | locator height | Height of the receiver's current tip |
| 16 | 32 | locator block ID | Block ID at the locator height |
| 48 | 2 | maximum block count | `1` through `4` |

Integers use the P2P protocol's unsigned little-endian encoding. The locator is
one exact active-chain entry because Stage 7.4 has no competing branches. A
server rejects a zero count, a count above four, or a locator that is not its
block at the supplied height.

## 4. `BLOCKS` payload

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

A successful response contains at most four consecutive blocks immediately
after the locator. A non-success response contains zero blocks. Four maximum-size
blocks produce a 644,687-byte payload, below the 1,048,576-byte P2P frame limit.

## 5. Receiver validation and persistence

The receiver validates the complete response structure before applying its
blocks: matching request and peer, known status, bounded count, every record
length, canonical block decoding, and absence of trailing bytes. It then appends
blocks in response order. Each append independently enforces chain ID, height,
parent ID, increasing timestamp, Proof of Work, transactions, rewards, account
state, and durable block-record storage.

After each successful append the node revalidates its persistent mempool and
updates the tip used in later `HELLO` messages. It requests another batch from
its new tip until the height and ID equal the snapshot advertised by the source.
An empty successful batch before that target, an invalid block, a storage error,
or a mismatching target tip fails synchronization without trusting later data.

Already appended valid blocks remain durable if a later batch fails. Restarting
the node reconstructs state from its existing block store and therefore does not
download those blocks again.

## 6. Observable state

Full-node control status exposes a `sync` object with `state`, `received_blocks`,
`target_height`, and `target_tip`. Implemented states are `idle`, `syncing`,
`complete`, `up_to_date`, `diverged`, and `failed`. Structured events report
start, request, served batch, applied block, completion, divergence, and failure.

## 7. Required verification

Unit tests enforce message identifiers and payload bounds. The Stage 7.4
multi-process scenario must:

1. start two full nodes and two miners from Genesis;
2. mine the first reward block;
3. submit and confirm one signed payment in block 2;
4. advance the chain to height 5 and start a third full node from Genesis;
5. synchronize it through separate four-block and one-block responses;
6. compare all three tips and account states;
7. restart the third node with the same data directory; and
8. prove it reports `up_to_date` without downloading blocks again.
