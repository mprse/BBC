# BBC Linear Chain and Account State

## Scope

Stage 5 defines deterministic validation for a single linear BBC chain and its
derived account state. A node starts from the canonical Genesis Block and
replays later blocks in height order. Persistent block storage, competing
branches, cumulative-work fork choice, and reorganizations are intentionally
deferred.

## Monetary units and block reward

All balances and transaction values use unsigned 64-bit integers.

```text
1 BBC = 100,000,000 base units
block subsidy = 50 BBC = 5,000,000,000 base units
```

Genesis creates no funds. Every accepted non-Genesis block creates exactly one
block subsidy for the address in its `reward_recipient` header field. The
subsidy is constant in Version 1; there is no halving schedule or maximum supply
yet.

Transaction fees do not create funds. Each fee is deducted from its sender and
the sum of all block fees is credited to the reward recipient together with the
subsidy. The minimum fee is zero.

## Account model

The state maps each 32-byte BBC address hash to:

```text
balance: unsigned 64-bit base-unit balance
next_nonce: unsigned 64-bit nonce required by the next transaction
```

An address absent from the map has balance `0` and next nonce `0`. Therefore,
the first accepted transaction from an address uses nonce `0`, the second uses
nonce `1`, and so on.

## Extending the chain

A new `Blockchain` contains canonical Genesis at height `0` and an empty account
state. A non-Genesis block extends the current tip only when all these checks
pass:

1. Its height is exactly the parent height plus one.
2. Its `previous_block_hash` equals the current tip block ID.
3. Its timestamp is strictly greater than the parent timestamp.
4. Its target and Proof of Work satisfy the Stage 4 rules.
5. Every transaction can be applied in its encoded block order.
6. The subsidy and collected fees can be credited without integer overflow.

Canonical decoding already checks the block and transaction formats,
signatures, transaction root, chain ID, duplicate transaction IDs, and size
limits before this chain-level validation runs. There is no wall-clock check in
Stage 5 because consensus replay must not depend on the local clock.

## Transaction state transition

For each transaction, in block order:

1. Derive the sender address from the signed public key.
2. Require `transaction.account_nonce == sender.next_nonce`.
3. Require `sender.balance >= amount + fee`.
4. Subtract `amount + fee` from the sender.
5. Increment the sender's next nonce.
6. Add `amount` to the recipient.
7. Add `fee` to the block fee total.

After every transaction succeeds, credit `block subsidy + total fees` to the
block reward recipient. This ordering means a miner cannot spend the reward
from the block that creates it; the reward becomes available to later blocks.

Every state transition is atomic. An invalid transaction, nonce, balance, or
overflow rejects the complete block and leaves both the chain tip and account
state unchanged.

## Offline CLI replay

Stage 5 exposes deterministic replay without defining durable node storage:

```text
bbc chain verify [--block <path>]...
bbc chain tip [--block <path>]...
bbc chain balance --address <BBC-address> [--block <path>]...
bbc wallet balance [--file <wallet-path>] [--block <path>]...
```

Each `--block` names one canonical `.bbcblock` file. Files must be supplied in
ascending height order, starting with block 1; Genesis is built into the
program and must not be supplied. Replaying the same ordered files always
produces the same tip and account state. `wallet balance` opens the selected or
explicit wallet only to derive its address; private key material is never used
to calculate the chain state.
