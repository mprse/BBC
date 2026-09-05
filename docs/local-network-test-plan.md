# BBC Local Network Scenario Testing

- Status: Implemented

## 1. Purpose

The scenario system runs a complete BBC network on one computer. It starts
separate `bbc` processes, connects them over real TCP, asks wallets to submit
signed payments, coordinates mining races, creates partitions, and verifies the
resulting chain and account state. Every run begins with empty private data
directories and the selected profile's canonical Genesis Block.

The Python runner is a test supervisor, not a trusted blockchain component. It
cannot mark a transaction valid, install a block directly, select the winning
branch, or edit a node database. Nodes still validate all P2P data through the
normal C++ code paths.

## 2. Actors and roles

A scenario actor is one long-running `bbc node run` process. Its `roles` array
may contain:

- `wallet`: owns an ephemeral test key and can sign a payment;
- `full_node`: stores and validates blocks, maintains a mempool, listens for
  peers, synchronizes, and relays accepted data;
- `miner`: requests a template and performs Proof of Work.

Roles may be combined in one actor. Every actor with `miner` names a full-node
`mining_source` and uses an outbound connection to it. A wallet can submit only
when its actor already has a handshaken full-node peer; the current scenarios do
this with `wallet + miner` actors. A wallet-only actor can supply a recipient
address but does not yet connect to P2P by itself. Each full node owns a separate
data directory; two processes must never write the same directory.

## 3. Two independent communication planes

The P2P data plane uses the same framed binary TCP protocol that nodes use with
peers. Transactions, templates, mined blocks, synchronization batches, and
liveness messages travel over this plane.

The test-control plane uses authenticated newline-delimited JSON over a
different TCP connection bound to `127.0.0.1`. It lets the runner request
status, connect or disconnect peers, start mining, submit a payment, dump state,
and shut down. Its random token, generated configuration, and ephemeral port are
private run artifacts. This endpoint is not suitable for LAN or Internet use.

See `node-control.md` for its methods and `p2p-protocol-v1.md` for the peer wire
protocol.

## 4. Scenario format

Scenario files are JSON documents under `scenarios/`. They contain:

- `schema_version` and a unique scenario `name`;
- optional runner settings in `network`;
- `actors`, including roles and logical peer relationships;
- ordered `steps` that perform actions or wait for observable conditions;
- final `assertions` over actor state and events.

The runner resolves port `0` to an available loopback port and generates a
private actor configuration only after validating the whole document. Logical
actor names, rather than fixed addresses, keep scenarios repeatable.

A minimal topology looks like this:

```json
{
  "schema_version": 1,
  "name": "two-actor-smoke",
  "network": {},
  "actors": [
    {
      "name": "node-a",
      "roles": ["full_node"],
      "control": "127.0.0.1:0",
      "p2p": "127.0.0.1:0",
      "peers": ["node-b"]
    },
    {
      "name": "node-b",
      "roles": ["full_node"],
      "control": "127.0.0.1:0",
      "p2p": "127.0.0.1:0"
    }
  ],
  "steps": [
    {"command": "start_all"},
    {"wait": "all_ready"},
    {"command": "connect_all"},
    {"wait": "full_nodes_connected"},
    {"command": "ping", "actors": ["node-a", "node-b"]},
    {"wait": "pongs"},
    {"command": "dump", "actors": "all"}
  ],
  "assertions": [
    {"all_ready": true},
    {"same_tip": ["node-a", "node-b"]}
  ]
}
```

The source files in `scenarios/` are the exact schema examples. Unsupported
steps and assertions fail explicitly. Invalid actor references, role
combinations, endpoints, and fixed-port conflicts fail before processes start.

## 5. Execution model

A normal scenario follows this sequence:

1. Validate configuration and create an isolated run directory.
2. Start the requested actors and wait for explicit `ready` events.
3. Resolve dynamic P2P ports and establish the declared topology.
4. Perform ordered actions, using condition-based waits instead of fixed sleeps.
5. Collect normalized state dumps and evaluate all assertions.
6. Request graceful shutdown, preserve diagnostic artifacts, and return a
   nonzero exit code on failure.

Mining races first deliver and validate a template at every selected miner.
The runner then releases all prepared miners at one shared near-future time.
This barrier affects only test scheduling; it does not alter blocks or
consensus. The first valid submitted block is accepted and relayed. Other
miners stop when the accepted parent becomes stale.

## 6. Network profiles and commands

The same scenario file works with two isolated profiles:

- `development` is the default and uses deliberately visible, slower mining;
- `regtest` uses a much easier real Proof-of-Work target for automated tests.

Run from the repository root:

```console
python tools/scenario.py scenarios/mining-race.json
python tools/scenario.py scenarios/mining-race.json --regtest
python tools/scenario.py scenarios/fork-reorg.json --regtest --no-ui
```

`--no-ui` emits prefixed log lines for CI, redirected output, or terminals that
cannot update an interactive dashboard. It does not change scenario behavior.
The interactive dashboard retains ten rows per actor and replaces consecutive
mining-progress rows so important mining, validation, and cancellation events
remain visible.

## 7. Included end-to-end scenarios

| File | What it proves |
|---|---|
| `two-actor-smoke.json` | Handshake, ping/pong, restart, and reconnect |
| `mining-race.json` | Competing miners, one accepted block, and stale-work cancellation |
| `transaction-propagation.json` | Signed payment submission and mempool relay |
| `transaction-confirmation.json` | Payment inclusion, rewards, fees, balances, and nonces |
| `late-join-sync.json` | Bounded block download and restart from synchronized storage |
| `fork-reorg.json` | Partition, competing branches, cumulative-work reorganization, and transaction restoration |

The scenario runner can assert readiness, peer counts, event occurrence, mining
outcomes, active height and tip, stored block count, transaction count,
mempool equality, account balances and nonces, synchronization state, reward
accounting, and confirmed-payment outcomes.

## 8. Observation and assertions

Actors write JSON Lines events with an actor-local sequence number, event name,
and structured details. Concurrent processes have no perfect global log order,
so the UI displays controller receipt order while assertions depend on object
IDs and state conditions.

Final dumps compare normalized protocol state rather than SQLite bytes. They
include network identity, roles, peer summary, active height and tip, stored
block count, account state, pending transaction IDs, miner status, and sync
status. Wallet-only actors do not have an independent blockchain.

On failure, the runner records the unmet condition and latest statuses, collects
dumps when possible, requests graceful shutdown, and force-terminates only
after a deadline.

## 9. Artifacts and secrets

Each invocation writes to:

```text
build/scenarios/<scenario-name>/<UTC-run-id>/
```

The directory contains the resolved scenario, actor event logs, isolated chain
and mempool stores, final dumps, and `summary.json`. Generated actor
configuration files are removed after success unless `--keep` is used and are
retained after failure for diagnosis.

Run directories may contain control tokens and test wallet material. They are
ignored by Git and must never be committed. Events and dumps never include
private keys, wallet passwords, or decrypted wallet material.

## 10. Current boundary

The runner and actor control server are intentionally loopback-only. The P2P
implementation is exercised across real TCP sockets, but the product does not
yet provide public node configuration, peer discovery, NAT traversal, a secured
wallet RPC, or deployment tooling. Those capabilities are required before the
same executable can be operated safely across a LAN or the public Internet.
