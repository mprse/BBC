# ADR 0005: Linear Chain and Account State

## Status

Accepted on 2026-09-04.

## Context

BBC must turn individually valid blocks into an ordered blockchain and
derive balances from its history. The protocol also needs exact rules for
monetary precision, account nonces, fees, block rewards, timestamps, and
integer overflow before state transitions can be deterministic across nodes.

Fork choice and durable node storage are separate concerns. Introducing them at
the same time as the first state machine would make transaction and reward
rules harder to test in isolation.

## Decision

- BBC uses 100,000,000 base units per BBC.
- Every accepted non-Genesis Version 1 block creates a constant subsidy of 50
  BBC. Genesis creates no funds; Version 1 has no halving or supply cap.
- Account state stores a base-unit balance and the next required transaction
  nonce. A new account starts with balance zero and next nonce zero.
- A transaction must use the sender's exact next nonce and have enough funds for
  `amount + fee`. Values and resulting balances must fit in `uint64_t`.
- The minimum fee is zero. Fees are deducted from senders and transferred to
  the block reward recipient; they are not newly issued currency.
- Transactions execute atomically in their encoded block order. The block
  subsidy and total fees are credited only after all transactions succeed.
- A block must have height `parent.height + 1`, name the current tip as its
  parent, have a timestamp strictly greater than its parent, and pass the fixed
  fixed Proof-of-Work rule.
- Consensus performs no local wall-clock check.
- The initial state machine holds one linear chain in memory and can rebuild it
  by replaying ordered block files. ADR 0006 adds persistence, and ADR 0014
  extends the same state rules to forks and reorganizations.

The complete validation and transition rules are specified in
`docs/chain-state.md`.

## Consequences

- The first mined reward-only block bootstraps spendable supply without a
  privileged Genesis allocation.
- A sender can create its first transaction with nonce zero, matching the
  existing Transaction v1 CLI and examples.
- A miner cannot spend a newly created reward in the same block.
- Any invalid transaction rejects the entire block without partial balance or
  nonce changes.
- Replaying the same linear block history produces the same account state on
  every node.
- Fork handling and crash-safe persistence are separate decisions recorded in
  ADRs 0014 and 0006.
