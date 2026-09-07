# ADR 0019: Explicit Internet P2P Scope

- Status: Accepted
- Date: 2026-09-06

## Context

Private LAN P2P permits multi-computer tests but cannot connect a node behind
NAT to a publicly reachable seed/full node. A first Internet topology needs
static public peer addresses without weakening the local RPC boundary or
silently exposing every existing application configuration.

EC2 commonly assigns a private address to the instance and maps an Elastic IP
to it. The address a process binds and the address remote peers dial are
therefore different operational values. The current wire protocol has no peer
address advertisement or discovery message.

## Decision

- Add `full_node.scope` with `loopback`, `lan`, and `internet` values.
- Default omitted scope to `lan` for compatibility with existing persistent
  configurations.
- Require explicit `internet` scope before accepting public listener or initial
  peer IPv4 addresses.
- Accept only numeric unicast IPv4 at runtime. Continue rejecting wildcard,
  multicast, limited broadcast, DNS, and IPv6 endpoints.
- Let mining-only processes connect outbound to a numeric public IPv4 source;
  they expose no P2P listener.
- Keep application RPC fixed to exact IPv4 loopback in every scope.
- Use the EC2 private address as its listener and configure its Elastic IP in
  remote nodes. Do not add an unused advertised-address field before address
  gossip exists in the wire protocol.

## Consequences

- Existing loopback and LAN configurations retain their behavior.
- Nodes behind NAT can maintain bidirectional TCP sessions to static public full
  nodes without inbound port forwarding.
- Selecting `internet` is an explicit operator decision but does not configure
  firewalls, NAT, an Elastic IP, DNS, or service management.
- One seed/full node is a transport bottleneck and isolation risk; multiple
  independent public peers and later peer discovery are still required.
- The P2P frame format, handshake, consensus objects, and fork choice do not
  change.
