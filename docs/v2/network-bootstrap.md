# Starting a BBC v2 Network

This is a **proposed procedure**, not an instruction for the current
`development` chain. A BBC network is a group of nodes enforcing exactly the
same validation rules and starting from exactly the same Genesis Block. A seed
node helps participants find peers; it does not own the ledger or get to waive
validation rules.

## 1. Freeze a network identity before inviting participants

A canonical, public network manifest must fix at least:

- a unique chain ID and P2P network marker;
- canonical Genesis bytes and their hash;
- the exact founder address and special height-1 allocation; 1,000 BBC
  (100,000,000,000 base units) is an illustrative amount still to confirm;
- the rule that height 1 requires Proof of Work but no ordinary transaction,
  and that heights 2 and above require at least one valid ordinary transaction;
- the ordinary miner-subsidy function, fee rules, block limits, target block
  interval, and deterministic difficulty-adjustment algorithm;
- the canonical transaction, address, block, and signature versions;
- a human-readable network name.

The manifest must have an unambiguous canonical encoding and a displayed
fingerprint. Each node must compare that fingerprint and Genesis hash during
startup and peer handshake. Initial public peer endpoints belong in a separate
bootstrap list that can change without changing the consensus fingerprint.
Consensus parameters cannot change this way. An address alone must not be
treated as proof that a peer belongs to the intended network.

The first four bullets describe the agreed *direction*. The remaining exact
values and encodings are open design decisions, listed in the
[v2 index](README.md). The proposed `bbc network create --definition
<complete-definition.json> --out <manifest.json>` command must refuse to
produce an incomplete manifest. No arbitrary per-node reward override may
change the founder allocation.

## 2. Prepare a test network first

1. Implement the v2 rules and deterministic tests, including rejection of
   empty blocks at height 2 or later and rejection of an incorrect height-1
   reward recipient or amount.
2. Run local multi-process scenarios on a loopback-only test network with its
   own chain ID, marker, Genesis, and low bounded Proof-of-Work target.
3. Test two miners racing on the same payment, a conflicting payment, a fork,
   a reorganization, node restart, and catch-up synchronization.
4. Verify emission totals and every participant's derived account state after
   each accepted block.

Never connect the scenario control endpoint to a public interface. Distinct
network identities prevent ordinary test blocks and transactions from being
accepted on the public experiment. They do not prevent a malicious participant
from writing its own client and submitting *valid* transactions to that
experiment.

## 3. Publish one experimental network

1. Publish the frozen manifest, software version, Genesis fingerprint, reward
   schedule, and limitations to every participant before launch.
2. Start at least one publicly reachable full node, for example on EC2. Bind
   BBC P2P to the instance's assigned private IPv4 address; advertise its
   stable public IPv4 address as an initial peer. Allow only the P2P TCP port
   through the cloud firewall. Keep local administration on `127.0.0.1` and
   never expose its token or port to the Internet.
3. Start a miner connected to that full node. The first valid mined block at
   height 1 pays exactly the amount frozen in the manifest to its founder
   address. The founder's private key should remain in the encrypted wallet on
   the owner's computer, not on the public seed merely to receive this reward.
4. Wait for every participating full node to independently accept and
   synchronize the same height-1 block. Compare chain IDs, Genesis hashes,
   tips, and balances before making the first payment.
5. The founder sends a normal signed payment. From height 2 onward, miners
   obtain transaction-bearing templates from their full-node source; a node
   rejects a reward-only candidate even if its Proof of Work is valid.
6. Add independent public peers when possible. Nodes behind NAT can initiate
   outbound connections to them; one seed should not remain the only contact.

The first block is a visible founder allocation, not a hidden ability to mint
funds later. If someone other than the founder finds its Proof of Work, the
reward still goes to the fixed founder address. Ordinary later subsidies go to
the winning block's validated miner reward address.

## 4. Restarting an experiment from zero

A blockchain cannot be reset for everyone by deleting one EC2 directory. Each
participant may retain its previous history, and old consensus rules may
accept blocks rejected by the new rules. A planned restart therefore creates
a *new* network identity and must be announced to all participants.

1. Stop mining and node services; record the old manifest fingerprint and tip.
2. Back up old node data and encrypted wallet files. Do not delete them as part
   of routine setup, and never put wallets or credentials in Git.
3. Freeze the new manifest with a new chain ID, P2P marker, Genesis hash, and
   separate node-data paths. A wallet key may be reused only if the new address
   and signature rules explicitly permit it; its old balance never carries
   over automatically.
4. Update every full node and miner to compatible software and the new
   manifest. Start from the new Genesis, not from copied old block files or a
   reused SQLite cache.
5. Mine and verify the new special first block, then run a small payment and
   synchronization check before inviting more users.

Once other people depend on a network, a restart is a coordinated new-network
launch, not a transparent upgrade. The existing EC2/laptop `development` chain
remains an experiment until this boundary is explicitly planned.

## Operational limits

This procedure does not make BBC production-ready. The present system has a
fixed difficulty target, no verified wallet-only balance protocol, and static
peer configuration. Difficulty adjustment, multi-host testing, recovery,
resource-abuse limits, operational monitoring, and software-update rules need
review before claims of durable public-network security.
