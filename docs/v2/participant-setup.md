# Creating Wallets, Full Nodes, and Miners

This is a **proposed v2 workflow**. Its commands are illustrative pseudocode;
the current executable and configuration format are documented in
[application-config.md](../application-config.md). A participant can combine
roles on one computer, but the roles have different trust and storage needs.

| Installation | Keeps private key? | Validates whole chain? | Mines? | Must accept inbound P2P? |
| --- | --- | --- | --- | --- |
| Wallet only | Yes, encrypted | No | No | No |
| Wallet + full node | Yes, encrypted | Yes | No | Not if it makes outbound peer connections |
| Wallet + miner | Yes, encrypted | No; uses a full-node source | Yes | No |
| Wallet + full node + miner | Yes, encrypted | Yes | Yes | Not necessarily; public seed does |

The wallet role is available by default to the human operator. A long-running
node or miner never needs to decrypt the wallet merely to receive rewards. The
reward address is public; signing a payment happens in a separate short-lived
wallet command.

## Common first steps

1. Install a build compatible with the intended network and obtain its
   published manifest through a trusted channel. Check its fingerprint.
2. Create an encrypted wallet and select it locally. Back up the wallet file
   offline; losing its private key loses control of the funds. Do not upload
   it to a seed node or share its password.
3. Join the network with an outbound public full-node contact. This records
   public settings, not the wallet password. A wallet-only participant does
   not need to initialize or run a node.

```text
bbc network join --file <network-manifest> --peer <public-full-node-ip>:7333
bbc wallet create --name personal --select
bbc wallet address
```

If the same person runs multiple machines, they may give each installation a
distinct node profile and data directory. One wallet file should not be
concurrently modified from unrelated hosts without a deliberately designed
wallet synchronization mechanism.

## Wallet only

Add one or more full-node contacts to the local profile. The wallet initiates
outbound connections, so no router port forwarding is needed. A remote
full-node response can help display balance and submit payments, but without
independent chain verification it is only a *remote report*. The CLI must say
which tip and source it used and warn about disagreement or stale peers.

```text
bbc network peer add <another-full-node-ip>:7333
bbc wallet balance
bbc send --to BBC_<recipient> --amount 1.25
```

No background BBC service is required just to store the encrypted wallet.
The current code does not yet implement the proposed simple balance and send
commands or a verified light-wallet protocol.

## Wallet with a full node

Enable full-node validation and select an address assigned to this computer.
The node downloads and independently checks the chain from Genesis, keeps
blocks and a rebuildable account-state cache, and relays valid transactions and
blocks. On a laptop behind NAT it can dial a public peer even if other peers
cannot initiate connections to the laptop.

```text
bbc node init --name home --network <network-manifest> --wallet personal
bbc node enable full-node --listen <local-ip>:7333 --peer <public-ip>:7333
bbc node run
```

Keep `node run` in its own terminal (or an operating-system service). Use
`bbc wallet balance` and `bbc node status` from a second terminal. Only a
synced local full node can provide the proposed wallet command with a balance
derived from locally verified blocks. If sync is incomplete, display that
clearly. The P2P listener may require a host firewall rule for inbound peers;
an outbound-only connection to the public peer does not require a home-router
port-forwarding rule.

## Wallet with a miner

A miner requires a reward address and an up-to-date full-node source. It may
use the local full node or a remote one. The remote source supplies a candidate
containing valid transactions selected from its mempool. A mining-only process
must not assume the source is trustworthy; it must check the template fields
it can validate and report that it does not hold a fully validated chain.

```text
bbc node init --name home --network <network-manifest> --wallet personal
bbc node enable miner --reward-to <my-BBC-address> --source <full-node-ip>:7333
bbc node run
bbc miner start
bbc miner stop
```

When combined with a local full node, omit `--source`: the miner builds from
its own validated tip and mempool. Under the proposed v2 consensus rule,
miners wait when no ordinary transaction is eligible for the next block. If
another block wins, they cancel stale work and build a new candidate from the
updated tip and pending transactions. The special height-1 founder block is
the only transaction-free mining job.

## Public full node, optionally with a miner

A seed/full node requires stable storage, a reachable P2P address, service
monitoring, and a backup policy. On EC2, bind P2P to the instance's private
IPv4 address and let the public address or Elastic IP reach it through the
cloud mapping. Open only the P2P port to other participants. Restrict SSH;
keep local administration bound to loopback. See the current
[EC2 deployment guide](../ec2-deployment.md) for platform-specific packaging
and systemd details.

The seed can mine to a public address whose private key remains on a different
computer. It is a bootstrap contact and relay, not a central validator or a
special authority. Other full nodes still reject blocks that violate their
own consensus rules.

## Before accepting payments

- Verify the manifest fingerprint and network identity.
- Verify the node is synchronized to a known tip and has at least one healthy
  peer; compare multiple independent peers where available.
- Distinguish a mempool acceptance from a block confirmation.
- Remember that a fork may replace the selected tip and remove or requeue a
  recently confirmed transaction.
- Do not present remote-reported wallet balances as locally verified.

See the [transaction journey](transaction-lifecycle.md) for the complete path.
