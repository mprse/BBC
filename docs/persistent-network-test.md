# Persistent Two-Node Test

## Purpose

This test exercises the normal application interface rather than the scenario
runner. Two long-running processes read persistent configuration files, connect
through P2P, confirm a signed payment, stop, and reopen the same local state.

The included configuration files use `regtest` and IPv4 loopback so the test is
fast and remains on one computer. Node A combines wallet, full-node, and miner
roles. Node B combines wallet and full-node roles. Only node A lists an initial
peer; one outbound connection is enough for both processes to exchange data.

## Preparation

Run commands from the repository root. Create the miner wallet and keep it
selected:

```console
.\build\windows-msvc-debug\bbc.exe wallet create --file miner.wallet --load
```

Copy its printed address into `miner.reward_address` in
`examples/two-node/node-a.json`. The committed address is only a valid
placeholder and has no known private key.

Create the recipient wallet without changing the selected miner wallet:

```console
.\build\windows-msvc-debug\bbc.exe wallet create --file recipient.wallet
```

Record its printed address for the payment, then validate both configurations:

```console
.\build\windows-msvc-debug\bbc.exe config validate --file examples\two-node\node-a.json
.\build\windows-msvc-debug\bbc.exe config validate --file examples\two-node\node-b.json
```

The example uses P2P ports `7433` and `7443` and RPC ports `7434` and `7444`.
Change all matching listener and peer values together if another application
already uses a port.

## Start both nodes

Open terminal 1 for node B:

```console
.\build\windows-msvc-debug\bbc.exe node run --config examples\two-node\node-b.json
```

Open terminal 2 for node A:

```console
.\build\windows-msvc-debug\bbc.exe node run --config examples\two-node\node-a.json
```

Each node creates its data directory and private RPC token on first start. Node
A automatically connects to node B. In terminal 3, inspect both processes:

```console
.\build\windows-msvc-debug\bbc.exe rpc status --config examples\two-node\node-a.json
.\build\windows-msvc-debug\bbc.exe rpc status --config examples\two-node\node-b.json
```

Both status documents should report one completed handshake and chain height
zero.

## Mine, pay, and confirm

Start one mining cycle on node A:

```console
.\build\windows-msvc-debug\bbc.exe rpc start-mining --config examples\two-node\node-a.json
```

After the block is relayed, both nodes should report height one. Create a
payment from the selected miner wallet to the recorded recipient address:

```console
.\build\windows-msvc-debug\bbc.exe transaction create --to <RECIPIENT_ADDRESS> --amount 1000000000 --fee 1000 --nonce 0 --out payment.bbctx
```

Submit the public signed file and check that both mempools contain it:

```console
.\build\windows-msvc-debug\bbc.exe rpc submit --config examples\two-node\node-a.json --transaction payment.bbctx
.\build\windows-msvc-debug\bbc.exe rpc status --config examples\two-node\node-a.json
.\build\windows-msvc-debug\bbc.exe rpc status --config examples\two-node\node-b.json
```

Start another mining cycle on node A. The next block confirms the payment:

```console
.\build\windows-msvc-debug\bbc.exe rpc start-mining --config examples\two-node\node-a.json
```

Compare deterministic account state on both nodes:

```console
.\build\windows-msvc-debug\bbc.exe rpc dump --config examples\two-node\node-a.json
.\build\windows-msvc-debug\bbc.exe rpc dump --config examples\two-node\node-b.json
```

Both nodes must report the same height-two tip, an empty mempool, recipient
balance `1000000000`, recipient nonce `0`, miner balance `9000000000`, and miner
nonce `1`. The miner receives two 50 BBC subsidies; the payment amount leaves
its account, while its transaction fee returns as part of the second block's
reward.

## Stop and reopen

Stop both processes gracefully:

```console
.\build\windows-msvc-debug\bbc.exe rpc stop --config examples\two-node\node-a.json
.\build\windows-msvc-debug\bbc.exe rpc stop --config examples\two-node\node-b.json
```

Start them again with the same `node run` commands. `rpc dump` must still show
the same tip, balances, nonces, and empty mempool. The nodes reopen their
append-only block history and SQLite caches, and each reuses its existing RPC
token.

To demonstrate catch-up synchronization, stop node B before the second mining
cycle, mine the payment block on node A, and then restart node B. Node A keeps
retrying its configured peer; after reconnecting, node B downloads the missing
block and reaches the same height-two state.

Wallets, node data, token files, and `payment.bbctx` created by this guide are
ignored by Git. Delete them only when their test state is no longer needed.

## Automated coverage

The Catch2 test `two persistent nodes confirm payment and recover after
restart` performs the same essential flow with temporary ports and directories.
It starts node A before node B to exercise automatic reconnection, confirms a
payment while node B is offline, verifies catch-up synchronization, and reopens
both nodes to verify persisted state and RPC tokens. Run it as part of the full
test suite:

```console
python tools/build.py test
```
