# BBC P2P over the Internet

## Purpose and current boundary

Persistent BBC nodes can make P2P connections to numeric public IPv4 addresses
when their full-node configuration explicitly selects the `internet` scope.
This supports a first topology with one publicly reachable EC2 full node and
other nodes that initiate outbound connections from behind NAT.

This capability does not make BBC production cryptocurrency software. The
current implementation has bounded frames, connection limits, handshake
validation, and independent consensus validation, but it does not yet provide
DNS resolution, automatic peer discovery, address gossip, connection bans,
encrypted transport, NAT traversal, or operational monitoring.

Application RPC remains restricted to `127.0.0.1` in every P2P scope. Never
publish its port or token.

## Address scopes

The optional `full_node.scope` field accepts:

| Value | Listener and initial-peer addresses |
|---|---|
| `loopback` | Numeric IPv4 loopback in `127.0.0.0/8` |
| `lan` | Loopback or RFC 1918 private IPv4; this is the default |
| `internet` | Numeric unicast IPv4, including public addresses |

All scopes reject port zero in application configuration. P2P also rejects the
wildcard address `0.0.0.0`, multicast, the limited broadcast address, DNS names,
and IPv6. A listener must name an address assigned to the local computer.

Selecting `internet` permits public connectivity but does not create a public
address, change a router, or open a firewall. Reachability still depends on the
host, NAT, and network rules.

## EC2 seed and miner

The template [`../examples/ec2-node.json`](../examples/ec2-node.json) combines
the `full_node` and `miner` roles. Replace its placeholder values before use:

- set `full_node.listen.host` to the EC2 instance's private IPv4 address;
- allocate a stable Elastic IP and record it for other nodes;
- replace `miner.reward_address` with the address of the reward wallet;
- keep `miner.auto_start` enabled for continuous mining;
- choose persistent paths for the node data and RPC token.

AWS normally maps the Elastic IP to the instance's private address. The BBC
process therefore binds the private address, while remote configurations use
the Elastic IP as an initial peer. The current protocol does not advertise an
address to other peers, so the public address does not appear in the EC2 node's
own BBC configuration.

The reward address is public. Its private key does not need to be stored on the
EC2 instance; rewards can be sent to an encrypted wallet retained on another
computer.

The EC2 security group should allow:

- inbound TCP on the configured P2P port, such as `7333`;
- SSH only from the administrator's current source address;
- no inbound rule for the RPC port.

The instance's operating-system firewall must agree with the security group.
Opening the P2P port is an explicit public exposure decision.

## Node behind NAT

The template
[`../examples/internet-client.json`](../examples/internet-client.json) binds a
home full node to its private LAN address and lists the EC2 Elastic IP as an
initial peer. Replace both documentation addresses with real values.

The home node initiates an outbound TCP connection. NAT records the connection,
so the EC2 node can send blocks, transactions, pings, and synchronization
responses back through the same bidirectional socket. The home router does not
need a port-forwarding rule for this outbound-only topology.

If two nodes are both behind NAT and neither has port forwarding, they normally
cannot accept a direct connection from each other. They can still exchange data
through public full nodes to which both maintain outbound connections. A robust
network should eventually provide several independent public peers so one EC2
instance is not a transport bottleneck or a single source of isolation.

## Static bootstrap workflow

BBC currently uses a static initial-peer list:

1. Start the EC2 node and make its P2P port reachable through its Elastic IP.
2. Put that numeric Elastic IP and P2P port in each outbound node's `peers` list.
3. Start the outbound node; it reconnects automatically after failures.
4. Inspect each node locally with `bbc rpc status --config <path>`.
5. Require a completed handshake before relying on synchronization or relay.

The EC2 node is both a bootstrap contact and an ordinary full-node relay in this
initial topology. It cannot create valid transactions for another wallet or
force an invalid block into another full node, but an isolated client depending
on only that peer can be censored or deprived of current data. Multiple public
connections are the planned mitigation.

## Remaining deployment work

The executable can express and enforce static numeric IPv4 connectivity. The
CMake project produces a Linux release archive with a systemd service template,
and [`ec2-deployment.md`](ec2-deployment.md) defines its installation. EC2
operation still requires:

- durable storage, logs, health checks, and backups;
- a real multi-host connectivity and synchronization test.
