# ADR 0008: Networking Foundation

- Status: Accepted
- Date: 2026-09-04

## Context

BBC needs cross-platform TCP, readable test configuration, multiple managed
processes, and deterministic observation. Implementing P2P framing and process
orchestration simultaneously would make failures difficult to localize.

## Decision

- Use standalone Asio for cross-platform TCP and asynchronous I/O in C++.
- Use nlohmann/json for generated actor configuration and the test-control
  protocol. P2P consensus objects remain canonical binary data, not JSON.
- Use a Python standard-library runner because Python is already the supported
  cross-platform developer entry point.
- Separate the P2P data plane from an opt-in loopback-only test-control plane.
- Establish process supervision, isolated data directories, structured events,
  control authentication, state dumps, assertions, and shutdown before
  implementing P2P messages.
- Use operating-system-assigned control ports when a scenario specifies port
  zero, while still supporting explicit unique ports for visual scenarios.

## Consequences

- Asio and nlohmann/json become pinned vcpkg manifest dependencies.
- The first two-process scenario proves orchestration but not node-to-node
  communication; both actors independently remain at canonical Genesis.
- Test-control JSON cannot be used as a shortcut around normal transaction,
  mempool, block, or chain validation.
- The same runner can later drive transaction propagation, mining, sync, fork,
  and reorganization scenarios without becoming a central blockchain service.
