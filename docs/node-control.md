# Stage 7 Node Control and Scenario Runner

## Scope

Stage 7.0 introduced a long-running BBC actor process and a Python supervisor.
Stage 7.1 adds a separate binary P2P listener for full-node actors, explicit
scenario topology, handshake observation, ping checks, and reconnect tests.
Stage 7.2 adds signed transaction submission and mempool convergence. Stage 7.3
adds network profiles and scenario-controlled mining workers. Stage 7.4 adds
staged actor startup, initial block synchronization, and restart-from-storage
verification.

## Actor command

```console
bbc node run --config <generated-actor-config.json>
```

The configuration is generated inside an ignored scenario run directory:

```json
{
  "schema_version": 1,
  "name": "node-a",
  "roles": ["wallet", "full_node"],
  "data_directory": "<absolute-run-directory>",
  "network": "development",
  "p2p": {
    "host": "127.0.0.1",
    "port": 0
  },
  "control": {
    "host": "127.0.0.1",
    "port": 0,
    "token": "<random-per-actor-token>"
  }
}
```

Actor names contain 1 through 64 ASCII letters, digits, underscores, or hyphens.
Roles are unique values from `wallet`, `full_node`, and `miner`. A `full_node`
must have a `p2p` object; actors without that role must not have one. Both hosts
must be exactly `127.0.0.1`. Port zero asks the operating system for an available
ephemeral port; a nonzero port must fit an unsigned 16-bit value. Control tokens
contain between 32 and 256 characters.
`network` is required and is either `development` or `regtest`.

The scenario wallet is ephemeral and exists only in actor memory. A full-node
actor initializes profile-specific Genesis in its empty private data directory
or validates and opens an existing store. Stage 7.3 adds outbound-only mining
workers and the `start_mining` control action. A networked wallet can sign and
submit one transaction through `submit_transaction`; persistent scenario
wallets and automatic nonce lookup remain later increments. P2P behavior is defined in
[`p2p-protocol-v1.md`](p2p-protocol-v1.md) and
[`mining-protocol-v1.md`](mining-protocol-v1.md), with transaction messages in
[`transaction-protocol-v1.md`](transaction-protocol-v1.md) and synchronization
messages in [`sync-protocol-v1.md`](sync-protocol-v1.md).

## Control protocol

The control endpoint uses TCP only on IPv4 loopback. This is test orchestration,
not the BBC P2P data plane. One connection carries one newline-terminated JSON
request and one newline-terminated JSON response. Requests are limited to 64
KiB.

Request:

```json
{"id": 1, "method": "status", "token": "<actor-token>"}
```

Successful response:

```json
{"id": 1, "ok": true, "result": {"ready": true}}
```

Failed response:

```json
{
  "id": 1,
  "ok": false,
  "error": {"code": "unauthorized", "message": "Invalid control token."}
}
```

Supported methods are:

- `health`: return readiness;
- `status`: return actor, wallet, chain-tip, and mempool summary;
- `dump`: return the status plus deterministic account entries;
- `connect_peer`: start or retain an outbound P2P connection to the loopback
  endpoint supplied in `params`;
- `ping`: send a P2P `PING` to every handshaken peer and return its nonce;
- `start_mining`: ask the miner's connected full node for a block template and
  begin bounded mining work; the scenario runner supplies `defer_work: true` to
  prepare a coordinated race;
- `begin_mining`: release a prepared template at `start_at_unix_ms`; this
  test-only barrier gives every selected miner the same start instant;
- `submit_transaction`: sign a payment with the actor's in-memory wallet and
  submit it to a handshaken full-node peer; parameters are `recipient`,
  `amount`, `fee`, and `nonce`;
- `shutdown`: acknowledge and stop the accept loop gracefully.

Requests with missing fields, invalid JSON, an invalid token, or an unknown
method never mutate actor state.

## Structured events

The actor writes newline-delimited JSON events to standard output. Each event has
an actor name, actor-local sequence, event name, and details object. The first
event is `ready` and reports the actual control port plus the P2P port of a full
node. P2P events include connection, handshake, pong, rejection, and disconnect
information. The final graceful event is `stopped`. Tokens, session nonces,
passwords, and private keys are never emitted.

## Scenario runner

Run the first scenario from the repository root:

```console
python tools/scenario.py scenarios/two-actor-smoke.json
```

Use `--no-ui` for prefixed line output in CI or redirected terminals:

```console
python tools/scenario.py scenarios/two-actor-smoke.json --no-ui
```

The runner validates the JSON document, creates one private directory and random
control token per actor, starts all subprocesses, waits for explicit `ready`
events, requests final dumps, evaluates assertions, requests graceful shutdown,
and returns nonzero on failure. Actor `peers` entries name other full-node
actors; the runner resolves their dynamic ports only after readiness. It never
relies on a startup sleep.

The interactive renderer arranges recent actor events into terminal panels. On
Windows it first enables Virtual Terminal processing. If the current console
does not support that mode, the runner automatically falls back to plain output
with `[actor-name]` prefixes instead of printing raw ANSI escape sequences. Both
modes use exactly the same scenario execution and assertions.

Implemented steps are:

- `{"command": "start_all"}`;
- `{"command": "start", "actors": ["node-a", "node-b"]}`;
- `{"wait": "all_ready"}`;
- `{"wait": "actors_ready", "actors": ["node-a", "node-b"]}`;
- `{"command": "connect_all"}`;
- `{"command": "connect_all", "actors": ["node-a", "node-b"]}`;
- `{"wait": "full_nodes_connected"}`;
- `{"command": "ping", "actors": ["node-a", "node-b"]}`;
- `{"wait": "pongs"}`;
- `{"command": "start_mining", "actors": ["miner-a", "miner-b"]}`;
- `{"wait": "mining_complete", "height": 1}`;
- `{"command": "submit_transaction", "from": "mining_winner", "miners":
  ["miner-a", "miner-b"], "to": "receiver", "amount": 1000000000,
  "fee": 1000, "nonce": 0}`;
- `{"wait": "transaction_propagated"}`;
- `{"wait": "sync_complete", "actors": ["node-c"], "height": 2}`;
- `{"command": "restart", "actor": "node-b"}`;
- `{"command": "dump", "actors": "all"}`.

The mining command and completion wait may be repeated for later heights. The
runner records one winner per completed round so assertions can distinguish the
funded sender from the miner that confirms its payment.
Actor-scoped `start`, readiness, connection, and peer waits let a scenario keep
a node offline while the initial chain advances. `sync_complete` waits for both
the requested height and an explicit successful target-tip match.

It implements `all_ready`, `same_tip`, `same_state`, exact `height`, exact
`tip_transaction_count`, full-node `mempool_size`, `same_mempool`, per-actor
`peer_count`, `mining_outcome`, `winner_reward`, and `payment_confirmed`
assertions. `sync_state` can additionally require a full node's final state and
download count. `payment_confirmed` verifies the dynamic balance outcome for either
the same or different winners, including the height-2 fee, sender nonce, and
total supply across all completed mining rounds. Restart preserves the
actor data directory and resolved P2P port, allowing configured peers to prove
automatic reconnect. Unsupported future steps and assertions fail explicitly
rather than being ignored.

The scenario-level `start_mining` command is coordinated internally. The runner
requests and validates every selected miner's template first, waits until all
miners report `ready`, then schedules `begin_mining` for one shared near-future
Unix-millisecond timestamp. This prevents an easy regtest solution from being
accepted before another miner has even received its template.

## Artifacts

Each run writes under:

```text
build/scenarios/<scenario-name>/<UTC-run-id>/
```

Artifacts include the resolved scenario, actor event logs, isolated chain and
mempool storage, final dumps, and `summary.json`. Generated actor configuration
files are deleted after a successful run unless `--keep` is specified. They are
kept after failure for diagnosis, but their expired control tokens must still be
treated as local test data and never committed.
