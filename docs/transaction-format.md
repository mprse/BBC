# BBC Transaction Format Version 1

## Scope

This document specifies the canonical binary encoding of a signed BBC
transaction. The same encoding is used in `.bbctx` files and will be reused in
blocks and P2P messages. Version 1 transactions have an exact size of 161 bytes.

All integer fields are unsigned and encoded in little-endian byte order.

## Binary layout

| Offset | Size | Field | Version 1 value |
|---:|---:|---|---|
| 0 | 4 | Magic | ASCII `BBTX` |
| 4 | 1 | Format version | `1` |
| 5 | 4 | Chain ID | `1` |
| 9 | 32 | Sender public key | Raw Ed25519 public-key bytes |
| 41 | 32 | Recipient | Raw SHA-256 public-key hash |
| 73 | 8 | Amount | BBC base units |
| 81 | 8 | Fee | BBC base units |
| 89 | 8 | Account nonce | Sender's transaction sequence value |
| 97 | 64 | Signature | Ed25519 detached signature |

The human-readable recipient address is reconstructed as `BBC_` followed by
the uppercase hexadecimal encoding of the 32-byte recipient field. The sender
address is not serialized; it is derived from the serialized public key using
the same address rule.

## Signature

The signature input is exactly bytes 0 through 96. The `BBTX` magic, version,
and chain ID provide domain and network separation. The signature is generated
by the private key corresponding to the sender public key at offset 9.

The transaction ID is:

```text
SHA-256(bytes 0 through 160)
```

The signature is therefore included in the transaction ID but not in its own
signature input.

## Validation order

A version 1 decoder performs these checks without mutating account state:

1. Require exactly 161 bytes.
2. Require the `BBTX` magic.
3. Require format version `1`.
4. Require chain ID `1`.
5. Require `amount > 0`.
6. Require `amount + fee` to fit in an unsigned 64-bit integer.
7. Verify the Ed25519 signature over bytes 0 through 96 with the supplied
   sender public key.

Balance, the expected account nonce, fee policy, and duplicate transaction
checks require blockchain state and are performed by the chain-state and
mempool components. Passing this format-level validation does not by itself
mean a transaction can be applied to the current chain state.

## File behavior

A `.bbctx` file contains exactly one signed transaction. Writers refuse to
overwrite an existing path. Readers treat the complete file as untrusted and
reject incorrect lengths, unsupported protocol fields, invalid numeric values,
and invalid signatures.

Transaction files contain public information only: addresses, amounts, nonce,
public key, signature, and transaction ID. They never contain wallet passwords
or private keys.

## C++ API and integration

`bbc::transaction` in `include/bbc/transaction/transaction.hpp` exposes
`TransactionFields`, `sign_transaction()`, and the immutable
`SignedTransaction` value. `serialize()` and `deserialize_transaction()` use the
canonical bytes described above; `save_transaction()` and `load_transaction()`
apply the same rules to `.bbctx` files. `TransactionResult` carries either a
valid value or a specific `TransactionError` without changing chain state.

The same `SignedTransaction` type is accepted by block construction, mempool
admission, chain-state execution, CLI files, and P2P transaction messages.
