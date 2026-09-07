# BBC

Bi-Bi-Coin (BBC) is an educational C++20 blockchain implementation. It is
designed to make wallets, signed transactions, blocks, Proof of Work, full-node
validation, mining, synchronization, forks, and reorganizations observable in
small deterministic experiments.

BBC is not production cryptocurrency software and must not be used to store
real value.

## Documentation

Start with these documents:

- [Documentation index](docs/README.md) — where each design topic is explained;
- [Glossary](docs/glossary.md) — blockchain, networking, cryptography, and build
  terminology;
- [Architecture overview](docs/architecture.md) — components, process roles,
  data flow, APIs, and trust boundaries;
- [Toolchain and build guide](docs/toolchain.md) — Windows development and
  Linux/EC2 builds;
- [Local network scenarios](docs/local-network-test-plan.md) — repeatable
  multi-process demonstrations.

Protocol decisions and their trade-offs are recorded in [ADRs](docs/adr/).
`BBC_PROJECT.md` is the original Polish concept document; tested code and the
English reference documents under `docs/` describe the current implementation.

## Quick start on Windows

Prerequisites and installation details are in [docs/toolchain.md](docs/toolchain.md).
From an ordinary PowerShell or `cmd` terminal in the repository root:

```console
python tools/build.py test
python tools/build.py run
```

The helper discovers Visual Studio Build Tools, prepares the MSVC environment
for its child processes, configures CMake, installs declared vcpkg dependencies,
builds with Ninja, and runs CTest. It does not modify the machine environment or
require a PowerShell execution-policy change.

After building, invoke the Windows executable directly with:

```console
.\build\windows-msvc-debug\bbc.exe help
```

## Implemented system

BBC currently provides:

- Ed25519 wallets encrypted with Argon2id and XChaCha20-Poly1305;
- deterministic BBC addresses and canonical signed transaction files;
- canonical blocks with transaction Merkle roots and a fixed Proof-of-Work
  rule;
- an account-based state with balances, transaction nonces, block subsidies,
  and fees;
- append-only block storage plus a rebuildable SQLite index and state cache;
- a persistent local mempool with nonce, balance, duplicate, capacity, and fee
  selection policy;
- framed TCP peer communication, handshake validation, transaction and block
  relay, mining workers, and initial synchronization;
- persistent side branches, cumulative-work fork choice, reorganizations, and
  detached-transaction restoration;
- a Python scenario runner for observable multi-process tests on one computer;
- strict persistent application configuration with a long-running node runtime
  and an authenticated loopback RPC client;
- optional continuous mining with explicit automatic startup and local RPC
  start/stop controls.

Persistent application P2P supports explicit loopback, private-LAN, and
Internet scopes with numeric unicast IPv4 endpoints. Application RPC and
scenario control remain restricted to IPv4 loopback. DNS peer resolution,
automatic peer discovery, IPv6, and production deployment are not implemented.

## Application configuration

BBC keeps persistent user configuration separate from generated scenario actor
configuration. Validate or inspect the included example with:

```console
.\build\windows-msvc-debug\bbc.exe config validate --file examples\application-config.json
.\build\windows-msvc-debug\bbc.exe config show --file examples\application-config.json
```

The parser validates the mandatory wallet role, optional full-node and miner
roles, paths, endpoints, peers, mining reward address, and loopback RPC
boundary. Start the configured services in one terminal:

```console
.\build\windows-msvc-debug\bbc.exe node run --config examples\application-config.json
```

Inspect and stop the process from another terminal:

```console
.\build\windows-msvc-debug\bbc.exe rpc status --config examples\application-config.json
.\build\windows-msvc-debug\bbc.exe rpc start-continuous-mining --config examples\application-config.json
.\build\windows-msvc-debug\bbc.exe rpc stop-mining --config examples\application-config.json
.\build\windows-msvc-debug\bbc.exe rpc stop --config examples\application-config.json
```

The scenario runner continues to use the separate
`node run --scenario-config` interface. See
[application-config.md](docs/application-config.md) for the complete format and
[`rpc.md`](docs/rpc.md) for local command details.

A complete two-node exercise using persistent configuration, mining, payment,
catch-up synchronization, and restart is available in
[persistent-network-test.md](docs/persistent-network-test.md).
For two physical Windows computers on one trusted local network, follow
[lan-operation.md](docs/lan-operation.md).
The static public-peer model for an EC2 seed/full node and clients behind NAT is
described in [internet-p2p.md](docs/internet-p2p.md).

## Wallet workflow

Create an encrypted wallet and make it the selected wallet:

```console
.\build\windows-msvc-debug\bbc.exe wallet create --file wallet.dat --load
```

Inspect the selection and address:

```console
.\build\windows-msvc-debug\bbc.exe wallet selected
.\build\windows-msvc-debug\bbc.exe wallet address
```

The selection stores only the absolute wallet path. Commands that need the
private key ask for the password with terminal echo disabled. Passwords and
private keys are never accepted as command-line arguments.

Create, inspect, and verify a signed transaction:

```console
.\build\windows-msvc-debug\bbc.exe transaction create --to <BBC-address> --amount 1000000000 --fee 1000 --nonce 0 --out payment.bbctx
.\build\windows-msvc-debug\bbc.exe transaction show --file payment.bbctx
.\build\windows-msvc-debug\bbc.exe transaction verify --file payment.bbctx
```

Amounts and fees are integer base units. One BBC equals 100,000,000 base units.
The account nonce orders one sender's transactions and is unrelated to the
mining nonce.

## Blocks, mining, and chain state

Display canonical Genesis:

```console
.\build\windows-msvc-debug\bbc.exe block genesis
```

Create and mine a block candidate:

```console
.\build\windows-msvc-debug\bbc.exe block create --height 1 --previous 10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2 --reward-to <BBC-address> --timestamp 1788393600 --out candidate-1.bbcblock
.\build\windows-msvc-debug\bbc.exe block mine --file candidate-1.bbcblock --out block-1.bbcblock
```

Replay block files or import them into persistent node storage:

```console
.\build\windows-msvc-debug\bbc.exe chain verify --block block-1.bbcblock
.\build\windows-msvc-debug\bbc.exe chain init --data-dir node-a
.\build\windows-msvc-debug\bbc.exe chain add --data-dir node-a --block block-1.bbcblock
.\build\windows-msvc-debug\bbc.exe chain tip --data-dir node-a
.\build\windows-msvc-debug\bbc.exe wallet balance --data-dir node-a
```

Genesis grants no funds. Every valid non-Genesis block creates a subsidy of
50 BBC for its reward recipient. Fees are deducted from senders and added to
that reward. Exact validity and storage rules are documented in
[chain-state.md](docs/chain-state.md) and [chain-store.md](docs/chain-store.md).

## Mempool workflow

Store a pending transaction and create a candidate from the current chain and
mempool:

```console
.\build\windows-msvc-debug\bbc.exe mempool add --data-dir node-a --transaction payment.bbctx
.\build\windows-msvc-debug\bbc.exe mempool list --data-dir node-a
.\build\windows-msvc-debug\bbc.exe block candidate --data-dir node-a --reward-to <BBC-address> --timestamp 1788393601 --out candidate-2.bbcblock
```

The mempool is local policy and a rebuildable cache, not consensus history. See
[mempool.md](docs/mempool.md) for admission, persistence, candidate selection,
and reorganization behavior.

## Network demonstrations

Run the fast automated scenarios with the isolated `regtest` network profile:

```console
python tools/scenario.py scenarios/two-actor-smoke.json --regtest --no-ui
python tools/scenario.py scenarios/mining-race.json --regtest --no-ui
python tools/scenario.py scenarios/transaction-propagation.json --regtest --no-ui
python tools/scenario.py scenarios/transaction-confirmation.json --regtest --no-ui
python tools/scenario.py scenarios/late-join-sync.json --regtest --no-ui
python tools/scenario.py scenarios/fork-reorg.json --regtest --no-ui
```

Omit `--regtest` to use the slower `development` Proof-of-Work target and omit
`--no-ui` to display the interactive actor dashboard. Generated data, logs,
resolved configuration, dumps, and summaries are written below
`build/scenarios/<scenario>/<run-id>/`.

The scenarios demonstrate connection handshakes, mining races, transaction
propagation and confirmation, late-node synchronization, network partitions,
competing branches, chain reorganization, mempool restoration, and restart from
persistent storage.

## Visual Studio Code

Open the repository, run `CMake: Select Configure Preset`, select
`Windows MSVC Debug`, and then run `CMake: Build`. The same build remains
available from the integrated terminal through `python tools/build.py build`.
