# BBC Application Configuration Version 1

## Purpose

An application configuration describes one persistent BBC installation. It
selects the network, wallet, optional full-node and miner roles, local data
directory, peer endpoints, and the loopback command endpoint that will be used
by a separately running CLI client.

This file is different from the generated scenario actor configuration. A user
creates and keeps an application configuration. The scenario runner creates
short-lived actor configurations inside `build/scenarios/` and controls them
through test-only methods.

The parser, long-running node command, and local RPC client described here are
implemented. The scenario runner retains its independent
`bbc node run --scenario-config` boundary.

## Complete example

```json
{
  "schema_version": 1,
  "name": "home-node",
  "network": "development",
  "roles": ["wallet", "full_node", "miner"],
  "wallet": {
    "file": "wallet.dat"
  },
  "data_directory": "data",
  "full_node": {
    "scope": "lan",
    "listen": {
      "host": "192.168.1.10",
      "port": 7333
    },
    "peers": [
      {
        "host": "192.168.1.20",
        "port": 7333
      }
    ]
  },
  "miner": {
    "reward_address": "BBC_4400000000000000000000000000000000000000000000000000000000000000",
    "auto_start": false
  },
  "rpc": {
    "listen": {
      "host": "127.0.0.1",
      "port": 7334
    },
    "token_file": "rpc.token"
  }
}
```

JSON field names and role names are case-sensitive. Unknown fields are rejected
so a spelling mistake cannot silently disable a security or networking option.
The complete file is limited to 1 MiB.

## Common fields

| Field | Rule |
|---|---|
| `schema_version` | Required unsigned integer with value `1` |
| `name` | 1 to 64 ASCII letters, digits, underscores, or hyphens |
| `network` | `development` or `regtest` |
| `roles` | Unique role names; `wallet` is always required |
| `wallet.file` | Non-empty path to the encrypted BBC wallet |
| `data_directory` | Non-empty path for persistent application data |

Relative paths are resolved from the directory containing the configuration,
not from the terminal's current directory. The parser normalizes paths but does
not create directories or require the wallet, token, or data files to exist.
That separation lets `bbc config validate` check a configuration before setup.
The configuration file, wallet file, data directory, and RPC token file must
identify distinct paths.

## Roles

`wallet` is mandatory. It means this installation has one encrypted wallet file
used by local CLI commands. A wallet password and private key are never stored
in this configuration. The long-running process does not open or decrypt the
wallet.

`full_node` enables persistent blockchain validation, a mempool, a P2P listener,
block synchronization, and relay. It requires the `full_node` object.

`miner` enables Proof-of-Work mining. It requires the `miner` object. A miner
combined with `full_node` uses the local full node. A miner without `full_node`
must provide a remote `source` endpoint.

An installation with only `wallet` has no long-running process and therefore
must not contain `full_node`, `miner`, or `rpc` objects. A full node or miner
requires the loopback `rpc` object so another terminal can inspect and control
the running process.

## Full-node settings

```json
{
  "scope": "lan",
  "listen": {"host": "192.168.1.10", "port": 7333},
  "peers": [
    {"host": "192.168.1.20", "port": 7333}
  ]
}
```

`scope` is optional and accepts `loopback`, `lan`, or `internet`. An omitted
scope defaults to `lan`. `loopback` accepts only `127.0.0.0/8`; `lan` accepts
loopback and the RFC 1918 private ranges; `internet` accepts numeric unicast
IPv4 including public addresses. A public endpoint is rejected unless
`internet` is selected explicitly.

`listen` is required and identifies the inbound P2P TCP endpoint.
`peers` is optional and contains at most 64 unique initial peer endpoints. Ports
range from 1 through 65535. Version 1 host values support DNS names and IPv4
literals; whitespace, control characters, URL schemes, paths, and embedded
ports are rejected. The listener itself cannot also appear as an initial peer.

Version 1 parsing accepts syntactically valid DNS names and IPv4 endpoints, but
the current persistent runtime starts only when its P2P listener, initial peers,
and mining source use numeric IPv4 addresses allowed by the selected scope. The
private ranges are `10.0.0.0/8`, `172.16.0.0/12`, and `192.168.0.0/16`.
Wildcard `0.0.0.0`, multicast, limited broadcast, DNS names, and IPv6 are
rejected before the node creates its RPC token or persistent state. The
listener must name one exact address assigned to the local computer. See
[`lan-operation.md`](lan-operation.md) for private-network configuration and
[`internet-p2p.md`](internet-p2p.md) for the explicit public boundary.

## Miner settings

```json
{
  "reward_address": "BBC_4400000000000000000000000000000000000000000000000000000000000000",
  "source": {"host": "127.0.0.1", "port": 7333},
  "auto_start": false
}
```

`reward_address` is required and must be a canonical BBC address for the
selected network's current address format. It is public and does not unlock the
wallet. `source` is required for a mining-only installation and forbidden when
the same process has the `full_node` role. `auto_start` is an optional Boolean
that defaults to `false`. When it is `true`, the persistent process begins
continuous mining after its P2P and RPC listeners are ready.

## Local RPC settings

```json
{
  "listen": {"host": "127.0.0.1", "port": 7334},
  "token_file": "rpc.token"
}
```

Version 1 RPC is always bound to IPv4 loopback. Its port ranges from 1 through
65535 and must differ from the full-node P2P port when that listener also uses
`127.0.0.1`. `token_file` names a separate local secret generated during setup.
The token itself must never appear in the JSON configuration, logs, command
line, or Git.

`bbc node run --config` creates the token file on first start from 32 bytes of
operating-system cryptographic randomness and stores it as 64 hexadecimal
characters. Later starts require that exact format and reuse the same token.
The node and CLI compare or read the secret internally; normal output reveals
only public node state.

The transaction client opens and decrypts the wallet in its own short-lived
process when creating a `.bbctx` file. RPC sends only that public signed
transaction to the running node. The node process and RPC therefore do not
receive the wallet password or private key.

## CLI inspection

```console
bbc config validate --file bbc.json
bbc config show --file bbc.json
```

The repository contains a non-secret starting point at
`examples/application-config.json`. Copy it before making machine-specific
changes; do not place a real wallet, RPC token, or node database in Git.

`validate` returns success only when every field and cross-field rule passes.
`show` prints the normalized configuration, including resolved absolute paths,
without opening the wallet or token file and without printing secret content.

## Starting and controlling services

Start a configured full node or miner in a dedicated terminal:

```console
bbc node run --config bbc.json
```

The process initializes or reopens full-node storage, starts P2P, adds initial
peers, creates or validates the RPC token, writes structured lifecycle events to
standard output, and waits for an authenticated RPC request to stop. A
wallet-only configuration is intentionally rejected by `node run` because it
has no background service.

A combined `full_node` and `miner` uses its local validated chain and mempool to
create a candidate. A mining-only process requests work from its configured
`miner.source`. `bbc rpc start-mining` starts one block attempt.
`bbc rpc start-continuous-mining` keeps requesting or building new work after
each result, and `bbc rpc stop-mining` cancels current work. The same continuous
mode starts automatically when `miner.auto_start` is `true`.

Use another terminal for `bbc rpc ...` commands. See [`rpc.md`](rpc.md) for the
complete command and wire interfaces.

## C++ API

`bbc::config::load_application_config()` in
`include/bbc/config/application_config.hpp` parses and validates an untrusted
file. It returns `ApplicationConfigResult`, containing either a complete typed
configuration value or an `ApplicationConfigError`. Callers use the
typed role, P2P scope, endpoint, full-node, miner, and RPC settings rather than
reading JSON independently.
