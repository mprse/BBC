#!/usr/bin/env python3
"""Configure, build, and run BBC with a platform-appropriate toolchain."""

from __future__ import annotations

import argparse
import os
import platform
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Sequence


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_PRESETS = {
    "Windows": "windows-msvc-debug",
    "Linux": "linux-gcc-debug",
    "Darwin": "macos-clang-debug",
}
DEFAULT_RELEASE_PRESETS = {
    "Windows": "windows-msvc-release",
    "Linux": "linux-gcc-release",
    "Darwin": "macos-clang-release",
}


class ToolchainError(RuntimeError):
    """Raised when the required native toolchain cannot be prepared."""


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Configure, build, test, run, or package BBC."
    )
    parser.add_argument(
        "action",
        choices=("configure", "build", "test", "run", "package", "shell"),
        nargs="?",
        default="build",
        help="operation to perform (default: build)",
    )
    parser.add_argument(
        "--preset",
        help="override the default CMake preset for the current platform",
    )
    return parser.parse_args()


def find_visual_studio() -> Path:
    candidates = []
    program_files_x86 = os.environ.get("ProgramFiles(x86)")
    if program_files_x86:
        candidates.append(
            Path(program_files_x86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
        )

    candidates.append(
        Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")
    )

    vswhere_from_path = shutil.which("vswhere")
    if vswhere_from_path:
        candidates.append(Path(vswhere_from_path))

    vswhere = next((path for path in candidates if path.is_file()), None)
    if vswhere is None:
        raise ToolchainError(
            "Visual Studio Installer (vswhere.exe) was not found. "
            "Install Visual Studio Build Tools with the Desktop development with C++ workload."
        )

    result = subprocess.run(
        [
            str(vswhere),
            "-latest",
            "-products",
            "*",
            "-requires",
            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
            "-property",
            "installationPath",
        ],
        check=True,
        capture_output=True,
        text=True,
    )

    installation_path = result.stdout.strip()
    if not installation_path:
        raise ToolchainError(
            "MSVC was not found. Install the Desktop development with C++ workload."
        )

    return Path(installation_path)


def windows_build_environment() -> dict[str, str]:
    visual_studio = find_visual_studio()
    developer_script = visual_studio / "Common7" / "Tools" / "VsDevCmd.bat"
    if not developer_script.is_file():
        raise ToolchainError(f"Visual Studio environment script not found: {developer_script}")

    with tempfile.TemporaryDirectory(prefix="bbc-build-") as temporary_directory:
        environment_script = Path(temporary_directory) / "environment.cmd"
        environment_script.write_text(
            "@echo off\n"
            f'call "{developer_script}" -arch=x64 -host_arch=x64 -no_logo\n'
            "if errorlevel 1 exit /b %errorlevel%\n"
            "set\n",
            encoding="utf-8",
        )
        result = subprocess.run(
            [
                os.environ.get("COMSPEC", "cmd.exe"),
                "/d",
                "/c",
                str(environment_script),
            ],
            check=False,
            capture_output=True,
            text=True,
        )

    if result.returncode != 0:
        details = result.stderr.strip() or result.stdout.strip()
        raise ToolchainError(f"Could not initialize the MSVC environment: {details}")

    # `set` returns the complete child environment. Rebuilding the mapping avoids
    # duplicate case variants such as `Path` and `PATH`, which Windows itself
    # treats as the same variable but Python dictionaries do not.
    environment: dict[str, str] = {}
    for line in result.stdout.splitlines():
        name, separator, value = line.partition("=")
        if separator and name:
            environment[name] = value

    path_entries = [
        (name, value)
        for name, value in environment.items()
        if name.casefold() == "path"
    ]
    if path_entries:
        for name, _ in path_entries:
            del environment[name]
        environment["Path"] = max((value for _, value in path_entries), key=len)

    return environment


def build_environment() -> dict[str, str]:
    environment = (
        windows_build_environment()
        if platform.system() == "Windows"
        else os.environ.copy()
    )

    if "VCPKG_ROOT" not in environment:
        vcpkg = require_tool("vcpkg", environment)
        environment["VCPKG_ROOT"] = str(Path(vcpkg).resolve().parent)

    toolchain_file = (
        Path(environment["VCPKG_ROOT"])
        / "scripts"
        / "buildsystems"
        / "vcpkg.cmake"
    )
    if not toolchain_file.is_file():
        raise ToolchainError(f"vcpkg CMake toolchain not found: {toolchain_file}")

    return environment


def require_tool(name: str, environment: dict[str, str]) -> str:
    search_path = next(
        (value for key, value in environment.items() if key.casefold() == "path"),
        None,
    )
    path = shutil.which(name, path=search_path)
    if path is None:
        raise ToolchainError(f"Required tool '{name}' was not found on PATH.")
    return path


def display_command(command: Sequence[str]) -> str:
    if platform.system() == "Windows":
        return subprocess.list2cmdline(command)
    return shlex.join(command)


def run_command(command: Sequence[str], environment: dict[str, str]) -> None:
    print(f"> {display_command(command)}", flush=True)
    subprocess.run(
        list(command),
        cwd=PROJECT_ROOT,
        env=environment,
        check=True,
    )


def configure(cmake: str, preset: str, environment: dict[str, str]) -> None:
    run_command((cmake, "--preset", preset), environment)


def build(cmake: str, preset: str, environment: dict[str, str]) -> None:
    configure(cmake, preset, environment)
    run_command((cmake, "--build", "--preset", preset), environment)


def test(ctest: str, cmake: str, preset: str, environment: dict[str, str]) -> None:
    build(cmake, preset, environment)
    run_command((ctest, "--preset", preset), environment)


def package(cmake: str, preset: str, environment: dict[str, str]) -> None:
    if platform.system() != "Linux":
        raise ToolchainError("Deployment packages can currently be built only on Linux.")
    build(cmake, preset, environment)
    run_command(
        (cmake, "--build", "--preset", preset, "--target", "package"),
        environment,
    )


def run_program(preset: str, environment: dict[str, str]) -> None:
    executable_name = "bbc.exe" if platform.system() == "Windows" else "bbc"
    executable = PROJECT_ROOT / "build" / preset / executable_name
    if not executable.is_file():
        raise ToolchainError(f"Built executable not found: {executable}")
    run_command((str(executable),), environment)


def open_shell(environment: dict[str, str]) -> None:
    if platform.system() == "Windows":
        shell = require_tool("powershell.exe", environment)
        command = (shell, "-NoLogo")
    else:
        shell = environment.get("SHELL") or require_tool("sh", environment)
        command = (shell,)

    print("Opening a shell with the native build environment configured.")
    print("Exit the child shell to return to the original terminal.")
    run_command(command, environment)


def main() -> int:
    arguments = parse_arguments()
    system = platform.system()
    defaults = (
        DEFAULT_RELEASE_PRESETS
        if arguments.action == "package"
        else DEFAULT_PRESETS
    )
    preset = arguments.preset or defaults.get(system)
    if preset is None:
        raise ToolchainError(f"Unsupported platform: {system}")

    environment = build_environment()

    if arguments.action == "shell":
        open_shell(environment)
        return 0

    cmake = require_tool("cmake", environment)
    require_tool("ninja", environment)

    if arguments.action == "configure":
        configure(cmake, preset, environment)
    elif arguments.action == "build":
        build(cmake, preset, environment)
    elif arguments.action == "test":
        ctest = require_tool("ctest", environment)
        test(ctest, cmake, preset, environment)
    elif arguments.action == "run":
        build(cmake, preset, environment)
        run_program(preset, environment)
    elif arguments.action == "package":
        package(cmake, preset, environment)

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ToolchainError, subprocess.CalledProcessError) as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
