# BBC

Bi-Bi-Coin is an educational C++ project for learning how a blockchain works.

## Prerequisites

- Visual Studio Code
- Microsoft C/C++ extension
- CMake Tools extension
- Visual Studio Build Tools with the MSVC compiler and Windows SDK
- CMake and Ninja
- Python 3.9 or newer
- vcpkg available on `PATH`

The build uses the pinned vcpkg manifest to install Asio, nlohmann/json,
libsodium, SQLite, and Catch2. If
`VCPKG_ROOT` is not already set, the build helper derives it from the `vcpkg`
executable found on `PATH`.

## Build and run

The cross-platform build helper prepares the native compiler environment,
configures the project, builds it, and runs the executable:

```console
python tools/build.py run
```

Available actions:

```console
python tools/build.py configure
python tools/build.py build
python tools/build.py test
python tools/build.py run
python tools/build.py shell
```

On Windows, the helper automatically discovers Visual Studio Build Tools and
configures MSVC without requiring PowerShell execution-policy changes. On Linux
it uses GCC, and on macOS it uses Clang.

## Command-line interface

Run the application without arguments to display help:

```console
python tools/build.py run
```

After building, commands can also be invoked directly:

```console
./build/linux-gcc-debug/bbc help
./build/linux-gcc-debug/bbc version
./build/linux-gcc-debug/bbc wallet help
./build/linux-gcc-debug/bbc wallet-demo
```

On Windows, use `build\windows-msvc-debug\bbc.exe` instead.

## Stage 1: cryptography and wallet

The first implementation stage provides:

- Ed25519 key generation, signing, and signature verification through libsodium,
- BBC address derivation from the SHA-256 hash of the public key,
- password-encrypted wallet persistence using Argon2id and
  XChaCha20-Poly1305,
- authenticated rejection of incorrect passwords and modified wallet files.

The current address form is `BBC_` followed by the 64-character uppercase
SHA-256 digest of the 32-byte public key. This format is intentionally simple
for the educational stage and is not yet a stable public-network address format.

Create, select, and inspect a persistent encrypted wallet with:

```powershell
.\build\windows-msvc-debug\bbc.exe wallet create --file wallet.dat --load
.\build\windows-msvc-debug\bbc.exe wallet selected
.\build\windows-msvc-debug\bbc.exe wallet address
```

`--load` makes the newly created wallet the selected wallet. An existing wallet
can be selected later with:

```powershell
.\build\windows-msvc-debug\bbc.exe wallet select --file another-wallet.dat
```

The selection stores only the wallet's absolute path, never its password or
private key. Each CLI invocation is a separate process, so commands that decrypt
the wallet still request its password. On Windows the selection is stored under
`%LOCALAPPDATA%\BBC`; Linux uses `$XDG_CONFIG_HOME/bbc` or `~/.config/bbc`, and
macOS uses `~/Library/Application Support/BBC`. `BBC_CONFIG_HOME` can override
this directory.

The application asks for the password interactively with terminal echo disabled.
There is deliberately no `--password` option because process arguments can be
recorded in shell history or observed by other programs.

Sign a message with the encrypted wallet:

```powershell
.\build\windows-msvc-debug\bbc.exe wallet sign `
    --message "Hello BBC"
```

`wallet address` and `wallet sign` use the selected wallet by default. Their
optional `--file <path>` argument temporarily uses another wallet without
changing the selection.

The command prints the public key and signature as uppercase hexadecimal text.
Verify them without opening a wallet:

```powershell
.\build\windows-msvc-debug\bbc.exe wallet verify `
    --public-key <64-hex-characters> `
    --message "Hello BBC" `
    --signature <128-hex-characters>
```

Signature verification covers the exact message bytes. Changing whitespace,
letter case, or any other character causes verification to fail. The save
operation refuses to overwrite an existing wallet file.

The original in-memory smoke demonstration remains available:

```powershell
.\build\windows-msvc-debug\bbc.exe wallet-demo
```

The binary wallet format and security decisions are documented in
[`docs/wallet-file-format.md`](docs/wallet-file-format.md) and
[`docs/adr/0001-stage-1-cryptography.md`](docs/adr/0001-stage-1-cryptography.md).

## Stage 2: signed transaction files

Create and sign a transaction with the selected wallet:

```powershell
.\build\windows-msvc-debug\bbc.exe transaction create `
    --to BBC_000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F `
    --amount 1000 `
    --fee 5 `
    --nonce 0 `
    --out payment.bbctx
```

`amount` and `fee` are unsigned integers expressed in BBC base units. One BBC
equals 100,000,000 base units.
`nonce` is the sender account nonce, not a mining nonce.

The command asks for the selected wallet password, signs the canonical binary
transaction, and writes an exact 161-byte file. It refuses to overwrite an
existing output file. Use `--wallet <path>` to sign once with a wallet other
than the selected wallet.

Inspect or verify a transaction without opening a wallet:

```powershell
.\build\windows-msvc-debug\bbc.exe transaction show --file payment.bbctx
.\build\windows-msvc-debug\bbc.exe transaction verify --file payment.bbctx
```

Transaction files contain only public transaction data and an Ed25519
signature. They never contain a password or private key. The exact encoding is
specified in [`docs/transaction-format.md`](docs/transaction-format.md), and
the protocol decision is recorded in
[`docs/adr/0002-transaction-v1.md`](docs/adr/0002-transaction-v1.md).

## Stage 3: blocks and Genesis

Display the canonical Genesis Block:

```powershell
.\build\windows-msvc-debug\bbc.exe block genesis
```

Genesis is empty, grants no initial funds, and has this fixed block ID:

```text
10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2
```

Create an unmined reward-only first-block candidate:

```powershell
.\build\windows-msvc-debug\bbc.exe block create `
    --height 1 `
    --previous 10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2 `
    --reward-to BBC_4400000000000000000000000000000000000000000000000000000000000000 `
    --timestamp 1788393600 `
    --out candidate-1.bbcblock
```

Use a real wallet address for `--reward-to`. Add signed transaction files by
repeating `--transaction`:

```powershell
.\build\windows-msvc-debug\bbc.exe block create `
    --height 1 `
    --previous <64-character-block-id> `
    --reward-to <BBC-address> `
    --timestamp <unix-seconds> `
    --transaction payment-1.bbctx `
    --transaction payment-2.bbctx `
    --out candidate-1.bbcblock
```

The optional `--target` is a 64-character hexadecimal 256-bit value and defaults
to the fixed Stage 4 network target. The optional `--nonce` defaults to zero.
Custom targets remain useful for malformed-input tests, but they fail network
Proof of Work verification.

Inspect a canonical block file:

```powershell
.\build\windows-msvc-debug\bbc.exe block show --file candidate-1.bbcblock
```

Block v1 has a 165-byte header and between zero and 1000 canonical Transaction
v1 values. The exact encoding and Genesis constants are specified in
[`docs/block-format.md`](docs/block-format.md). The decision is recorded in
[`docs/adr/0003-block-v1-and-genesis.md`](docs/adr/0003-block-v1-and-genesis.md).

## Stage 4: Proof of Work

Mine an existing candidate by searching its mining nonce:

```powershell
.\build\windows-msvc-debug\bbc.exe block mine `
    --file candidate-1.bbcblock `
    --out block-1.bbcblock
```

Mining keeps the candidate content fixed, begins at its encoded nonce, and
increments that nonce until the block hash is below the target. The command
prints the winning nonce, number of attempts, elapsed time, and hash rate. It
never overwrites an existing output file.

Use `--max-attempts <value>` to stop after a bounded amount of work. A stopped
run prints the next untested nonce, which can be placed in a new candidate with
`block create --nonce <value>` before resuming.

Verify the canonical format and Proof of Work:

```powershell
.\build\windows-msvc-debug\bbc.exe block verify --file block-1.bbcblock
```

The initial network target is fixed at:

```text
000000FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF
```

Genesis is exempt from mining. The exact Proof of Work rules and deterministic
vector are specified in [`docs/proof-of-work.md`](docs/proof-of-work.md), and
the decision is recorded in
[`docs/adr/0004-fixed-proof-of-work.md`](docs/adr/0004-fixed-proof-of-work.md).

## Stage 5: Linear chain and account state

Replay mined block files in height order and verify the resulting chain:

```powershell
.\build\windows-msvc-debug\bbc.exe chain verify `
    --block block-1.bbcblock `
    --block block-2.bbcblock
```

Inspect its tip or an account balance using the same ordered block history:

```powershell
.\build\windows-msvc-debug\bbc.exe chain tip `
    --block block-1.bbcblock

.\build\windows-msvc-debug\bbc.exe chain balance `
    --address BBC_4400000000000000000000000000000000000000000000000000000000000000 `
    --block block-1.bbcblock
```

If a wallet is selected, its address can be queried directly:

```powershell
.\build\windows-msvc-debug\bbc.exe wallet balance `
    --block block-1.bbcblock
```

Genesis is built into the executable and must not be passed as a file. Stage 5
uses `100,000,000` base units per BBC, a constant `50 BBC` block subsidy, account
nonces starting at zero, and a zero minimum fee. Fees are transferred to the
block reward recipient. Chain and account-state validation is atomic.

The exact replay and state-transition rules are specified in
[`docs/chain-state.md`](docs/chain-state.md), and the decision is recorded in
[`docs/adr/0005-linear-chain-and-account-state.md`](docs/adr/0005-linear-chain-and-account-state.md).

## Stage 5.1: Persistent chain store

Create a node data directory and import mined blocks once:

```powershell
.\build\windows-msvc-debug\bbc.exe chain init --data-dir node-a
.\build\windows-msvc-debug\bbc.exe chain add --data-dir node-a --block block-1.bbcblock
.\build\windows-msvc-debug\bbc.exe chain add --data-dir node-a --block block-2.bbcblock
```

Later queries read the stored chain directly:

```powershell
.\build\windows-msvc-debug\bbc.exe chain verify --data-dir node-a
.\build\windows-msvc-debug\bbc.exe chain tip --data-dir node-a
.\build\windows-msvc-debug\bbc.exe wallet balance --data-dir node-a
```

The authoritative append-only history is `node-a/chain/blocks.dat`. The derived
SQLite index and account-state cache is `node-a/chain/chain.db`; deleting only
that database is safe because it is rebuilt from fully validated block records
on the next open.

The format and recovery rules are specified in
[`docs/chain-store.md`](docs/chain-store.md), and the storage decision is
recorded in
[`docs/adr/0006-append-only-chain-store.md`](docs/adr/0006-append-only-chain-store.md).

## Stage 6: Mempool

Add a signed transaction to a node's local pending pool and inspect it:

```powershell
.\build\windows-msvc-debug\bbc.exe mempool add --data-dir node-a --transaction payment.bbctx
.\build\windows-msvc-debug\bbc.exe mempool list --data-dir node-a
.\build\windows-msvc-debug\bbc.exe mempool status --data-dir node-a
```

Create a mining candidate directly from the stored chain and mempool:

```powershell
.\build\windows-msvc-debug\bbc.exe block candidate --data-dir node-a --reward-to <BBC-address> --timestamp <unix-seconds> --out candidate.bbcblock
.\build\windows-msvc-debug\bbc.exe block mine --file candidate.bbcblock --out mined.bbcblock
.\build\windows-msvc-debug\bbc.exe chain add --data-dir node-a --block mined.bbcblock
```

The mempool accepts at most 10,000 transactions, requires consecutive pending
nonces per sender, and reserves pending outgoing amounts plus fees against the
confirmed balance. Candidate selection takes at most 1,000 transactions,
prefers higher fees, and preserves each sender's nonce order. Adding a block to
the chain revalidates the local queue and removes confirmed or invalid entries.

The local cache is `node-a/mempool/mempool.db`. It is not consensus history and
can be deleted to start with an empty queue. Exact policy and persistence rules
are documented in [`docs/mempool.md`](docs/mempool.md), and the decision is
recorded in
[`docs/adr/0007-local-persistent-mempool.md`](docs/adr/0007-local-persistent-mempool.md).

The proposed multi-process TCP test harness, actor roles, scenario format,
control channel, dashboard, synchronization rules, and staged P2P rollout are
described in
[`docs/local-network-test-plan.md`](docs/local-network-test-plan.md).

## Stage 7.0: Local scenario foundation

Run the first two-process scenario:

```powershell
python tools/scenario.py scenarios/two-actor-smoke.json
```

The runner starts two long-lived `bbc` processes with isolated storage, waits
for both to initialize canonical Genesis, collects normalized state dumps,
checks that their tips and heights agree, and shuts them down. It uses a panel
display in an interactive terminal and prefixed lines with `--no-ui`.

This stage contains no P2P traffic yet. Its loopback JSON control channel is
separate from the binary P2P protocol planned for Stage 7.1. The exact actor
configuration, control methods, output, and artifacts are documented in
[`docs/node-control.md`](docs/node-control.md). The dependency and architecture
decision is recorded in
[`docs/adr/0008-stage-7-networking-foundation.md`](docs/adr/0008-stage-7-networking-foundation.md).

## Stage 7.1: TCP framing and handshake

The same scenario now opens a distinct loopback P2P listener for each full node,
connects the declared topology, completes the binary `HELLO` handshake, checks
`PING/PONG` in both directions, restarts one node, and verifies automatic
reconnection before collecting final dumps:

```powershell
python tools/scenario.py scenarios/two-actor-smoke.json --no-ui
```

P2P status is available through the test-control `status` and `dump` methods.
The canonical frame bytes, handshake fields, limits, duplicate-connection rule,
and disconnect behavior are documented in
[`docs/p2p-protocol-v1.md`](docs/p2p-protocol-v1.md) and accepted by
[`docs/adr/0009-p2p-framing-and-handshake.md`](docs/adr/0009-p2p-framing-and-handshake.md).
Transaction and block propagation are not part of Stage 7.1.

## Stage 7.2: Signed transaction propagation

Run the reward-and-payment scenario:

```console
python tools/scenario.py scenarios/transaction-propagation-regtest.json --no-ui
```

Two miners race for the first reward. The winning wallet signs a payment and
submits its canonical 161-byte transaction to `full-a`. That node validates and
persists it in its mempool, acknowledges the submitting wallet, and relays it to
`full-b`. The scenario requires both full nodes to expose the same pending
transaction ID. The wire messages and result codes are specified in
[`docs/transaction-protocol-v1.md`](docs/transaction-protocol-v1.md) and accepted
by [`docs/adr/0011-transaction-propagation.md`](docs/adr/0011-transaction-propagation.md).

Run the complete fast confirmation flow through block 2:

```console
python tools/scenario.py scenarios/transaction-confirmation-regtest.json --no-ui
```

Use the development profile to watch both mining races at normal demonstration
difficulty:

```console
python tools/scenario.py scenarios/transaction-confirmation-development.json
```

## Stage 7.3: First network mining race

Run the fast automated profile:

```console
python tools/scenario.py scenarios/mining-race-regtest.json --no-ui
```

Run the deliberately slower visible development race:

```console
python tools/scenario.py scenarios/mining-race-development.json
```

Both scenarios start one full node and two wallet miners. The first valid
height-1 block is persisted and rewarded; the competing miner stops with
`stale_parent`. Network parameters and wire payloads are defined in
`docs/network-profiles.md` and `docs/mining-protocol-v1.md`. A full node now
selects up to 1,000 pending transactions when it builds a later template.

The transaction-confirmation scenarios run a second coordinated race. The
height-2 winner confirms the pending payment, receives the normal subsidy plus
its fee, and causes both full nodes to remove the transaction from their
mempools. The architectural decision is recorded in
[`docs/adr/0012-network-transaction-confirmation.md`](docs/adr/0012-network-transaction-confirmation.md).

## Visual Studio Code

Open the repository in Visual Studio Code, then:

1. Run `CMake: Select Configure Preset` and select `Windows MSVC Debug`.
2. Run `CMake: Build`.
3. Run the program from the integrated terminal:

```powershell
.\build\windows-msvc-debug\bbc.exe
```
