# BBC Transaction Propagation Protocol Version 1

- Status: Accepted

## 1. Purpose and roles

This protocol transports an already signed BBC transaction from a networked
wallet to a full node and relays accepted transactions between full nodes. The
control plane may ask a wallet to create a payment, but it does not carry the
transaction between nodes and cannot bypass normal signature, chain-state, or
mempool validation.

A submitting actor selects a handshaken peer advertising the full-node service.
The full node is authoritative for acceptance into its own mempool. Other full
nodes independently validate every relay; acceptance by one node is not proof
that another node must accept it.

## 2. Messages

All messages use the frame defined in `p2p-protocol-v1.md` and are legal only
after `HELLO` completes.

| Type | Name | Payload |
| ---: | --- | --- |
| 20 | `TRANSACTION_SUBMIT` | canonical signed transaction (161 bytes) |
| 21 | `TRANSACTION` | canonical signed transaction (161 bytes) |
| 22 | `TRANSACTION_RESULT` | transaction ID (32) + accepted (1) + reason (2) |

The signed transaction is encoded exactly as specified in
`transaction-format.md`. The result reason is an unsigned little-endian 16-bit
integer. `accepted` is exactly zero or one.

Result codes are:

| Code | Meaning |
| ---: | --- |
| 0 | accepted |
| 1 | malformed canonical transaction |
| 2 | transaction belongs to another network profile |
| 3 | duplicate transaction ID |
| 4 | another state, mempool-policy, or local-storage rejection |

An accepted result must use code 0. A rejected result must use a nonzero code.
Malformed input may not have a trustworthy transaction ID, so its result uses
32 zero bytes. A result that does not match the wallet's pending transaction is
ignored. The wallet also requires the result to come from the exact full-node
peer selected for that submission.

## 3. Submission, validation, and relay

The wallet signs the exact recipient, amount, fee, account nonce, and active
chain ID, records the transaction ID as pending, then sends
`TRANSACTION_SUBMIT`. The receiving full node performs these operations in
order:

1. Decode the fixed canonical representation and verify its Ed25519 signature.
2. Require the transaction chain ID to match the local network profile.
3. Validate it against current confirmed account state and mempool policy.
4. Persist it in the local SQLite mempool store.
5. Return `TRANSACTION_RESULT` to the submitting peer.
6. On acceptance, broadcast `TRANSACTION` to every other handshaken peer.

Each receiving full node repeats steps 1 through 4. It broadcasts only a newly
accepted transaction and excludes the connection from which it arrived. A
relay encountered again through a network cycle is rejected as a duplicate and
is not broadcast further. This makes relay loop-free without trusting the
sender's decision.

The mempool is local cache, not consensus history. A block accepted afterward
causes the store to revalidate pending transactions against the new account
state. Newly requested block templates select at most 1,000 transactions using
the policy in `mempool.md`.

## 4. Observable state

The submitting wallet exposes transaction state `idle`, `submitted`,
`accepted`, or `rejected` plus the pending transaction ID. Full-node status
exposes mempool size, and normalized dumps also expose ordered transaction IDs.
Structured events record submission, local acceptance or rejection, and relay;
they never contain a private key or wallet password.

The current protocol permits one correlated in-flight transaction per wallet
actor and requires an explicit account nonce. It does not implement fee-based peer
admission, retry after disconnect, transaction inventory announcements,
automatic nonce lookup, or a public wallet RPC.

## 5. Required verification

Unit tests enforce the fixed frame payload sizes. The multi-process regtest
scenario must:

- start two full nodes, two wallet miners, and one receiving wallet;
- mine and propagate the height-1 reward block;
- select the winning miner as the funded sender;
- sign and submit a payment with nonce zero;
- observe an accepted result at the sender;
- observe the same single transaction ID in both full-node mempools.

The confirmation scenario then performs a second mining race and must verify:

- the height-2 template contains the pending transaction;
- both full nodes accept and persist the same winning block;
- both persistent mempools remove the confirmed transaction;
- both full nodes derive identical balances and nonces;
- the height-2 miner receives the transaction fee in addition to its subsidy.
