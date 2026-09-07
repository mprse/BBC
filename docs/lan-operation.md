# Running BBC on a Local Network

## Purpose and boundary

BBC can run its P2P connection between computers on the same trusted local
network. A full node may bind to one numeric IPv4 loopback address or one
private IPv4 address from these RFC 1918 ranges:

- `10.0.0.0/8`;
- `172.16.0.0/12`;
- `192.168.0.0/16`.

The application rejects `0.0.0.0`, public IP addresses, DNS names, IPv6, and
addresses outside those ranges before creating an RPC token or node data. This
restriction avoids accidentally exposing the current educational node to the
public Internet. It is an application safety policy and does not change the P2P
wire protocol or consensus rules.

Use `"scope": "lan"` in the `full_node` object. This is also the default when
the field is omitted. Public numeric peers require the separate explicit
`internet` scope described in [`internet-p2p.md`](internet-p2p.md).

Application RPC remains bound to `127.0.0.1`. Commands such as `rpc status`,
`rpc submit`, and `rpc stop` must be run locally on the same computer as their
node. Never add an inbound firewall rule for the RPC port.

## Find each computer's address

Connect both Windows computers to the same trusted LAN. On each computer, run:

```console
ipconfig
```

Record the active adapter's `IPv4 Address`, such as `192.168.1.10` and
`192.168.1.20`. Do not use a public address reported by a website, a disconnected
adapter, a virtual-machine adapter, or the router's address.

The examples below use:

| Computer | P2P address | P2P port | RPC address |
|---|---|---:|---|
| Node A | `192.168.1.10` | `7433` | `127.0.0.1:7434` |
| Node B | `192.168.1.20` | `7433` | `127.0.0.1:7434` |

Both computers may use the same ports because each owns a different IP address.

## Configure node B

Copy an application configuration to computer B and set its full-node section
to its own LAN address:

```json
"full_node": {
  "scope": "lan",
  "listen": {"host": "192.168.1.20", "port": 7433},
  "peers": []
},
"rpc": {
  "listen": {"host": "127.0.0.1", "port": 7434},
  "token_file": "node-b.token"
}
```

The rest of the file still requires `schema_version`, `name`, `network`, roles,
wallet, and data-directory settings as described in
[`application-config.md`](application-config.md).

## Configure node A

Bind node A to its own LAN address and list node B as the initial peer:

```json
"full_node": {
  "scope": "lan",
  "listen": {"host": "192.168.1.10", "port": 7433},
  "peers": [
    {"host": "192.168.1.20", "port": 7433}
  ]
},
"rpc": {
  "listen": {"host": "127.0.0.1", "port": 7434},
  "token_file": "node-a.token"
}
```

Only one node needs to initiate the connection. Once the handshake completes,
the TCP connection carries messages in both directions. Both nodes must select
the same BBC network profile; use `regtest` for a quick experiment or
`development` for the normal slower development target.

Validate each complete file locally before starting its process:

```console
.\build\windows-msvc-debug\bbc.exe config validate --file bbc.json
```

## Windows Firewall

Allow inbound TCP traffic only for the configured P2P port, only on the Private
Windows network profile, and preferably only from the local subnet. Do not open
the RPC port. Windows may offer to create this rule when the node first binds;
review the network profile before accepting it.

If creating the rule manually, use Windows Defender Firewall with Advanced
Security and configure an inbound port rule with:

- protocol `TCP`;
- local port `7433` or the configured P2P port;
- action `Allow the connection`;
- profile `Private` only;
- remote address limited to `Local subnet` when available.

The exact administrative interface differs between supported Windows editions.
The BBC process does not modify firewall rules itself.

## Start and verify

Start node B first on computer B, then node A on computer A:

```console
.\build\windows-msvc-debug\bbc.exe node run --config bbc.json
```

On each computer, use a second local terminal:

```console
.\build\windows-msvc-debug\bbc.exe rpc status --config bbc.json
```

Both status responses should show a completed P2P handshake. Mining, signed
transaction submission, state comparison, and restart then use the same local
RPC commands as [`persistent-network-test.md`](persistent-network-test.md).

If the connection fails, verify the configured addresses with `ipconfig`, check
that both nodes use the same network profile and Genesis identity, confirm that
the P2P port is not already occupied, and inspect the firewall rule. A changing
DHCP address requires updating the configuration or reserving that address in
the router.

## Intentionally unsupported

LAN scope is not public-node deployment. The separate `internet` scope supports
static numeric public peers, but the current runtime does not provide DNS
resolution, IPv6, TLS, NAT traversal, router port forwarding, peer discovery,
connection bans, or an Internet deployment package. An EC2 node also needs a
release build, service management, logging, and resource controls.
