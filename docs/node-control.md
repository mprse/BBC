# Stage 7.0 Node Control and Scenario Runner

## Scope

Stage 7.0 introduces a long-running BBC actor process and a Python supervisor.
It deliberately does not implement peer-to-peer communication yet. Its purpose
is to prove process startup, isolated storage, loopback control, structured
events, state collection, assertions, and graceful shutdown before P2P behavior
adds concurrency.

## Actor command

```console
bbc node run --config <generated-actor-config.json>
```

The configuration is generated inside an ignored scenario run directory. Its
Stage 7.0 fields are:

```json
{
  "schema_version": 1,
  "name": "node-a",
  "roles": ["wallet", "full_node"],
  "data_directory": "<absolute-run-directory>",
  "control": {
    "host": "127.0.0.1",
    "port": 0,
    "token": "<random-per-actor-token>"
  }
}
```

Actor names contain 1 through 64 ASCII letters, digits, underscores, or hyphens.
Roles are unique values from `wallet`, `full_node`, and `miner`. The control host
must be exactly `127.0.0.1`. Port zero asks the operating system for an available
ephemeral port; a nonzero port must fit an unsigned 16-bit value. Control tokens
contain between 32 and 256 characters.

The Stage 7.0 wallet is ephemeral and exists only in actor memory. A full-node
actor initializes canonical Genesis in its empty private data directory or
validates and opens an existing store. Persistent scenario wallets, transaction
submission, mining, and peer connections are later increments.

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
- `shutdown`: acknowledge and stop the accept loop gracefully.

Requests with missing fields, invalid JSON, an invalid token, or an unknown
method never mutate actor state.

## Structured events

The actor writes newline-delimited JSON events to standard output. Each event has
an actor name, actor-local sequence, event name, and details object. The first
event is `ready` and reports the actual control port. The final graceful event is
`stopped`. Tokens, passwords, and private keys are never emitted.

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
and returns nonzero on failure. It never relies on a startup sleep.

The interactive renderer arranges recent actor events into terminal panels. On
Windows it first enables Virtual Terminal processing. If the current console
does not support that mode, the runner automatically falls back to plain output
with `[actor-name]` prefixes instead of printing raw ANSI escape sequences. Both
modes use exactly the same scenario execution and assertions.

Stage 7.0 implements these steps:

- `{"command": "start_all"}`;
- `{"wait": "all_ready"}`;
- `{"command": "dump", "actors": "all"}`.

It implements `all_ready`, `same_tip`, exact `height`, and full-node
`mempool_size` assertions. Unsupported future steps and assertions fail
explicitly rather than being ignored.

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
