# BBC v2 Design Draft

This directory proposes the next user experience and network rules. **It is not
the current BBC implementation or an executable command reference.** The
[current documentation](../README.md) and tested code remain authoritative for
the running `development` network. In particular, current BBC accepts
reward-only blocks and pays a fixed 50 BBC subsidy for every non-Genesis block.

Here, *v2* names a design proposal, not an already assigned wire-protocol
version. The proposal may require a new network identity, block-validation
rules, storage migration boundaries, and updated tests before it can run.

## Reading order

1. [User and operator CLI](cli.md) separates everyday wallet commands from
   node administration and local diagnostics.
2. [Starting a network](network-bootstrap.md) describes its immutable identity,
   initial allocation, deployment, and a safe experimental restart.
3. [Creating participants](participant-setup.md) describes wallet-only,
   full-node, miner, and combined installations on Windows or Linux.
4. [Transaction journey](transaction-lifecycle.md) shows submission, mining,
   independent validation, competing blocks, and balance updates.

The [glossary](../glossary.md) explains specialized terms. Every `bbc` command
in this directory is **proposed pseudocode**, not a command to run against the
current executable. Consult the current [README](../../README.md) and
[application configuration](../application-config.md) for working commands.

## Direction agreed so far

- The first mined block contains no ordinary payments. It credits a founder
  address fixed in the network rules, regardless of who found its Proof of
  Work. The previously discussed 1,000 BBC is an **illustrative amount**, not
  a frozen consensus parameter. Canonical Genesis remains the common starting
  point.
- A later block must contain at least one valid ordinary transaction. An idle
  miner waits rather than building reward-only blocks. This is a consensus
  rule, not merely a default miner preference.
- Each later accepted block may create a miner subsidy that decreases by a
  predetermined schedule. Its included transaction fees go to the miner.
- Full nodes independently validate all received transactions and blocks. A
  mempool acceptance is provisional, not a final payment confirmation.
- Local automated tests use an isolated, loopback-only test network. A public
  node never exposes the test-control API or its local administration endpoint.

## Decisions required before implementation

The following values are intentionally **not** invented by this draft:

| Decision | Why it matters |
| --- | --- |
| Exact founder allocation and address | Fixes the exceptional first-block payout. |
| First ordinary miner subsidy and reduction schedule | Determines issuance and miner incentives. |
| Target block interval and difficulty-adjustment algorithm | Determines block cadence as mining power changes. |
| Minimum fee, fee unit, and transaction-relay limits | Affects spam cost, user experience, and resource use. |
| Network ID, address/network separation, and first-block encoding | Defines exact compatibility and prevents cross-network replay. |
| Wallet-only balance verification protocol | A remote node's report is not independently verified by a wallet. |
| Activation and reset procedure for the existing EC2/laptop experiment | Old blocks obey different rules and cannot silently become v2 history. |

Creating more wallets is not itself a source of BBC: a wallet needs an existing
balance to send. Conversely, a funded miner can send a valid payment to an
address it controls. An open network cannot infer from a signed transfer
whether two addresses belong to different people. Requiring a transaction
therefore prevents *empty* blocks, not self-transfer-triggered mining.

## Compatibility boundary

The existing `development` and `regtest` profiles, their Genesis blocks, files,
and CLI stay documented under `docs/`. A proposed v2 network must use a new,
unmistakable identity and separate node-data directories. Existing wallet key
files may remain usable only after the new address and chain-ID rules are
specified; old balances do not transfer automatically. Do not describe v2 as a
production cryptocurrency or use it to store real value.
