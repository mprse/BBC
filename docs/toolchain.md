# BBC Toolchain and Build Guide

## Purpose

BBC uses one CMake project and one Python entry point across supported operating
systems. The compiler changes by platform, but source code, dependencies,
warnings, tests, and directory layout remain consistent.

Windows is the primary development environment today. Linux with GCC is the
target environment for a public full node on a VPS or AWS EC2. The Linux debug
build preset and explicit Internet P2P scope exist; release packaging and
managed service operation are still being prepared.

## Common tools

| Tool | Minimum or selected version | Purpose |
| --- | --- | --- |
| C++ | C++20 | Application and protocol implementation |
| CMake | 3.25 | Cross-platform build configuration |
| Ninja | Current supported release | Parallel build execution |
| Python | 3.9 | Toolchain launcher and scenario runner |
| vcpkg | Repository baseline | Reproducible C++ dependencies |
| Git | Current supported release | Source history and collaboration |

The vcpkg manifest installs:

- Asio for asynchronous TCP networking;
- libsodium for cryptography;
- SQLite for embedded persistent caches;
- nlohmann/json for configuration, control messages, and events;
- Catch2 for C++ tests.

`tools/build.py` locates the platform tools, supplies `VCPKG_ROOT`, selects the
matching preset, and runs CMake or CTest. It does not install compilers or make
persistent system changes.

## Standard commands

Run commands from the repository root:

```console
python tools/build.py configure
python tools/build.py build
python tools/build.py test
python tools/build.py run
python tools/build.py package
python tools/build.py shell
```

`package` is available on Linux and selects the release preset by default. The
other actions continue to select the platform's debug preset unless `--preset`
overrides it.

`build` is the default action. Build output is isolated by preset:

```text
build/windows-msvc-debug/
build/windows-msvc-release/
build/linux-gcc-debug/
build/linux-gcc-release/
build/macos-clang-debug/
build/macos-clang-release/
```

## Windows development

### Required software

Install:

1. Visual Studio Code;
2. the Microsoft C/C++ extension;
3. the CMake Tools extension;
4. Visual Studio Build Tools with **Desktop development with C++**, MSVC x64,
   and a Windows SDK;
5. Python 3.9 or newer;
6. Git;
7. vcpkg, with its executable directory on `PATH`.

CMake and Ninja may come from Visual Studio or another installation. The helper
finds the Visual Studio installation through `vswhere.exe`, invokes
`VsDevCmd.bat` in a child process, and passes the resulting MSVC environment to
CMake. An ordinary PowerShell or `cmd` terminal is sufficient. Do not weaken the
PowerShell execution policy and do not run Visual Studio Code as Administrator.

### Build and test

```console
python tools/build.py test
```

The selected preset is `windows-msvc-debug`. It enables C++20, disables compiler
extensions, and builds with `/W4 /permissive-`.

Run the executable directly:

```console
.\build\windows-msvc-debug\bbc.exe
```

### Visual Studio Code

Open the repository folder, select the `Windows MSVC Debug` configure preset in
CMake Tools, and run `CMake: Build`. The integrated terminal may use the same
Python commands as an external terminal.

## Linux development and EC2 preparation

The Linux preset uses GCC, Ninja, and the same vcpkg manifest. Ubuntu 24.04 LTS
or another recent distribution with CMake 3.25 or newer is a suitable starting
point.

On Ubuntu, install the system tools:

```console
sudo apt update
sudo apt install build-essential cmake ninja-build python3 git curl zip unzip tar pkg-config
```

Install vcpkg in a user-owned tools directory and prepare its environment:

```console
git clone https://github.com/microsoft/vcpkg.git "$HOME/tools/vcpkg"
"$HOME/tools/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_ROOT="$HOME/tools/vcpkg"
export PATH="$VCPKG_ROOT:$PATH"
```

Clone BBC, enter its repository directory, and verify the complete build:

```console
python3 tools/build.py test
python3 tools/build.py run
```

The selected preset is `linux-gcc-debug`. It builds with
`-Wall -Wextra -Wpedantic`. Verify the optimized executable and create the
deployment archive with:

```console
python3 tools/build.py build --preset linux-gcc-release
python3 tools/build.py package
```

The package command configures `linux-gcc-release`, builds with tests disabled,
and runs CPack. It writes
`build/linux-gcc-release/bbc-<version>-linux-<architecture>.tar.gz`. The archive
contains `bin/bbc`, the EC2 configuration template, the license, and reference
documentation. Build the archive on the same Linux architecture used by the
target EC2 instance; a Windows binary is not a Linux deployment artifact.

On EC2, also plan for:

- persistent storage for the node data directory;
- a fixed public Elastic IP for the current numeric IPv4 peer configuration;
- an inbound security-group rule for the BBC P2P TCP port;
- SSH access restricted to the administrator's source address;
- a service manager such as `systemd` to restart BBC after a reboot;
- monitoring and backups of authoritative `blocks.dat` data.

Do not expose the loopback application RPC or test-control endpoint to a LAN or
the Internet. Static Internet P2P configuration is documented in
[`internet-p2p.md`](internet-p2p.md). Managed service operation is documented
separately from compilation and packaging.

## Private LAN testing

The Windows build can bind application P2P to one private RFC 1918 address for
testing between computers on the same trusted network. Keep RPC on
`127.0.0.1`, open only the selected P2P TCP port on the Windows Private firewall
profile, and follow [`lan-operation.md`](lan-operation.md). Public Internet and
EC2 P2P exposure use the explicit `internet` address scope described in
[`internet-p2p.md`](internet-p2p.md).

## macOS

The `macos-clang-debug` and `macos-clang-release` presets select Clang and
otherwise follow the same Python, CMake, Ninja, and vcpkg workflow. macOS is a
supported project target but is not part of the current Windows-to-EC2
deployment path.

## Dependency behavior

The first configure after a clean clone may take longer because vcpkg downloads,
builds, and caches dependencies. Later builds normally report that packages are
already installed. These downloads are toolchain preparation; the BBC
application itself does not download libraries when it runs.

`vcpkg.json` and its pinned baseline are the dependency source of truth. Do not
copy third-party binaries into the repository or install undeclared libraries
manually to make one workstation pass.

## Troubleshooting

### `cmake` or `ninja` is not found

Use `python tools/build.py ...` from the repository root. If the helper also
reports a missing tool, install it or correct `PATH`.

### MSVC environment is missing

Modify the Visual Studio Build Tools installation and enable **Desktop
development with C++**. The helper requires `vswhere.exe`, `VsDevCmd.bat`, an x64
MSVC compiler, and a Windows SDK.

### vcpkg is not found

Place the vcpkg executable directory on `PATH` or define `VCPKG_ROOT` for the
current terminal. `VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake` must exist.

### A fresh build downloads many packages

This is expected once per dependency version and target triplet. Do not commit
the generated `build/` directory.

### Verification before a commit

Run:

```console
python tools/build.py test
python tools/build.py run
git diff --check
```
