# BBC Chain Store Version 1

## Scope

Stage 5.1 defines local durable storage for one linear BBC chain. The storage
format is a node implementation detail, not a consensus format: nodes may use a
different database layout as long as they validate the same canonical blocks
and derive the same state.

A data directory contains:

```text
<data-dir>/
`-- chain/
    |-- blocks.dat
    `-- chain.db
```

`blocks.dat` is the authoritative immutable history. `chain.db` is a derived
SQLite index and account-state cache that can be recreated from `blocks.dat`.

## `blocks.dat`

The file contains append-only Version 1 records. Genesis is always the first
record, followed by non-Genesis blocks in ascending height order.

Each record has this layout:

| Offset | Size | Field | Value |
|---:|---:|---|---|
| 0 | 4 | Record magic | ASCII `BBRC` |
| 4 | 1 | Record version | `1` |
| 5 | 3 | Reserved | Three zero bytes |
| 8 | 4 | Payload size | Little-endian `uint32` |
| 12 | variable | Payload | Canonical BBC Block v1 bytes |
| 12 + payload size | 32 | Checksum | SHA-256 of the payload |

The payload size must be between the canonical block header size and the
maximum canonical block size. A record therefore occupies `44 + payload size`
bytes. No padding is added between records.

Opening a store scans the complete file and rejects an invalid header, reserved
byte, size, checksum, block encoding, Genesis record, Proof of Work, parent,
height, timestamp, transaction, reward, or state transition. Trailing partial
records are invalid and never silently ignored.

## `chain.db`

The SQLite database uses schema version 1 and contains three tables:

- `blocks`: height, block hash, parent hash, byte offset, and record size;
- `accounts`: address hash, balance, and next account nonce;
- `metadata`: current tip height and hash.

Unsigned 64-bit heights, balances, and nonces are stored as eight-byte
big-endian blobs. This preserves their complete unsigned range and makes height
keys sort in numeric order. Hashes and addresses are stored as raw 32-byte
blobs.

The current implementation deliberately validates all records and rebuilds the
SQLite tables whenever the store is opened. This is slower than trusting the
cache but makes startup recovery and correctness explicit while the project is
small. A missing `chain.db` is recreated automatically.

## Append and recovery behavior

Before writing, a candidate block is applied to a copy of the current
blockchain and state. Invalid blocks do not change either file.

For a valid block:

1. Encode the complete record in memory.
2. Append it to `blocks.dat` and flush the stream.
3. Rebuild the SQLite cache inside one `BEGIN IMMEDIATE` transaction.
4. Publish the new in-memory tip only after the database commit succeeds.

If the database update fails during a running process, `blocks.dat` is resized
back to its previous validated boundary. If a process stops after a complete
record reaches `blocks.dat` but before the cache commit, the next open accepts
the record and rebuilds `chain.db`. A partial tail is detected and reported;
automatic destructive repair is intentionally not performed.

Stage 5.1 supports one writer per data directory. Concurrent writer locking,
segmented block files, pruning, branch storage, and reorganization journals are
deferred.

## CLI

Initialize a store containing Genesis:

```text
bbc chain init --data-dir <path>
```

Validate and append one mined block:

```text
bbc chain add --data-dir <path> --block <path>
```

Read the persisted chain without listing individual block files:

```text
bbc chain verify --data-dir <path>
bbc chain tip --data-dir <path>
bbc chain balance --address <BBC-address> --data-dir <path>
bbc wallet balance [--file <wallet-path>] --data-dir <path>
```

The older repeated `--block` form remains available as an offline replay and
diagnostic interface. A command must use either `--data-dir` or `--block`, never
both.
