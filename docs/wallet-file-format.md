# BBC Wallet File Format

## Scope

This document specifies version 1 of the local BBC wallet file. The file stores
one Ed25519 key pair. It is a local storage format, not a consensus or P2P
encoding.

All integer fields are unsigned and encoded in little-endian byte order. The
file has an exact size of 176 bytes. A decoder rejects every other size.

## Binary layout

| Offset | Size | Field | Version 1 value |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `BBCW` |
| 4 | 1 | Format version | `1` |
| 5 | 1 | KDF identifier | `1` = Argon2id 1.3 |
| 6 | 1 | Encryption identifier | `1` = XChaCha20-Poly1305-IETF |
| 7 | 1 | Reserved | `0` |
| 8 | 8 | Argon2id operations limit | `2` |
| 16 | 8 | Argon2id memory limit | `67108864` bytes |
| 24 | 16 | Salt | Random bytes |
| 40 | 24 | Nonce | Random bytes |
| 64 | 32 | Ed25519 public key | Raw public-key bytes |
| 96 | 80 | Encrypted private key | 64-byte plaintext plus 16-byte tag |

Bytes 0 through 95 form the authenticated header. They are passed to
XChaCha20-Poly1305 as associated data: they remain readable but any modification
causes authentication to fail.

## Writing

1. Reject an empty password or an existing destination path.
2. Generate a fresh 16-byte salt and 24-byte nonce with libsodium.
3. Derive a 32-byte encryption key with Argon2id 1.3, two operations, 64 MiB of
   memory, the password bytes, and the salt.
4. Construct the 96-byte header.
5. Encrypt the raw 64-byte Ed25519 private key with
   XChaCha20-Poly1305-IETF, using the header as associated data.
6. Write the header followed by the 80-byte ciphertext and authentication tag.
7. Overwrite temporary plaintext and derived-key buffers.

## Reading and validation order

1. Reject an empty password, an unreadable file, or a size other than 176 bytes.
2. Validate the magic, version, algorithm identifiers, reserved byte, and exact
   KDF parameters before performing password derivation.
3. Derive the encryption key from the supplied password and stored salt.
4. Authenticate and decrypt the private key using the stored nonce and the
   complete header as associated data.
5. Reconstruct the Ed25519 public key from the private key and require it to
   match the authenticated public-key field.
6. Overwrite temporary plaintext and derived-key buffers.

An authentication failure intentionally reports one common error for an
incorrect password and a corrupted or modified authenticated file. This avoids
claiming which condition occurred when the program cannot distinguish them.

## Security boundaries

The private key is encrypted at rest. The magic, algorithms, KDF parameters,
salt, nonce, and public key are not secret. The format does not protect against
file deletion, replacement with an older valid copy, observation of a password
entered by an insecure caller, malware, memory inspection of a running process,
or a weak password subjected to offline guessing.

## C++ API and CLI integration

`bbc::wallet::Wallet` in `include/bbc/wallet/wallet.hpp` creates a key pair,
derives its address, signs bytes, and saves encrypted wallet data. `load_wallet()`
authenticates and decrypts an existing file. `bbc::wallet::Address` in
`include/bbc/wallet/address.hpp` provides public-key derivation, strict text
parsing, and access to the underlying 32-byte hash.

The `bbc wallet` CLI commands use these interfaces. Commands that need the
private key read a password interactively with terminal echo disabled. The
selected-wallet feature stores only a path to a wallet file, never its password
or private key.
