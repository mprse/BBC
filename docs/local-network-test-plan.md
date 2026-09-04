# Local Network Scenario Test Plan

- Status: Accepted
- Target: Stage 7 and later

## 1. Purpose

BBC needs a repeatable way to run a complete local network on one computer,
observe how its processes cooperate, submit scripted transactions, and verify
that every full node reaches the expected state. The first environment binds
only to loopback and starts every participant from the canonical Genesis Block.

The test system has two independent responsibilities:

1. Exercise the real BBC peer-to-peer protocol over TCP.
2. Orchestrate and observe processes through a separate local test-control
   channel.

The controller is a test tool. It must not become a source of blockchain truth,
relay production peer traffic, bypass transaction signatures, or mutate a full
node's database directly.

## 2. Terminology and actor roles

A scenario contains **actors**, not necessarily full nodes. Each actor is one
long-running `bbc` process with one or more capabilities:

- `wallet`: owns one test wallet and can sign transactions;
- `full_node`: stores and validates the chain, owns a mempool, listens for peers,
  and relays valid transactions and blocks;
- `miner`: performs Proof of Work.

Valid initial combinations are:

- `wallet`;
- `wallet + full_node`;
- `wallet + miner`;
- `wallet + full_node + miner`.

A miner without `full_node` is a mining worker. It must name a full node as its
`mining_source`, request a block template from it, and submit a solved block
back to it. A wallet without `full_node` must name one or more full nodes as
`submit_to` targets. A full node owns a unique data directory. Two processes
must never write the same data directory.

The example requested topology therefore has eight actors but three explicit
full nodes:

- two `wallet + full_node` actors;
- three `wallet + miner` workers;
- one `wallet + full_node + miner` actor;
- two `wallet` clients.

## 3. Process and port model

The scenario runner starts one subprocess per actor. Every actor receives:

- a stable scenario name such as `node-a` or `miner-c`;
- a private run directory;
- a test-control port;
- a P2P listen port when the actor has `full_node` capability;
- explicit initial peers or upstream full nodes;
- a network profile;
- a structured-log destination.

All initial tests use `127.0.0.1` and distinct ports. P2P and control endpoints
must never share a port. The runner validates actor names, role constraints,
port uniqueness, references to other actors, and the topology before starting
any process.

The planned process command is conceptually:

```console
bbc node run --config <generated-actor-config>
```

The exact production CLI and configuration format will be defined with the node
runtime. The scenario file remains the human-edited source; actor configuration
files are generated run artifacts.

## 4. Two communication planes

### 4.1 P2P data plane

Actors use real TCP connections for protocol behavior. The first increments are:

1. connection lifecycle, framed reads and writes, `HELLO`, `PING`, and `PONG`;
2. signed transaction submission, validation, deduplication, and relay;
3. block-template requests, solved-block submission, validation, and relay;
4. tip comparison, missing-block download, reconnect, and late-join sync;
5. fork tracking and reorganization in Stage 8.

The P2P format will be a bounded binary protocol. TCP is a byte stream, so the
implementation must correctly handle partial headers, partial payloads, and
multiple messages received in one read. Every frame will include a network
magic, protocol version, message type, payload length, and payload checksum.
Exact byte layout, byte order, size limits, handshake compatibility, and message
payloads require a separate protocol specification and ADR before coding.

Nodes must treat every peer byte as untrusted. The design needs explicit limits
for frame size, queued outbound bytes, connection count, request count, idle
time, malformed messages, duplicate object IDs, and relay loops.

### 4.2 Test-control plane

Every actor exposes an opt-in control endpoint bound only to `127.0.0.1`. It is
enabled only by an explicit scenario/test option and uses newline-delimited JSON
requests, responses, and events. Each request has a correlation ID. The runner
generates an unpredictable token for each actor and passes it without placing
wallet passwords or private keys in command-line arguments or logs.

The control plane may request normal public operations such as:

- readiness and health status;
- wallet address;
- signed payment creation and submission by that actor's wallet;
- mining start, stop, and bounded work;
- peer, chain-tip, balance, nonce, and mempool status;
- a normalized final state dump;
- graceful shutdown.

It may not inject an unsigned transaction as accepted, install a block without
normal validation, edit chain state, or declare which competing chain wins.

Control JSON is not the P2P protocol and is not exposed outside loopback. Later
developer RPC may reuse concepts, but the initial control server is test-only.

## 5. Scenario file

The first scenario format should be JSON. It is human-readable text, has mature
parsers in C++ and Python, and Python can load it without another package. JSON
also avoids inventing a custom command language. YAML support can be added later
as an input adapter if editing large scenarios becomes inconvenient.

All names, fields, commands, output, and documentation remain English. A draft
scenario has four sections:

- `network`: profile, loopback range, timeouts, and random seed;
- `actors`: roles, ports, initial peers, upstreams, and wallet fixture names;
- `steps`: commands and condition-based barriers;
- `assertions`: final consensus and actor-specific expectations.

Example shape:

```json
{
  "schema_version": 1,
  "name": "eight-actor-payment",
  "network": {
    "profile": "regtest",
    "random_seed": 7,
    "step_timeout_ms": 10000
  },
  "transactions_file": "transactions/eight-actor-payment.json",
  "actors": [
    {
      "name": "node-a",
      "roles": ["wallet", "full_node"],
      "p2p": "127.0.0.1:19001",
      "control": "127.0.0.1:20001",
      "peers": ["node-b", "hybrid"]
    },
    {
      "name": "node-b",
      "roles": ["wallet", "full_node"],
      "p2p": "127.0.0.1:19002",
      "control": "127.0.0.1:20002",
      "peers": ["node-a"]
    },
    {
      "name": "miner-a",
      "roles": ["wallet", "miner"],
      "control": "127.0.0.1:20003",
      "mining_source": "node-a"
    },
    {
      "name": "miner-b",
      "roles": ["wallet", "miner"],
      "control": "127.0.0.1:20004",
      "mining_source": "node-b"
    },
    {
      "name": "miner-c",
      "roles": ["wallet", "miner"],
      "control": "127.0.0.1:20005",
      "mining_source": "node-a"
    },
    {
      "name": "hybrid",
      "roles": ["wallet", "full_node", "miner"],
      "p2p": "127.0.0.1:19006",
      "control": "127.0.0.1:20006",
      "peers": ["node-a", "node-b"]
    },
    {
      "name": "wallet-a",
      "roles": ["wallet"],
      "control": "127.0.0.1:20007",
      "submit_to": ["node-a"]
    },
    {
      "name": "wallet-b",
      "roles": ["wallet"],
      "control": "127.0.0.1:20008",
      "submit_to": ["node-b"]
    }
  ],
  "steps": [
    {"command": "start_all"},
    {"wait": "all_ready"},
    {"wait": "full_nodes_connected"},
    {"command": "mine_blocks", "actor": "hybrid", "count": 1},
    {"wait": "height", "actors": ["node-a", "node-b", "hybrid"], "value": 1},
    {"command": "submit_transaction", "transaction": "payment-1"},
    {"wait": "transaction_in_mempools", "actors": ["node-a", "node-b", "hybrid"]},
    {"command": "mine_blocks", "actor": "miner-a", "count": 1},
    {"wait": "height", "actors": ["node-a", "node-b", "hybrid"], "value": 2},
    {"command": "dump", "actors": "all"}
  ],
  "assertions": [
    {"same_tip": ["node-a", "node-b", "hybrid"]},
    {"same_state": ["node-a", "node-b", "hybrid"]},
    {"mempool_size": 0}
  ]
}
```

Transaction recipients are actor names in the scenario. The runner resolves
them to addresses learned after readiness. A scenario may define transactions
inline, but the normal form keeps the ordered transaction catalog in the text
file named by `transactions_file`:

```json
{
  "schema_version": 1,
  "transactions": [
    {
      "name": "payment-1",
      "from": "hybrid",
      "to": "wallet-a",
      "amount": 1000000000,
      "fee": 1000
    }
  ]
}
```

Steps reference transaction names and determine exactly when the runner asks a
wallet actor to create, sign, and submit each payment. Amounts and fees are
always integer base units. By default the sending wallet obtains the next nonce
from its configured full node. Negative scenarios may provide an explicit nonce
or a named mutation, but expected-invalid data remains separate from ordinary
payment definitions.

## 6. Genesis, funding, mining, and time

Every actor receives a new empty run directory. Every full node initializes the
same network-specific Genesis Block. The scenario never copies a previous
chain, mempool, or SQLite cache into a run.

Genesis creates no funds, so a useful payment scenario must first mine one or
more reward-only blocks. The steps must explicitly state which miner receives
each reward. Starting several miners at height zero is allowed for fork tests,
but basic propagation scenarios should start one miner at a time and wait for
the block to converge before continuing.

The current fixed development target is too slow and variable for deterministic
multi-process integration tests. Stage 7 should introduce a distinct `regtest`
network profile with its own network identity, Genesis Block, and very easy
Proof-of-Work target. Production/development-chain nodes must reject regtest
messages and blocks. Exact regtest constants are a consensus decision and need
explicit approval in a separate ADR.

Scenario correctness must not depend on arbitrary sleeps. The runner uses
condition-based waits with deadlines. Block timestamps come from a deterministic
scenario clock or explicit step values, not from uncontrolled wall-clock races.
Real-time, real-difficulty mining remains an optional demonstration mode and is
not used as the required pass/fail test.

## 7. Observation and console layout

The runner captures structured JSON Lines events from every actor and renders a
single terminal dashboard with one labeled panel per actor. Eight panels can be
shown in a grid, with a separate controller timeline and summary area. This is
more portable and controllable than launching eight unrelated Windows terminal
windows.

Each event contains at least:

- actor name;
- actor-local monotonically increasing sequence number;
- event type and severity;
- relevant object ID, peer, height, or rejection reason;
- controller receipt time for display only.

Useful events include connection changes, handshake completion, transaction
accepted/rejected/relayed, candidate rebuilt, mining started/stopped, block
found/accepted/rejected/relayed, sync progress, and chain-tip changes.

Logs from concurrent processes do not have one perfect global order. The UI may
show controller receipt order, but assertions rely on object IDs and state
conditions rather than log timing. A `--no-ui` mode writes prefixed lines and is
required for CI and redirected output. Optional Windows Terminal pane launching
can be added later as a convenience frontend, not as the test engine.

## 8. Synchronization and failure handling

Every command returns an acknowledgment and, when appropriate, an object ID.
The runner then waits for explicit observable conditions. Examples:

- all required actors report `ready`;
- expected peer sessions reach `handshake_complete`;
- a transaction ID appears in selected full-node mempools;
- all selected full nodes report the same height and tip ID;
- a balance and next nonce equal expected values;
- mining has stopped before a state dump.

Every wait has a timeout and prints the unmet condition plus the latest actor
statuses. On failure, the runner stops issuing scenario actions, requests final
dumps, terminates actors gracefully, force-terminates only after a shutdown
deadline, and exits nonzero.

The runner must also detect early process exit, port bind failure, duplicate
actor names, invalid role dependencies, lost control connections, and unhandled
protocol errors.

## 9. Final dumps and assertions

Database files are not compared byte for byte. SQLite layout and local mempool
contents are implementation details. The normalized dump contains:

- actor name and enabled roles;
- network identity and protocol version;
- peer connection summary;
- chain height, tip block ID, and cumulative work when available;
- ordered block IDs or a chain digest;
- deterministic account-state digest;
- requested account balances and next nonces;
- ordered or sorted pending transaction IDs;
- miner state and last template parent;
- software version.

The default final convergence assertion compares only consensus-relevant data
between full nodes: network identity, tip, active block history, account state,
and cumulative work. Wallet-only actors have no independent chain database.
Peer order, database bytes, log order, performance measurements, and transient
mempool differences are not equality requirements.

Assertions must name their scope. Useful assertions include `same_tip`,
`same_state`, `balance`, `next_nonce`, `contains_transaction`, `mempool_size`,
`peer_connected`, `block_accepted`, and `transaction_rejected` with an exact
reason.

## 10. Run artifacts and secret handling

Each invocation writes an ignored directory such as:

```text
build/scenarios/<scenario-name>/<run-id>/
|-- scenario.resolved.json
|-- controller.events.jsonl
|-- summary.json
`-- actors/
    |-- node-a/
    |   |-- data/
    |   |-- actor.events.jsonl
    |   `-- final-dump.json
    `-- ...
```

Wallet files, passwords, private keys, control tokens, databases, and generated
actor configuration must never be committed. Deterministic cryptographic test
vectors may be committed only when clearly labeled as public fixtures that must
never hold value. Normal visual runs may generate fresh wallets. Repeatable CI
runs use reviewed public fixture keys or a reviewed deterministic fixture
mechanism built from existing cryptographic primitives.

A long-running actor loads its wallet once at startup and keeps the decrypted
key only in process memory. The runner must not send wallet passwords in P2P
messages, control JSON, command-line arguments, structured logs, scenario files,
or final dumps.

## 11. Proposed implementation increments

### Stage 7.0: Scenario foundation (implemented)

- JSON schema and validation;
- process supervisor and per-run directories;
- test-control endpoint, readiness, status, dump, and shutdown;
- structured events, plain output, and terminal dashboard;
- two processes with no P2P behavior yet.

### Stage 7.1: TCP foundation (implemented)

- cross-platform asynchronous TCP abstraction;
- bounded frame parser and serializer;
- connection state machine;
- `HELLO`, `PING`, and `PONG`;
- explicit topology and reconnect tests.

### Stage 7.2: Transaction propagation

- wallet submission to a full node;
- validation through the existing transaction and mempool layers;
- transaction-ID deduplication and loop-free relay;
- wait and rejection assertions.

### Stage 7.3: Block propagation and mining workers (first slice implemented)

- block-template request and response;
- bounded/cancellable mining work;
- solved-block submission;
- full-node validation, persistence, mempool revalidation, and relay;
- deterministic regtest flow from Genesis through a confirmed payment.

The implemented first slice covers empty height-1 templates, two outbound-only
wallet miners, one authoritative full node, real batched Proof of Work, block
submission, persistence, relay, and stale-work cancellation. Transaction-backed
templates and the confirmed-payment flow remain follow-up work.

### Stage 7.4: Initial synchronization

- tip and locator exchange;
- missing-block request and bounded batch response;
- late join and reconnect;
- restart from existing local storage;
- convergence dumps.

Stage 8 then adds competing branches, cumulative work, reorganization, network
partition controls, healing, and eligible transaction reinsertion.

## 12. Decisions required before Stage 7 implementation

The following choices remain deliberately open:

1. How public deterministic wallet fixtures are generated and loaded.

Resolved choices are recorded in ADRs 0008 through 0010. The
remaining choices should be resolved in small ADRs. The scenario runner should be
Python because Python is already the cross-platform developer entry point and
is well suited to subprocess orchestration. Consensus, P2P validation, wallet
signing, block creation, and mining remain in the C++ executable.
