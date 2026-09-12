# Proposed BBC v2 CLI

This is a **design sketch**. None of the commands below should be assumed to
exist in the current executable. The goal is that ordinary users can create a
wallet, send funds, and inspect a clearly qualified balance without handling
block files, account nonces, RPC tokens, or JSON configuration by hand.

## Commands for wallet owners

The proposed active profile stores the selected network, wallet, and optional
local node name. It contains paths and public settings only. Passwords, private
keys, and RPC tokens never appear in command arguments or configuration.

```text
bbc network join --file <network-manifest> --peer <public-full-node-ip>:7333
bbc network peer add <another-full-node-ip>:7333
bbc wallet create --name personal --select
bbc wallet select personal
bbc wallet list
bbc wallet address
bbc wallet balance
bbc send --to BBC_<recipient> --amount 1.25
bbc transaction status <transaction-id>
```

`wallet create` prompts for a password and encrypts the new private key. It
must not overwrite an existing wallet. `wallet select` changes only the local
selection, not the blockchain. `wallet address` prints the public receiving
address. `network join` selects the network and initial contacts without
starting a service; peer changes must not alter its consensus fingerprint.
`send` derives the next valid account nonce from the selected chain
view, displays the exact amount and fee, asks for confirmation and the wallet
password, signs locally, and submits only the signed bytes. Decimal CLI amounts
are parsed exactly into integer base units; floating-point arithmetic is never
used. Fee selection must be explicit in the protocol, even if the ordinary CLI
offers a safe default.

`wallet balance` first obtains a chain view. With a local full node, it waits
for that node to finish synchronization and reads its independently validated
state. With a wallet-only installation, it may query configured remote full
nodes, but it must label the result **remote-reported** until a verifiable
light-client protocol exists. Disagreement, unreachable peers, or an unknown
sync status must produce a visible warning or error, never a silent assertion
that the reported balance is current. Querying two peers does not by itself
prove that either is honest or up to date.

`send` reports two different milestones: *accepted to a node's mempool* and
*included in the selected chain*. A successful submission is not a confirmed
payment. `transaction status` displays pending, confirmed with depth and tip,
rejected, or unknown; a chain reorganization may return a payment to pending.

## Commands for node operators

The proposed CLI creates a local installation profile, then enables optional
roles. Wallet is available by default. A full node and miner can run together
or separately; a miner-only process must name a full-node source. `node run`
remains a foreground service for a dedicated terminal or operating-system
service manager. Everyday wallet commands use a separate terminal.

```text
bbc node init --name home --network <network-manifest> --wallet personal
bbc node enable full-node --listen <local-ip>:7333 --peer <public-ip>:7333
bbc node enable miner --reward-to <BBC-address>
bbc node run
bbc node status
bbc miner start
bbc miner stop
bbc node stop
```

Exact flags and configuration format remain subject to implementation review.
The operator CLI should generate and validate configuration, show the network
fingerprint and roles before first start, and refuse incompatible existing
data. `node status` should show local synchronization, peers, tip, and mining
state in human-readable form, with an optional machine-readable output mode.
Administration communicates only with a locally authenticated loopback
endpoint. Public peers connect only to P2P.

For a miner without a local full node, `node enable miner` will additionally
require a source peer. A wallet-only installation does not start a background
service merely to hold a key file.

## Network creation and diagnostics

Network creation is an explicit operator action; it is not part of normal
wallet use. The proposed interface separates an editable definition from the
canonical manifest distributed to participants:

```text
bbc network create --definition <complete-definition.json> --out <manifest.json>
bbc network inspect --file <manifest.json>
```

`network create` should produce a manifest and fingerprint only after all
consensus parameters are specified. It must not silently choose a founder
address, reward schedule, difficulty algorithm, or network ID. `network
inspect` displays these values and the exact Genesis hash without starting a
node. The definition schema and output format still require design review.
See [starting a network](network-bootstrap.md).

Low-level block-file creation, arbitrary account-state replay, raw RPC, peer
fault injection, and state dumps belong in a diagnostic namespace or separate
developer binary. Build-time exclusion can remove *test-control* endpoints from
release packages, but cannot prevent someone from modifying open-source code
and sending otherwise valid network messages. Public full nodes enforce
consensus regardless of which client generated a message.

## Current implementation mapping

| User intent | Current v1 interface | Proposed v2 interface |
| --- | --- | --- |
| Create and select wallet | `bbc wallet create --file ... --load` | `bbc wallet create --name ... --select` |
| Read balance | Offline `bbc wallet balance --block/--data-dir`; local node RPC dump | `bbc wallet balance`, with source and sync status |
| Send payment | `transaction create`, then `rpc submit` | `bbc send` |
| Run configured node | `bbc node run --config ...` | `bbc node run` for selected profile |
| Control mining | `bbc rpc start-continuous-mining/stop-mining` | `bbc miner start/stop` |
| Inspect internals | `bbc rpc status/dump`, block and chain commands | Optional operator or diagnostic commands |

The mapping is explanatory, not a commitment that v1 flags will be removed in
one release. Migration and deprecation must be planned before changing the
public CLI.
