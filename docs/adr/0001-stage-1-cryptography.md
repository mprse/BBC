# ADR 0001: Stage 1 Cryptography and Wallet Protection

- Status: Accepted
- Date: 2026-09-03

## Context

Stage 1 requires portable key generation, address derivation, signing,
verification, and password-protected wallet storage. The project must not
implement cryptographic primitives itself. A wallet file must not reveal its
private key and must reject both an incorrect password and any undetected file
modification.

## Decision

BBC uses libsodium for all Stage 1 cryptographic operations:

- Ed25519 generates key pairs and creates detached signatures.
- SHA-256 hashes the 32-byte public key for the current educational address.
- Argon2id derives a 256-bit wallet-encryption key from a password and a fresh
  random 128-bit salt. Version 1 uses two operations and 64 MiB of memory.
- XChaCha20-Poly1305 encrypts the 64-byte Ed25519 private key with a fresh random
  192-bit nonce and authenticates the complete wallet header as associated data.
- Random values come from libsodium's operating-system-backed random generator.
- Secret temporary buffers and in-memory private-key storage are overwritten
  with `sodium_memzero` when their lifetime ends.

The current address is the ASCII prefix `BBC_` followed by the uppercase
hexadecimal SHA-256 digest of the raw public key. This format is accepted for
the educational Stage 1 implementation but is not declared stable for a public
network. It currently has no version field or human-entry checksum.

The wallet password is never stored. A wallet file stores the salt, nonce, KDF
parameters, public key, and authenticated ciphertext. Existing wallet files are
not overwritten by the save operation.

## Why XChaCha20-Poly1305

Encryption alone would hide the private key but would not reliably detect a
modified file. XChaCha20-Poly1305 is an authenticated-encryption construction:
it provides confidentiality and an authentication tag in one reviewed
primitive. Decryption succeeds only when the password-derived key, nonce,
header, and ciphertext are all exactly correct.

Its 192-bit nonce also makes generating a fresh nonce randomly practical, with
a negligible collision risk. This keeps the file-writing API simple and avoids
maintaining a persistent nonce counter. Argon2id solves a different problem: it
makes password guessing expensive. Both layers are required.

## Consequences

- Anyone who loses both the wallet file and its private-key backup loses access
  to the identity represented by that key.
- A weak password remains susceptible to offline guessing despite Argon2id;
  users still need a strong password.
- Changing algorithms, parameters, or field encoding requires a new wallet-file
  version and migration logic.
- File authentication detects corruption or tampering but does not prevent
  deletion, rollback to an older valid wallet file, or compromise of a running
  process.
- libsodium and Catch2 are pinned through the vcpkg baseline to make Stage 1
  builds reproducible.
