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

The build uses the pinned vcpkg manifest to install libsodium and Catch2. If
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

`amount` and `fee` are unsigned integers expressed in BBC base units. The
number of decimal places represented by one BBC has not been selected yet.
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

Create a reward-only first block with the maximum Stage 3 target:

```powershell
.\build\windows-msvc-debug\bbc.exe block create `
    --height 1 `
    --previous 10639A07612F06E14052E10B01E76E961D2BFE3458FAF7836D968C8DDC8683E2 `
    --reward-to BBC_4400000000000000000000000000000000000000000000000000000000000000 `
    --timestamp 1788393600 `
    --out block-1.bbcblock
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
    --out block-1.bbcblock
```

The optional `--target` is a 64-character hexadecimal 256-bit value and defaults
to all `FF` bytes. The optional `--nonce` defaults to zero. Stage 3 serializes
these fields but does not yet validate Proof of Work.

Inspect or perform format-level verification of a block file:

```powershell
.\build\windows-msvc-debug\bbc.exe block show --file block-1.bbcblock
.\build\windows-msvc-debug\bbc.exe block verify --file block-1.bbcblock
```

Block v1 has a 165-byte header and between zero and 1000 canonical Transaction
v1 values. The exact encoding and Genesis constants are specified in
[`docs/block-format.md`](docs/block-format.md). The decision is recorded in
[`docs/adr/0003-block-v1-and-genesis.md`](docs/adr/0003-block-v1-and-genesis.md).

## Visual Studio Code

Open the repository in Visual Studio Code, then:

1. Run `CMake: Select Configure Preset` and select `Windows MSVC Debug`.
2. Run `CMake: Build`.
3. Run the program from the integrated terminal:

```powershell
.\build\windows-msvc-debug\bbc.exe
```
