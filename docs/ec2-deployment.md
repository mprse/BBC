# BBC EC2 Deployment

## Purpose and boundary

This guide installs one persistent BBC full node and miner as a managed Ubuntu
service. It assumes an EC2 instance with a numeric private IPv4 address and a
stable Elastic IP. AWS account, instance, key-pair, Elastic IP, and security
group creation happen outside BBC and are deliberately separated from the
commands below.

BBC remains educational software and must not hold real value. The first
deployment is a development network, not a production cryptocurrency service.

## Files and identities

The packaged service uses these locations:

| Path | Owner | Purpose |
|---|---|---|
| `/opt/bbc` | root | versioned executable, documentation, and templates |
| `/etc/bbc/bbc.json` | root | public application configuration |
| `/var/lib/bbc` | `bbc` | block history, SQLite cache, mempool, and RPC token |
| `/etc/systemd/system/bbc.service` | root | systemd service definition |

The `bbc` system user has no interactive login. The service umask is `0077`, so
the generated RPC token is readable only by that user. The configuration
contains a public reward address but no private key or wallet password.

## Build the Linux release

Use an Ubuntu 24.04 LTS instance whose architecture matches the intended
deployment. Install the prerequisites and vcpkg as described in
[`toolchain.md`](toolchain.md), clone the repository, and run:

```console
python3 tools/build.py test
python3 tools/build.py package
```

Do not install an archive if either command fails. The package appears as
`build/linux-gcc-release/bbc-<version>-linux-<architecture>.tar.gz`.

## Install the package

The following commands require an administrator account. Replace the archive
name with the file created on the instance:

```console
sudo useradd --system --home /var/lib/bbc --shell /usr/sbin/nologin bbc
sudo install -d -o root -g root -m 0755 /opt/bbc /etc/bbc
sudo install -d -o bbc -g bbc -m 0700 /var/lib/bbc
sudo tar --no-same-owner -xzf build/linux-gcc-release/bbc-0.1.0-linux-x86_64.tar.gz -C /opt/bbc
sudo install -o root -g root -m 0644 /opt/bbc/share/bbc/examples/ec2-node.json /etc/bbc/bbc.json
sudo install -o root -g root -m 0644 /opt/bbc/share/bbc/systemd/bbc.service /etc/systemd/system/bbc.service
```

If the `bbc` user already exists, `useradd` reports that fact; verify the
existing account instead of creating a second identity.

## Configure the node

Edit `/etc/bbc/bbc.json` before starting the service. Replace:

- `full_node.listen.host` with the instance's private IPv4 address;
- `miner.reward_address` with the public BBC address that receives rewards.

Keep `full_node.scope` as `internet`, `miner.auto_start` as `true`, RPC on
`127.0.0.1`, and the `/var/lib/bbc` data and token paths. The configured wallet
path is not opened by the long-running service; reward ownership may remain in
an encrypted wallet on another computer.

Validate the edited file as the service user:

```console
sudo -u bbc /opt/bbc/bin/bbc config validate --file /etc/bbc/bbc.json
sudo -u bbc /opt/bbc/bin/bbc config show --file /etc/bbc/bbc.json
```

## Start and inspect the service

After configuration validation succeeds:

```console
sudo systemctl daemon-reload
sudo systemctl enable --now bbc.service
sudo systemctl status bbc.service
sudo journalctl -u bbc.service -f
```

Run local RPC commands as the service user because the token is private:

```console
sudo -u bbc /opt/bbc/bin/bbc rpc health --config /etc/bbc/bbc.json
sudo -u bbc /opt/bbc/bin/bbc rpc status --config /etc/bbc/bbc.json
sudo -u bbc /opt/bbc/bin/bbc rpc stop-mining --config /etc/bbc/bbc.json
sudo -u bbc /opt/bbc/bin/bbc rpc start-continuous-mining --config /etc/bbc/bbc.json
```

`systemctl stop bbc.service` invokes the authenticated local RPC shutdown and
waits up to 60 seconds before systemd applies its normal forced-stop policy.

## Service isolation

The supplied unit runs without Linux capabilities, restricts the process to
IPv4 and local Unix sockets, mounts the operating system and home directories
read-only or inaccessible, and grants write access only to `/var/lib/bbc`.
Logs go to the system journal. Validate the unit on the instance with:

```console
sudo systemd-analyze verify /etc/systemd/system/bbc.service
```

Do not weaken these restrictions to solve a path mistake. Keep mutable node
state below `/var/lib/bbc` and public configuration below `/etc/bbc`.

## AWS configuration still required

Before another computer can connect, configure the instance and AWS network:

- allocate and associate an Elastic IP;
- allow inbound TCP port `7333` in the security group;
- allow SSH only from the administrator's current public IP;
- do not add an inbound rule for RPC port `7334`;
- verify the instance's private IPv4 address matches `bbc.json`.

These are the next manual deployment steps. After they are complete, use the
Elastic IP in a remote node's initial peer list and perform the multi-host
connectivity and synchronization test from [`internet-p2p.md`](internet-p2p.md).
