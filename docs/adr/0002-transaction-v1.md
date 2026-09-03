# ADR 0002: Canonical Signed Transaction Version 1

- Status: Accepted
- Date: 2026-09-03

## Context

Stage 2 needs a deterministic transaction representation that can be signed,
hashed, stored in a binary file, embedded in a future block, and transmitted by
future P2P messages. Signing formatted text or JSON would introduce ambiguous
encodings and platform-dependent behavior.

The decimal precision of BBC, account state, fee policy, and mempool behavior
have not been implemented yet. The Stage 2 format must preserve those future
decisions without using floating-point values.

## Decision

BBC transaction version 1 is a fixed-width 161-byte binary value specified in
`docs/transaction-format.md`.

- The first 97 bytes are the canonical Ed25519 signature input.
- The final 64 bytes contain the detached signature.
- The sender public key is serialized and the sender address is derived from it.
- The recipient is serialized as its raw 32-byte public-key hash.
- Amount, fee, and account nonce are unsigned 64-bit little-endian integers.
- Amount and fee are denominated in currently unnamed base units.
- The format contains chain ID `1` to prevent cross-chain replay.
- The transaction ID is SHA-256 of the complete signed 161-byte value.
- A `.bbctx` file stores exactly one canonical signed transaction.
- Transaction writers do not overwrite existing files.

Timestamps and random identifiers are excluded. A transaction is identified by
its canonical signed content, and replay protection is based on the account
nonce plus future chain state.

## Consequences

- Identical fields signed by the same Ed25519 key produce identical serialized
  transactions and transaction IDs.
- Any change to a signed field invalidates the signature.
- Any change to this layout requires a new format version.
- The fixed format is simple and bounded but cannot gain optional fields without
  a new version.
- Format-level signature validity is distinct from state validity. A correctly
  signed transaction can still be rejected later for insufficient balance, an
  incorrect account nonce, fee policy, or duplication.
- `.bbctx` files are public artifacts and do not contain wallet secrets.
