# ADR 0018: Private LAN P2P Boundary

- Status: Accepted
- Date: 2026-09-06

## Context

Persistent BBC nodes can already connect, relay transactions and blocks,
synchronize, reorganize, and reopen state, but their runtime is restricted to
IPv4 loopback. Testing the same application processes on two physical computers
requires a narrowly defined LAN boundary before any public-Internet deployment.

The P2P protocol is unauthenticated and the project does not yet provide public
peer discovery, address resolution, ban policy, deployment packaging, or
operational monitoring. The authenticated application RPC contains local node
controls and must not become a network service.

## Decision

- Keep scenario P2P and all application RPC listeners restricted to IPv4
  loopback.
- Let persistent application P2P listeners and outbound targets use numeric IPv4
  loopback or RFC 1918 addresses.
- Require a full node to bind one exact local address. Reject the wildcard
  `0.0.0.0` address so configuration does not silently expose every interface.
- Reject DNS, IPv6, link-local, multicast, documentation, and public IPv4
  addresses at the runtime boundary.
- Keep frame parsing, handshake validation, resource limits, consensus
  validation, and network profiles unchanged.
- Document a Private-profile, P2P-port-only Windows Firewall rule and explicitly
  forbid exposing the loopback RPC port.

## Consequences

- Two computers on one private LAN can run normal configured BBC processes and
  communicate directly without the scenario runner.
- The existing loopback tests remain deterministic, while address-policy tests
  cover exact accepted and rejected ranges.
- A host with multiple interfaces must name the intended private interface
  rather than binding all interfaces.
- Public Internet and EC2 peer connectivity remain unsupported until their
  separate security and deployment requirements are implemented. ADR 0019
  subsequently defines an explicit static Internet scope while deployment
  packaging remains unsupported.
