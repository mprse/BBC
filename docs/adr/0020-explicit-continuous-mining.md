# ADR 0020: Explicit Continuous Mining

- Status: Accepted
- Date: 2026-09-07

## Context

One RPC-triggered mining attempt is useful for deterministic demonstrations,
but a persistent public miner must continue building on each accepted tip. An
implicit behavior change would make existing commands and tests race against
unexpected blocks.

## Decision

- Preserve `rpc start-mining` as one block attempt.
- Add `rpc start-continuous-mining` and `rpc stop-mining` for persistent nodes.
- Add optional `miner.auto_start`, defaulting to `false`.
- Start automatically only after P2P and local RPC listeners are ready.
- Cancel current work when another block changes the active tip, then create or
  request a template for the new tip.
- Permit reward-only blocks when the mempool is empty.
- Keep the scenario control API one-cycle and explicitly coordinated by its
  runner.

## Consequences

- EC2 can run an unattended full node and miner with `auto_start: true`.
- Existing application configurations and one-cycle tests retain their
  behavior.
- `stop-mining` waits for the current worker to observe cancellation and exit.
- Mining-only processes wait when their configured full-node source is not
  connected and resume after connectivity returns.
- The block format, Proof-of-Work target, fork choice, rewards, and P2P wire
  messages do not change.
