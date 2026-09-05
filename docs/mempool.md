# BBC Mempool

## Purpose and integration

The mempool is a local pool of valid, signed transactions that have not yet
been included in the node's active chain. The mempool is node policy, not
consensus state, so two correct nodes may temporarily contain different pending
transactions. Full nodes accept submissions, relay newly accepted transactions,
and use this pool when constructing mining templates.

## Admission rules

A transaction has already passed Transaction v1 decoding, chain ID, public-key,
sender-address, signature, amount, fee, and integer-overflow validation before
the mempool evaluates it. The mempool then applies these rules in order:

1. The pool contains fewer than 10,000 transactions.
2. The transaction ID is not already present.
3. The sender has no pending transaction with the same account nonce.
4. The nonce equals the confirmed account nonce plus the number of already
   accepted pending transactions from that sender. Nonce gaps are rejected.
5. The sender's confirmed balance covers the amount and fee of every accepted
   pending outgoing transaction, including the new transaction.

Pending incoming transfers do not increase spendable balance. This prevents a
chain of unconfirmed incoming payments from being treated as confirmed funds.
The minimum fee remains zero.

## Candidate selection

A block candidate contains at most 1,000 transactions. Selection repeatedly
compares the next nonce-eligible transaction of each sender and chooses the
highest fee. Equal fees are ordered by the lexicographically smaller 32-byte
transaction ID. After choosing a sender's transaction, that sender's next nonce
becomes eligible.

This policy prioritizes fees while preserving the consensus-required nonce
order for every sender. It compares absolute fees because all Transaction v1
records have the same encoded size.

Creating a candidate does not remove transactions. They remain pending while
miners search for Proof of Work and disappear only after the active chain
confirms them or makes them invalid.

## Persistence and revalidation

The local queue is stored in `<data-dir>/mempool/mempool.db` as canonical signed
transaction payloads in arrival order. Unlike `chain/blocks.dat`, this database
is not authoritative blockchain history. It may be deleted; the local node then
starts with an empty mempool and can learn pending transactions again later.

The database is revalidated against the current chain whenever it is opened.
Malformed rows, confirmed transactions, stale nonces, conflicting transactions,
and transactions whose reserved debit is no longer affordable are removed. A
successful `chain add` opens and revalidates the mempool after persisting the
block. If mempool maintenance fails, the authoritative block remains accepted
and the CLI emits a warning.

One process may write a data directory at a time. Full nodes relay newly
accepted transactions over P2P. After a chain reorganization, eligible
transactions from detached blocks are restored before the existing pending
queue and may be relayed again.

## C++ API

`bbc::mempool::Mempool` in `include/bbc/mempool/mempool.hpp` implements
in-memory admission, revalidation, and candidate selection. The methods are
`add()`, `revalidate()`, and `select()`.

`bbc::storage::MempoolStore` in
`include/bbc/storage/mempool_store.hpp` adds SQLite persistence. Its `open()`,
`add()`, and `revalidate()` methods cover normal operation;
`reconcile_reorganization()` rebuilds the queue after a branch switch and
returns transactions that should be relayed.

## CLI workflow

```console
bbc mempool add --data-dir node-a --transaction payment.bbctx
bbc mempool list --data-dir node-a
bbc mempool status --data-dir node-a
bbc block candidate --data-dir node-a --reward-to <BBC-address> --timestamp <unix-seconds> --out candidate.bbcblock
bbc block mine --file candidate.bbcblock --out mined.bbcblock
bbc chain add --data-dir node-a --block mined.bbcblock
```

The data directory must already contain a chain initialized with `chain init`.
The candidate command derives the next height, parent block ID, fixed difficulty
target, and initial mining nonce from the stored chain and local protocol rules.
