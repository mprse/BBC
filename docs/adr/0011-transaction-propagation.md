# ADR 0011: Signed Transaction Submission and Relay

- Status: Accepted
- Date: 2026-09-05

## Context

BBC already has canonical signed transactions, account-state validation, and a
persistent local mempool. Network tests need the first complete path from a
funded wallet through independent full-node validation and mempool convergence.
The test-control channel must orchestrate this behavior without becoming part
of the P2P data plane or a source of blockchain truth.

## Decision

- Add fixed P2P messages for wallet submission, full-node relay, and an explicit
  submission result.
- Reuse the exact 161-byte canonical transaction encoding on disk, in blocks,
  and on the wire.
- Make every full node independently verify the signature, chain ID, account
  state, nonce, balance, deduplication policy, and durable mempool write.
- Relay only newly accepted transactions, exclude the source peer, and stop
  cycles through transaction-ID deduplication.
- Let the local scenario control method request signing by the actor's in-memory
  wallet, while the signed transaction itself travels over P2P.
- Expose transaction state at the submitting actor and ordered pending IDs in
  normalized full-node dumps.
- Select the existing mempool policy's highest-priority transactions when a
  full node builds a new mining template.

## Consequences

The regtest scenario now proves that the first mining reward can fund a real
signed payment and that two full nodes converge on its transaction ID. Mempool
contents remain local and transient; consensus state changes only if a later
valid block includes the payment. Duplicate relays consume validation work but
do not circulate indefinitely.

The protocol tracks one in-flight transaction per wallet and requires the
scenario to supply its nonce. ADR 0012 adds confirmation in a height-2 block.
Rejection-specific scenarios, automatic nonce lookup, wallet-only network
clients, and inventory-based relay remain unsupported.
