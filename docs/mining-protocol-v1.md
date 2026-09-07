# BBC Mining Protocol Version 1

- Status: Accepted

## 1. Topology and authority

A mining worker advertises service bit 1 and makes an outbound connection to a
full node. It does not expose a listener. The full node advertises service bit 0
and is the only component in this slice that selects the active-chain tip,
constructs templates, validates submitted blocks, persists the winner, and
relays an accepted block. A wallet address in the template request determines
the block reward recipient.

An outbound-only miner retains the height and ID of each accepted block it
observes. This small validated-tip view lets it verify the parent and height of
the next template without storing the full blockchain. It does not store or
synchronize complete chain history, so a standalone miner must connect to an
up-to-date full node.

## 2. Messages

All integers are unsigned little-endian. Blocks use the canonical encoding in
`block-format.md`. These messages are legal only after HELLO completes.

| Type | Name | Payload |
| ---: | --- | --- |
| 10 | `TEMPLATE_REQUEST` | request ID (8) + reward address hash (32) |
| 11 | `BLOCK_TEMPLATE` | request ID (8) + canonical block (165..161165) |
| 12 | `BLOCK_SUBMIT` | canonical block (165..161165) |
| 13 | `BLOCK` | canonical block (165..161165) |
| 14 | `BLOCK_RESULT` | block ID (32) + accepted (1) + reason (2) |

The full node selects up to 1,000 valid transactions from its current mempool.
The height-1 mining-race scenario still produces a reward-only block because
its mempool begins empty. Every template uses the current tip as its parent,
sets timestamp to the parent timestamp plus one, starts the mining nonce at
zero, uses the selected network profile target, and pays the requested reward
address.

`BLOCK_RESULT.accepted` is exactly 0 or 1. Reason codes are:

| Code | Meaning |
| ---: | --- |
| 0 | accepted |
| 1 | stale parent or height |
| 2 | malformed block payload |
| 3 | another consensus or storage rejection |

A rejected solution does not disconnect the miner.

## 3. Cancellable work

The miner searches in batches of 10,000 nonces. Between batches it checks a
cancellation flag. It emits progress at most once every five seconds, including
the candidate height and cumulative attempt count. Finding a valid hash emits
`block_found` and `block_submitted` and sends `BLOCK_SUBMIT`.

For multi-miner scenarios, the test-control plane may defer work after the
template has been validated. The runner waits until all selected miners are
ready and releases them at one shared near-future timestamp. This barrier does
not travel over P2P, alter the candidate block, or affect consensus; it only
makes a low-difficulty regtest race exercise concurrent miners reliably.

The full node atomically appends the first valid solution, replies with
`BLOCK_RESULT`, and sends `BLOCK` to the other peers. A competing worker stops
with reason `stale_parent` when it receives the accepted block. If it found and
submitted a competing solution before processing that relay, the full node
returns reason code 1 and the same cancellation outcome. Blocks are never
merged.

## 4. Persistent mining policy

Persistent application mining and scenario mining share the same templates,
validation, messages, and cancellable worker. `bbc rpc start-mining` performs
one cycle. Continuous mode is enabled explicitly by
`bbc rpc start-continuous-mining` or `miner.auto_start`; it is disabled by
`bbc rpc stop-mining`.

In continuous mode, an accepted block causes the miner to build or request a
template for the next active-chain height. A block accepted from another miner
cancels current work with a stale-parent outcome before the next template is
started. A miner without a handshaken full-node source waits and retries without
changing chain state. Empty mempools produce valid reward-only blocks.

## 5. Scenarios

`mining-race.json` is shared by both profiles. With `--regtest` it is an
automated test using real low-difficulty PoW; without that option it uses the
normal development target as the visible demonstration. Both invocations start
one full node and two wallet miners from their profile-specific Genesis Block,
start both workers, require height 1, and require exactly one accepted miner and
one cancelled miner.

`transaction-propagation.json` continues from the same race. Its winner
submits a signed payment, both full nodes converge on one pending transaction,
and subsequent templates include that mempool selection.

`transaction-confirmation.json` runs the second race and verifies that
the height-2 winner confirms the payment. Both full nodes must converge on the
same tip and account state, expose empty mempools, and report one transaction in
the tip. CTest passes `--regtest`; running the same file without that flag
exercises the deliberately slower demonstration difficulty.
