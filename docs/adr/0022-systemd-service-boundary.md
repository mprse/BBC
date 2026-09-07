# ADR 0022: systemd Service Boundary

- Status: Accepted
- Date: 2026-09-07

## Context

An EC2 node must restart after a reboot, write only to durable application
storage, expose P2P without exposing RPC, and stop through the node's graceful
shutdown path.

## Decision

- Install the release below `/opt/bbc`, public configuration below `/etc/bbc`,
  and mutable state below `/var/lib/bbc`.
- Run the process as a dedicated non-login `bbc` system user.
- Keep RPC on loopback and its generated token readable only by that user.
- Use `ExecStop` to request authenticated RPC shutdown before systemd applies
  its timeout policy.
- Restart only after failures, not after an intentional clean stop.
- Apply filesystem, capability, device, address-family, and privilege
  restrictions suitable for the current IPv4 service.
- Send standard output and error to the system journal.

## Consequences

- Operators use `systemctl`, `journalctl`, and local `bbc rpc` commands instead
  of keeping an SSH terminal open.
- The node can write only below `/var/lib/bbc`; configuration and installed
  files remain read-only to the service.
- Package installation does not edit AWS security groups, allocate an Elastic
  IP, create backups, or expose RPC.
- The unit must be verified with the target instance's `systemd-analyze` before
  it is enabled.
- This is an operational boundary and does not change protocol or consensus.
