# BBC Glossary

This glossary defines terms as they are used by BBC. It is intended for readers
who do not already know blockchain, cryptography, networking, or C++ build
terminology.

## Blockchain and transactions

**Account**
A record identified by a BBC address. It contains the confirmed balance and the
next transaction nonce expected from that address.

**Account nonce**
A counter that orders transactions from one sender. The first confirmed
transaction uses nonce `0`, the next uses `1`, and so on. It prevents an old
signed transaction from being applied repeatedly. It is unrelated to the mining
nonce.

**Active chain**
The valid branch currently selected by a full node. Balances, nonces, mining
templates, and synchronization responses are derived from this branch.

**Address**
The public identifier used to receive BBC. The current form is `BBC_` followed
by the uppercase hexadecimal SHA-256 hash of an Ed25519 public key.

**Base unit**
The smallest amount represented by the protocol. BBC never uses floating-point
numbers for money. `100,000,000` base units equal `1 BBC`.

**Block**
A header plus an ordered list of signed transactions. The header identifies the
parent, height, transaction root, reward recipient, timestamp, difficulty
target, and mining nonce.

**Block height**
The block's distance from Genesis. Genesis has height `0`, its children have
height `1`, and so on.

**Block ID**
The SHA-256 hash of a block's canonical header. It commits to all header fields,
including the transaction root.

**Block locator**
A newest-first list of `(height, block ID)` pairs used by two nodes to find the
most recent block shared by their active chains.

**Block subsidy**
New BBC created by a valid non-Genesis block and credited to its reward
recipient. The current subsidy is constant at `50 BBC`.

**Blockchain**
The tree of valid blocks rooted at Genesis. One branch is active; other valid
branches may be retained in case they later accumulate more work.

**Canonical encoding**
The one exact byte representation accepted for a protocol object. Canonical
encoding ensures every correct implementation signs and hashes identical data.

**Chain ID**
An integer embedded in transactions and blocks that separates incompatible BBC
networks. Objects from one chain ID are rejected by another.

**Common ancestor**
The newest block shared by two branches before they diverge.

**Confirmation**
Inclusion of a transaction in an active-chain block. Later blocks increase the
amount of work that would have to be replaced to remove that transaction.

**Cumulative work**
The work attributed to a branch from Genesis to its tip. BBC currently assigns
one unit to each valid non-Genesis block because each network profile has a
fixed target.

**Fee**
An amount paid by a transaction sender in addition to the transferred amount.
The fee is credited to the reward recipient of the block that confirms the
transaction.

**Fork**
A situation where two valid blocks have the same parent, creating competing
branches. Forks can occur naturally when miners find blocks at nearly the same
time or when network groups are temporarily disconnected.

**Genesis Block**
The fixed first block at height `0`. It identifies the chain, contains no
transactions, creates no funds, and is not mined.

**Mempool**
A node's local pool of valid signed transactions that are not confirmed on its
active chain. Mempools may temporarily differ between correct nodes.

**Merkle root**
A hash that commits to every transaction ID and its position in a block. A
change to any transaction or to their order changes the root.

**Mining nonce**
A counter in the block header changed by a miner while searching for a block
hash below the target. It is unrelated to the account nonce.

**Reorganization (reorg)**
Switching the active chain from one valid branch to a strictly stronger branch.
The node detaches the old suffix, attaches the new suffix, updates account state,
and reconsiders eligible detached transactions for its mempool.

**Side branch**
A fully validated branch that is stored but is not currently active.

**Tip**
The final block of a branch. The active tip summarizes the node's current
consensus view.

**Transaction**
A signed instruction that transfers an integer amount of BBC from the address
derived from one public key to a recipient address.

**Transaction ID**
The SHA-256 hash of the complete canonical signed transaction.

**Wallet**
Software and local encrypted data that manage an Ed25519 key pair. A wallet
signs transactions; it does not decide whether a transaction or block is valid.

## Cryptography

**AEAD — Authenticated Encryption with Associated Data**
Encryption that also detects modification. BBC uses XChaCha20-Poly1305 to
encrypt private-key bytes and authenticate both ciphertext and readable wallet
metadata.

**Argon2id**
A password-based key derivation algorithm designed to make password guessing
expensive in both computation and memory.

**Digital signature**
Proof created with a private key and checked with the matching public key. It
authenticates exact bytes without revealing the private key.

**Ed25519**
The digital-signature system used for BBC wallet key pairs and transaction
signatures.

**Hash**
A fixed-size result derived from arbitrary bytes. A cryptographic hash is
designed so that changing the input unpredictably changes the result and
recovering the input from the result is impractical.

**KDF — Key Derivation Function**
An algorithm that derives a cryptographic key from a password and salt. BBC uses
Argon2id as its KDF.

**Private key**
Secret wallet material used to create signatures. It must never be logged,
transmitted, committed to Git, or passed as a command-line argument.

**Public key**
The non-secret half of a key pair. Other participants use it to verify
signatures, and BBC hashes it to derive the sender address.

**SHA-256**
The 256-bit cryptographic hash function used for addresses, transaction IDs,
block IDs, Merkle nodes, file checksums, and Proof of Work.

**XChaCha20-Poly1305**
The AEAD algorithm used to encrypt and authenticate BBC private-key material at
rest.

## Mining and consensus

**Consensus rules**
Deterministic rules every correct full node applies to transactions, blocks,
state transitions, Proof of Work, and fork choice.

**Difficulty target**
A 256-bit threshold. A non-Genesis block is valid only when its block ID,
interpreted as an unsigned big-endian integer, is strictly smaller than this
target. A smaller target makes mining harder.

**Full node**
A participant that stores blocks, independently validates protocol rules,
derives account state, maintains a mempool, and communicates valid data to peers.

**Miner**
A worker that requests a candidate from a full node and searches mining nonces.
The full node still independently validates every submitted solution.

**PoW — Proof of Work**
The requirement to demonstrate computational work by finding a valid block hash.
It makes producing a competing branch costly and supplies the ordering signal
used by BBC's fork-choice rule.

## Networking and infrastructure

**EC2 — Elastic Compute Cloud**
Amazon Web Services virtual-machine service. An EC2 instance can eventually run
a publicly reachable BBC full node.

**Handshake**
The initial exchange that verifies protocol-version overlap, chain identity,
Genesis identity, services, and peer identity before application messages are
accepted.

**LAN — Local Area Network**
A private local network, such as computers connected to one home router.

**NAT — Network Address Translation**
A router function that maps private local addresses to a public address.
Inbound Internet connections normally require port forwarding or another NAT
traversal mechanism.

**P2P — Peer to Peer**
A network model in which BBC processes communicate directly as peers instead of
trusting one central transaction or block server.

**Peer**
Another BBC process connected through the P2P protocol.

**Seed node**
A publicly reachable full node listed in initial configuration so a new node
can find its first peer. A seed is a discovery aid, not a trusted consensus
authority.

**Synchronization**
Downloading, validating, and persisting blocks needed to reach a peer's stronger
active-chain tip.

**TCP — Transmission Control Protocol**
The reliable byte-stream transport used by BBC P2P connections and the local
test-control endpoint.

**VPS — Virtual Private Server**
A rented virtual machine with administrative access, persistent storage, and
usually a public IP address.

## Software and project terms

**ADR — Architecture Decision Record**
A short document that records an important decision, its context, and its
consequences.

**API — Application Programming Interface**
A defined interface through which one software component calls another.

**CI — Continuous Integration**
Automated building and testing performed whenever code changes are published.

**CLI — Command-Line Interface**
Commands entered in a terminal, such as `bbc wallet create`.

**CMake**
The cross-platform system that configures and generates BBC builds.

**CTest**
CMake's test runner. It executes both Catch2 unit tests and registered scenario
tests.

**JSON — JavaScript Object Notation**
The text format used for local scenario and actor configuration and structured
event output. Consensus objects use canonical binary encoding instead.

**Ninja**
The build executor used by every BBC CMake preset.

**RPC — Remote Procedure Call**
A command interface that asks a running process to perform an operation. BBC's
current JSON control endpoint is loopback-only test orchestration, not a public
wallet RPC.

**SQLite**
An embedded SQL database stored in one local file. BBC uses it for rebuildable
chain indexes, account-state cache, and the local mempool.

**vcpkg**
The dependency manager that installs BBC's declared C++ libraries from the
repository manifest.
