# BBC Local RPC

## Purpose

Local RPC lets a second terminal inspect and control a persistent BBC full node
or miner. It is a command boundary on the same computer, not a public network
API. P2P remains the protocol used between BBC nodes.

The node never receives a wallet password, private key, or decrypted wallet.
Transactions are signed by a short-lived wallet command and RPC submits only
the canonical public `.bbctx` bytes.

## Starting the node

```console
bbc node run --config bbc.json
```

The command validates the persistent configuration, initializes or opens the
configured chain and mempool, starts the P2P service, adds initial peers, and
listens for RPC on the configured `127.0.0.1` port. It remains active until an
authenticated `stop` command or external process termination.

On first start, the node creates `rpc.token` from 32 cryptographically random
bytes. The file contains one 64-character hexadecimal token followed by a
newline. The node reuses it on later starts and rejects malformed existing
content. The path comes from `rpc.token_file`; the token value is never placed
in configuration, command arguments, events, or normal CLI output.

## CLI commands

Every command reads the endpoint and token-file path from the same application
configuration:

```console
bbc rpc health --config bbc.json
bbc rpc status --config bbc.json
bbc rpc dump --config bbc.json
bbc rpc ping --config bbc.json
bbc rpc start-mining --config bbc.json
bbc rpc submit --config bbc.json --transaction payment.bbctx
bbc rpc stop --config bbc.json
```

- `health` checks that the authenticated command endpoint is ready.
- `status` returns roles, software and network versions, chain tip, mempool,
  active P2P address scope, peers, synchronization, and mining state as
  formatted JSON.
- `dump` adds deterministic account and pending-transaction entries to status.
- `ping` asks the P2P layer to send a `PING` to every handshaken peer.
- `start-mining` starts one mining cycle. A combined full node and miner builds
  locally; a mining-only process requests a template from its source peer.
- `submit` validates a canonical signed transaction file, sends its public bytes
  to the node, and reports its transaction ID after mempool admission or remote
  submission.
- `stop` requests graceful shutdown after receiving its acknowledgement.

Commands return zero only after a valid successful response. Configuration,
token, connection, malformed-response, and node-reported failures return
nonzero and write an English explanation to standard error.

## Wire interface

RPC uses TCP over IPv4 loopback. Each connection carries one newline-terminated
JSON request and one newline-terminated JSON response. Requests and responses
are limited to 64 KiB. The request contains an integer `id`, a `method`, the
token, and optional `params`:

```json
{"id":1,"method":"status","token":"<64 hexadecimal characters>"}
```

A successful response contains the matching ID, `ok: true`, and `result`.
Failures contain `ok: false` and a stable error `code` plus human-readable
`message`. Authentication is checked before method dispatch, and invalid or
unauthenticated requests do not mutate node state.

The application methods are `health`, `status`, `dump`, `ping`, `start_mining`,
`submit_signed_transaction`, and `shutdown`. The shared runtime also implements
additional scenario-only methods documented in [`node-control.md`](node-control.md);
they are not exposed by the persistent CLI and must not be treated as a stable
application API.

## Current limits

- RPC cannot bind outside exact IPv4 loopback.
- P2P startup requires numeric IPv4 endpoints allowed by the configured P2P
  scope; DNS names and IPv6 are not supported.
- RPC is synchronous and intended for one local operator, not high request
  volume or untrusted public clients.
- `start-mining` starts one block attempt; continuous mining policy is not yet
  implemented.
- The user creates and signs `.bbctx` before `rpc submit`; a combined
  create-sign-submit command is not yet implemented.

The manual [`persistent-network-test.md`](persistent-network-test.md) exercise
runs two configured nodes through mining, payment confirmation, synchronization,
and restart without using the scenario runner.
