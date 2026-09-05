# ADR 0012: Network Transaction Confirmation Through a Second Mining Round

- Status: Accepted
- Date: 2026-09-05

## Context

Transaction propagation proves that a funded wallet can submit a signed
transaction and that
full nodes independently converge on its pending ID. A complete vertical slice
must also prove that miners receive the transaction in a later template, that a
winning block confirms it, and that every full node derives the same account
state while removing it from local mempool storage.

Mining-only actors do not store the blockchain. The original implementation
validated every template against the height and tip advertised once in the
initial `HELLO`, which becomes stale after the first accepted block and cannot
support a second mining round.

## Decision

- Let a mining-only actor retain the height and ID of the latest accepted block
  that extends its current candidate.
- Advance that view from a valid relayed block or from a matching successful
  result for the miner's own submitted block.
- Validate each later template against this retained tip, the configured
  network target, and the miner's requested reward address.
- Keep this view in memory only; it is not a substitute for full chain storage
  or initial synchronization.
- Run a second coordinated mining race after transaction propagation.
- Require the height-2 template to contain the pending transaction and require
  both full nodes to independently validate, persist, apply, and relay the
  winning block.
- Revalidate each persistent mempool after block acceptance so the confirmed
  transaction is removed.
- Record mining winners and submitted transaction metadata in runner memory so
  final assertions can calculate balances for both possible winner patterns.

## Consequences

The local network now demonstrates the complete reward-to-confirmation flow.
If one miner wins both rounds, its fee payment and fee reward cancel while it
receives both subsidies. If different miners win, the first winner pays amount
plus fee and the second receives subsidy plus fee. In both cases the recipient
receives the exact amount, the sender nonce becomes one, and total supply equals
two block subsidies.

This design supports miners that observed the chain from Genesis during the
current run. A late or restarted mining-only actor still cannot discover and
download missing blocks because mining-only actors do not store full history.
