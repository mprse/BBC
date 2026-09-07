# ADR 0021: Linux Release Package

- Status: Accepted
- Date: 2026-09-07

## Context

A CMake debug directory is not a stable deployment artifact. EC2 needs an
optimized executable and a predictable install layout that can be recreated on
the target architecture without committing generated binaries.

## Decision

- Add release presets for the supported compilers and operating systems.
- Use `linux-gcc-release` for the EC2 executable.
- Keep automated tests in debug presets and disable test targets in release
  presets.
- Install the executable under `bin`, documentation under `share/doc/bbc`, and
  deployment examples under `share/bbc`.
- Generate a `.tar.gz` package with CPack only on Linux.
- Add `python3 tools/build.py package` as the Linux deployment entry point.
- Build the package on the same architecture as the EC2 target.

## Consequences

- Generated release directories and archives remain under ignored `build/`.
- Windows can validate the release configuration and install layout, but it
  cannot produce or verify the Linux executable.
- The first EC2 preparation run must execute the Linux release build and tests
  before installing the service.
- This changes build and deployment behavior only; protocol and consensus data
  are unchanged.
