# Repository Working Agreement

This file applies to the entire repository. It defines how human contributors and
coding agents should work on BBC.

## 1. Project purpose

BBC (Bi-Bi-Coin) is an educational C++ project for learning how a blockchain,
wallet, proof-of-work consensus, and peer-to-peer network operate. It is not a
production cryptocurrency and must not be presented as suitable for storing real
value.

The first milestone is deliberately offline and deterministic:

1. Create wallets.
2. Create and sign a transaction.
3. Validate the signature and transaction rules.
4. Add the transaction to a block.
5. Find and validate a simple proof of work.
6. Apply the block to account state.

Do not introduce P2P networking until the offline transaction, block, consensus,
and state logic is covered by deterministic tests.

## 2. Communication and language

- Communicate with the user in Polish.
- Write source code, identifiers, comments, commit messages, documentation, logs,
  and user-facing program output in English.
- Keep explanations concise, but record protocol decisions precisely.
- `BBC_PROJECT.md` is the initial concept document and is currently written in
  Polish. New documentation must be written in English. Translate or replace the
  concept document only as a deliberate task; do not mix languages inside it.

## 3. Names and sources of truth

- The repository, CMake project, protocol, and currency are named `BBC` /
  Bi-Bi-Coin.
- The current executable target is `bbc`.
- Use `BBC` for protocol identifiers, ticker symbols, addresses, and examples.
  Use lowercase `bbc` for executable names, commands, paths, and network service
  names where lowercase is conventional.
- `BBC_PROJECT.md` describes the intended behavior at a conceptual level.
- Tested code and explicit protocol specifications are the source of truth for
  implemented behavior. If they conflict with the concept document, stop and
  surface the discrepancy before changing consensus behavior.

## 4. Repository and Git policy

- Repository remote: `git@github.com:mprse/BBC.git`.
- Default branch: `master`.
- Inspect `git status` before editing and preserve unrelated user changes.
- Keep commits focused and use short, imperative English commit messages.
- Do not commit build outputs, editor caches, generated dependency trees, wallet
  files, private keys, seeds, credentials, or other secrets.
- Do not rewrite published history or force-push.
- Use the `codex/` prefix for agent-created branches unless the user requests a
  different name.
- A push to `origin` is allowed only when the user asks to publish, commit and
  push, or otherwise clearly requests a remote update. Never push partial or
  failing work.

## 5. Supported toolchain

- Language standard: C++20 with compiler extensions disabled.
- Build system: CMake 3.25 or newer.
- Build executor: Ninja.
- Windows compiler: MSVC x64 from Visual Studio Build Tools.
- Linux compiler: GCC.
- macOS compiler: Clang.
- Cross-platform developer entry point: Python 3.9 or newer.
- Dependency manager: vcpkg in manifest mode when third-party dependencies are
  introduced.
- Editor: Visual Studio Code with Microsoft C/C++ and CMake Tools.
- Test integration: CTest with Catch2.
- Cryptography: libsodium provides Ed25519 and related
  primitives. Never implement cryptographic primitives in this repository.

`CMakePresets.json` is the shared source of truth for configure and build
settings. Use `CMakeUserPresets.json` only for uncommitted, machine-local
overrides.

Do not hard-code developer-specific absolute paths into committed build files.
Windows tool discovery belongs in `tools/build.py`. Do not require users to
weaken PowerShell execution policy to build the project.

## 6. Standard commands

Run commands from the repository root.

```console
python tools/build.py configure
python tools/build.py build
python tools/build.py test
python tools/build.py run
python tools/build.py shell
```

The default action is `build`:

```console
python tools/build.py
```

Platform presets are:

- `windows-msvc-debug`
- `linux-gcc-debug`
- `macos-clang-debug`

The helper must remain usable from ordinary PowerShell, `cmd`, Bash, and CI. On
Windows it discovers Visual Studio with `vswhere` and passes the prepared MSVC
environment only to child processes. It must not make persistent machine-level
environment changes.

## 7. Project structure

The current structure is intentionally small:

```text
.
|-- CMakeLists.txt
|-- CMakePresets.json
|-- vcpkg.json
|-- docs/
|   `-- adr/
|-- include/bbc/
|-- src/
|   |-- app/
|   |-- chain/
|   |-- consensus/
|   |-- core/
|   |-- crypto/
|   |-- storage/
|   |-- transaction/
|   |-- wallet/
|   `-- main.cpp
|-- tests/
|-- tools/
|   `-- build.py
`-- .vscode/
```

As implementation grows, organize code by responsibility rather than by
technical convenience. Expected areas include `crypto`, `wallet`, `chain`,
`mempool`, `consensus`, `network`, `storage`, and `rpc`. Keep consensus and
validation logic independent from CLI, storage, HTTP, and transport code.

Production code must not depend on test code. Prefer small libraries with clear
interfaces over a single large executable target.

## 8. C++ engineering rules

- Use portable C++20 and the standard library where practical.
- Keep compiler warnings enabled. New code must compile cleanly with `/W4` on
  MSVC and `-Wall -Wextra -Wpedantic` on GCC and Clang.
- Prefer RAII, value semantics, scoped enums, and explicit ownership.
- Use smart pointers only when dynamic ownership is required; do not use owning
  raw pointers.
- Use fixed-width integer types for protocol fields and serialized values.
- Never represent currency amounts, fees, balances, targets, or cumulative work
  with floating-point types.
- Check integer overflow, size limits, and conversions at trust boundaries.
- Keep functions focused and make invalid states difficult to represent.
- Do not use exceptions for ordinary protocol validation failures. Return an
  explicit result that preserves a useful validation reason.
- Avoid global mutable state and hidden nondeterminism.
- Keep platform-specific code behind narrow interfaces.
- Add comments for intent, invariants, and non-obvious trade-offs, not for syntax.

## 9. Protocol and security rules

- Consensus behavior must be deterministic across platforms and compilers.
- Define canonical binary serialization before signing or hashing structures.
  Never sign or hash informally formatted JSON or locale-dependent text.
- Domain-separate hashes and signatures where different protocol objects could
  otherwise share an encoding.
- A transaction nonce and a mining nonce are distinct concepts and must use
  unambiguous names in code.
- Derive the sender address from the supplied public key and validate that
  relationship before signature verification.
- Treat all network, wallet, storage, and serialized input as untrusted.
- Validate lengths before allocation and validate all fields before mutating
  state.
- Never log, transmit, commit, or expose a private key, recovery seed, wallet
  password, or decrypted wallet material.
- Use operating-system cryptographic randomness through a reviewed library.
- Do not invent custom cryptography, key derivation, encryption, address
  checksums, or signature schemes.
- Any change to serialization, block validity, transaction validity, fork choice,
  rewards, difficulty, or genesis data is a protocol decision. Document it and
  request confirmation when the decision has not already been made.

## 10. Dependencies

- Prefer the standard library for small, well-defined tasks.
- Add third-party C++ dependencies through `vcpkg.json` in manifest mode.
- Pin a vcpkg baseline for reproducible builds when the first dependency is
  added.
- Do not vendor dependency source trees without an explicit reason and approval.
- Keep the dependency surface small, especially in consensus-critical code.
- Verify library licenses are compatible with the repository license.
- The current dependencies are Asio, nlohmann/json, libsodium, SQLite, and
  Catch2; they are not
  authorized substitutes for protocol design or validation tests.

## 11. Testing and verification

- Every behavior change requires proportionate automated tests.
- Prefer deterministic unit tests. Do not depend on wall-clock timing, public
  networks, or random success conditions.
- Make cryptographic and serialization tests use fixed vectors in addition to
  generated cases.
- Cover both successful behavior and rejection paths.
- Add regression tests before or with bug fixes.
- Integrate tests with CTest so they run consistently from CMake and CI.
- Keep proof-of-work difficulty tiny and bounded in tests.
- Test state changes atomically: rejected transactions and blocks must leave
  state unchanged.
- Once tests exist, a code change is not complete until the relevant build and
  test presets pass.

For the current Stage 7.0 milestone, the minimum verification commands are:

```console
python tools/build.py test
python tools/build.py run
```

They must finish successfully and run the current application without errors.

Also run `git diff --check` before handing off changes.

## 12. Documentation policy

- Keep `README.md` focused on setup, build, test, and basic usage.
- Put protocol specifications and architectural explanations under `docs/` as
  they become implementation-ready.
- Record significant architectural or protocol choices as short English ADRs
  under `docs/adr/`.
- Update documentation in the same change as user-visible commands, file layout,
  protocol behavior, or prerequisites.
- Examples must be executable or clearly marked as conceptual pseudocode.
- Use exact units, byte order, size limits, and validation order in protocol
  documents; avoid ambiguous wording such as "for example" for consensus rules.

## 13. Working method for coding agents

1. Read this file, `README.md`, and the relevant design documentation.
2. Inspect the working tree before modifying files.
3. Identify whether the task changes ordinary implementation or protocol rules.
4. Make the smallest coherent change that advances the current milestone.
5. Add or update tests and documentation with the implementation.
6. Run the relevant build and tests, then run `git diff --check`.
7. Review the final diff for secrets, generated files, accidental rewrites, and
   unrelated changes.
8. Report what changed, what was verified, and any unresolved decision in Polish.

Do not silently expand task scope, make unresolved product decisions, or replace
working cross-platform infrastructure with an OS-specific shortcut. Prefer a
small working vertical slice over speculative abstractions for future phases.

## 14. Definition of done

A change is complete when:

- it satisfies the requested behavior,
- code and documentation are in English,
- supported builds remain reproducible through the shared presets,
- relevant automated tests pass,
- no warnings, secrets, generated files, or unrelated edits are introduced,
- protocol-impacting decisions are documented and confirmed,
- and the final handoff clearly states verification results and remaining work.
