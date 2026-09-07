# BBC Architecture Overview

## Purpose

BBC separates cryptographic data, consensus validation, local storage, peer
communication, and test orchestration. This prevents a command, database, miner,
or remote peer from bypassing the rules that every full node must enforce.

The project currently supports observable local experiments and persistent
nodes on one computer or one private IPv4 LAN. Application P2P may use loopback
or RFC 1918 addresses by default, and an explicit Internet scope permits static
numeric public peers. Application RPC, scenario P2P, and test control remain
restricted to IPv4 loopback.

## Main data flow

1. A wallet creates an Ed25519 key pair and stores the private key in an
   encrypted local file.
2. The wallet signs a canonical transaction containing recipient, amount, fee,
   and account nonce.
3. A full node validates the signature and current account rules before placing
   the transaction in its local mempool and relaying it.
4. A miner requests a candidate block from a full node. The node selects up to
   1,000 pending transactions and supplies the active-chain parent and target.
5. The miner changes the mining nonce until the block hash meets the target,
   then submits the complete block.
6. The full node independently validates the format, Proof of Work, parent,
   transactions, balances, nonces, fees, and reward before persisting the block.
7. Peers relay accepted blocks and download missing history. If another branch
   has strictly greater cumulative work, a full node reorganizes to it and
   reconsiders transactions detached from the old branch.

## Components and interfaces

| Component | Responsibility | Main C++ or external interface |
|---|---|---|
| `crypto` | Hashing, key generation, signatures, secure memory | `include/bbc/crypto/` |
| `wallet` | Addresses, encrypted key storage, signing | `Wallet`, `Address`, `bbc wallet ...` |
| `transaction` | Canonical signed transaction values and files | `SignedTransaction`, `.bbctx` |
| `chain` | Canonical blocks, fork tree, active state | `Block`, `Blockchain`, `.bbcblock` |
| `consensus` | Target checks and mining | `validate_proof_of_work()`, `mine_block()` |
| `mempool` | Pending transaction policy and selection | `Mempool` |
| `storage` | Authoritative blocks and rebuildable SQLite caches | `ChainStore`, `MempoolStore` |
| `network` | Framing, handshakes, relay, synchronization | P2P protocol version 1 |
| `node` | Shared long-running runtime for configured nodes and scenarios | `bbc node run ...` |
| `config` | Strict persistent application settings | `ApplicationConfig`, `bbc config ...` |
| `rpc` | Local authentication token and command client | `bbc rpc ...` |
| `app` | Human-facing command dispatch | `bbc <command>` |
| `tools/scenario.py` | Multi-process orchestration and assertions | Scenario JSON |

The public headers under `include/bbc/` define reusable C++ interfaces. Code
under `src/app/` translates CLI input into those interfaces; it must not contain
alternative consensus rules. The `node` layer composes the same chain, storage,
mempool, wallet, and network components for persistent processes and
long-running scenarios.

## Process roles

A wallet holds a key and creates signatures. It does not need to store the
blockchain and cannot declare a transaction accepted. In the current network
scenarios, a wallet submits only when combined with a role that already has a
full-node connection; a standalone wallet network client is not implemented.

A miner searches for Proof of Work on a template. It does not choose balances,
fees, or the active chain. A mining-only process relies on its connected full
node for current templates.

A full node stores block history and independently validates everything. Its
active chain determines confirmed balances and nonces. Full nodes may disagree
temporarily during propagation or a fork and converge after one valid branch
accumulates strictly more work.

One process may combine roles, but the responsibilities and validation
boundaries remain the same. A persistent node never decrypts the configured
wallet: the short-lived CLI signs locally and RPC carries only the public signed
transaction. A combined full node and miner creates templates from its own
validated chain and mempool.

## Persistent data

```text
<data-dir>/
|-- chain/
|   |-- blocks.dat
|   `-- chain.db
`-- mempool/
    `-- mempool.db
```

`blocks.dat` is authoritative append-only history. `chain.db` is a rebuildable
index and account-state cache. `mempool.db` is a replaceable cache of pending
transactions. An encrypted wallet file is independent of a node data directory.

Each running full node requires its own data directory. Database bytes are not
consensus: correct nodes may organize local indexes differently as long as they
validate the same canonical blocks and derive the same active state.

## Validation boundaries

- Decoders reject malformed sizes, encodings, unsupported versions, and invalid
  signatures before state mutation.
- The mempool applies local pending-transaction policy against confirmed state.
- `Blockchain::append()` applies consensus and fork-choice rules atomically.
- `ChainStore::append()` adds durable storage without weakening chain checks.
- P2P handlers treat all peers as untrusted and pass decoded objects through the
  same transaction, mempool, chain, and storage APIs.
- The scenario control endpoint can request normal operations but cannot install
  accepted state directly.
- The application RPC binds only to `127.0.0.1`, requires a separate random
  token, and submits signed transactions through the normal mempool path.

## Where to read next

Use `glossary.md` for unfamiliar terms. The data formats and consensus rules are
described in `wallet-file-format.md`, `transaction-format.md`,
`block-format.md`, `proof-of-work.md`, and `chain-state.md`. Persistent data is
covered by `chain-store.md` and `mempool.md`. Peer behavior is split across the
protocol documents listed in `README.md` in this directory.
