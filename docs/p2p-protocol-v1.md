# BBC P2P Protocol Version 1

- Status: Accepted
- Scope: Stage 7.1 through Stage 7.3

## 1. Purpose

This document defines the first binary peer-to-peer transport used between BBC
full nodes. It covers TCP framing, connection establishment, version and network
checks, liveness messages, resource limits, and disconnect behavior.
Transaction propagation is specified in `transaction-protocol-v1.md`, and
Stage 7.3 mining payloads are specified in `mining-protocol-v1.md`.
Initial chain synchronization remains outside the implemented scope.

The protocol is public and unauthenticated. A valid frame checksum detects
accidental corruption but does not prove who sent a message. Every transaction,
block, and other consensus object added in later stages must still pass its
normal cryptographic and consensus validation.

## 2. Transport

- Transport is TCP.
- Each connection carries frames in both directions.
- Integers use unsigned little-endian encoding.
- TCP is treated as a byte stream. A receiver must support a fragmented header,
  a fragmented payload, and multiple frames in one socket read.
- A peer must not allocate payload storage until the complete header has passed
  its magic, version, message-type, and payload-length checks.

## 3. Frame format

Every frame begins with this fixed 44-byte header:

| Offset | Size | Field | Version 1 value or meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | network magic | Profile-specific; see below |
| 4 | 2 | wire version | `1` |
| 6 | 2 | message type | See Section 4 |
| 8 | 4 | payload length | Number of bytes following the header |
| 12 | 32 | payload checksum | SHA-256 of the exact payload bytes |

The maximum payload length is 1,048,576 bytes. A zero-length payload uses the
SHA-256 digest of the empty byte sequence. The checksum is transmitted in the
same raw 32-byte order returned by the BBC SHA-256 primitive; it is not a hex
string.

The development network uses magic `42 42 43 01`; regtest uses
`42 42 43 02`. Their other parameters are fixed in `network-profiles.md`. A receiver closes
the connection when the magic or wire version is not supported; version 1 does
not attempt to reinterpret such a frame.

## 4. Message types

| Value | Name | Payload size | Allowed before handshake |
| ---: | --- | ---: | --- |
| 1 | `HELLO` | 104 bytes | Yes, and it must be first |
| 2 | `PING` | 8 bytes | No |
| 3 | `PONG` | 8 bytes | No |
| 10 | `TEMPLATE_REQUEST` | 40 bytes | No |
| 11 | `BLOCK_TEMPLATE` | 173..161173 bytes | No |
| 12 | `BLOCK_SUBMIT` | 165..161165 bytes | No |
| 13 | `BLOCK` | 165..161165 bytes | No |
| 14 | `BLOCK_RESULT` | 35 bytes | No |
| 20 | `TRANSACTION_SUBMIT` | 161 bytes | No |
| 21 | `TRANSACTION` | 161 bytes | No |
| 22 | `TRANSACTION_RESULT` | 35 bytes | No |

Message type zero is invalid. Unknown message types cause a disconnect in wire
version 1. A known message with the wrong payload length also causes a
disconnect.

## 5. HELLO payload

Each side sends exactly one `HELLO` immediately after the TCP connection is
established.

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 2 | minimum protocol version | Lowest application protocol supported |
| 2 | 2 | maximum protocol version | Highest application protocol supported |
| 4 | 4 | chain ID | BBC chain identifier |
| 8 | 8 | services | Capability bit field |
| 16 | 16 | session nonce | Random value generated once per process start |
| 32 | 8 | tip height | Current active-chain height |
| 40 | 32 | tip block ID | Current active-chain tip ID |
| 72 | 32 | Genesis Block ID | Expected Genesis Block ID |

Stage 7.1 nodes advertise minimum and maximum protocol version `1`. The selected
application protocol is the highest version contained in both inclusive ranges.
The connection is rejected when the ranges do not overlap.

Service bits are:

- bit 0: full-node chain and validation service;
- bit 1: mining service;
- bits 2 through 63: reserved and sent as zero.

At least one known service bit must be present. The wallet role is not a network
service and is not advertised. A mining-only actor uses an outbound connection
and does not expose a listener.

The connection is rejected when the chain ID or Genesis Block ID differs from
the local network. Equal session nonces indicate a self-connection and are also
rejected. Tip fields are informational in Stage 7.1 and do not modify local
chain state.

The handshake is complete only after the local `HELLO` has been sent and a
valid remote `HELLO` has been received. A non-`HELLO` first message, a second
`HELLO`, or any application message before handshake completion causes a
disconnect.

## 6. PING and PONG

`PING` contains one arbitrary unsigned 64-bit nonce. A peer receiving `PING`
after handshake completion responds with `PONG` containing the exact same
nonce. `PONG` with no matching outstanding local ping is ignored and recorded
as a protocol observation; it does not close the connection.

Stage 7.1 uses these messages for explicit scenario checks and idle-peer
liveness. They do not carry timestamps and must not affect consensus state.

## 7. Duplicate connections

One live session is retained for each pair of process session nonces. When both
nodes connect to each other at the same time, the node with the lexicographically
smaller session nonce keeps its outbound connection and closes its inbound
duplicate. The node with the larger nonce makes the opposite choice. Comparing
the raw 16-byte values gives both nodes the same decision without depending on
actor names or wallet addresses.

An implementation must not start another outbound attempt to the same configured
endpoint while one is connecting or already handshaking. Closing an unwanted
duplicate must not trigger an immediate reconnect while the retained session is
healthy.

## 8. Resource and time limits

Stage 7.1 uses these defensive defaults:

| Resource | Limit |
| --- | ---: |
| Frame payload | 1,048,576 bytes |
| Queued outbound bytes per peer | 4,194,304 bytes |
| Simultaneous P2P connections per process | 64 |
| Connections from one remote IP | 16 |
| Handshake completion | 5 seconds |
| Idle time before a liveness ping | 30 seconds |
| Matching pong after a liveness ping | 10 seconds |
| Reconnect backoff | 250 ms initially, doubling to 5 seconds |

The byte and connection limits are enforced before accepting more work. Queue
overflow, handshake timeout, pong timeout, or malformed input closes that peer
without stopping the node process. Reconnect delay resets after a completed
handshake and uses bounded jitter outside deterministic tests.

These values are local denial-of-service policy, not consensus rules. They may
be made configurable later, but tests use fixed values unless a negative test
explicitly overrides a limit.

## 9. Stage 7.1 topology and control behavior

Only an actor with the `full_node` role exposes a P2P listener. It binds to the
configured loopback host and may use port zero so the operating system selects
a free port. The actor reports the resolved endpoint in its structured `ready`
event and control `status` response.

Scenario peer names are resolved by the runner after all listeners are ready.
The runner asks one actor to connect to another through an authenticated control
request. The request initiates an ordinary P2P connection; the control plane
does not transport P2P frames or declare the handshake successful.

Control state and normalized dumps expose peer direction, remote endpoint,
handshake state, selected protocol version, advertised services, remote height
and tip, and last ping result. They never expose the session nonce or control
token.

## 10. Required verification

Unit tests must cover:

- fixed byte vectors for `HELLO`, `PING`, and `PONG`;
- canonical payload-size enforcement for transaction and mining messages;
- one-byte-at-a-time header and payload delivery;
- multiple frames in one input buffer;
- bad magic, unsupported version, unknown type, oversized length, wrong fixed
  payload size, and bad checksum;
- handshake ordering, network mismatch, self-connection, and duplicate `HELLO`;
- matching `PING` and `PONG` nonces.

Multi-process tests must cover:

- two full nodes binding dynamic loopback ports;
- an explicit connection reaching handshake-complete on both sides;
- a successful ping round trip;
- simultaneous cross-connect converging to one session;
- peer restart followed by reconnect and another successful handshake.
