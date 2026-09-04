#!/usr/bin/env python3
"""Run a local multi-process BBC scenario."""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import platform
import queue
import re
import secrets
import shutil
import socket
import subprocess
import sys
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_EXECUTABLES = {
    "Windows": PROJECT_ROOT / "build" / "windows-msvc-debug" / "bbc.exe",
    "Linux": PROJECT_ROOT / "build" / "linux-gcc-debug" / "bbc",
    "Darwin": PROJECT_ROOT / "build" / "macos-clang-debug" / "bbc",
}
ACTOR_NAME = re.compile(r"^[A-Za-z0-9_-]{1,64}$")
KNOWN_ROLES = {"wallet", "full_node", "miner"}


class ScenarioError(RuntimeError):
    """Raised when a scenario cannot be run or an assertion fails."""


def enable_ansi_output() -> bool:
    if not sys.stdout.isatty():
        return False
    if platform.system() != "Windows":
        return os.environ.get("TERM") != "dumb"

    standard_output_handle = -11
    enable_virtual_terminal_processing = 0x0004
    kernel32 = ctypes.windll.kernel32
    kernel32.GetStdHandle.argtypes = [ctypes.c_ulong]
    kernel32.GetStdHandle.restype = ctypes.c_void_p
    kernel32.GetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ulong)]
    kernel32.GetConsoleMode.restype = ctypes.c_int
    kernel32.SetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    kernel32.SetConsoleMode.restype = ctypes.c_int
    handle = kernel32.GetStdHandle(ctypes.c_ulong(standard_output_handle).value)
    mode = ctypes.c_ulong()
    invalid_handle = ctypes.c_void_p(-1).value
    if handle in (None, invalid_handle) or not kernel32.GetConsoleMode(
        handle, ctypes.byref(mode)
    ):
        return False
    return bool(
        kernel32.SetConsoleMode(
            handle,
            mode.value | enable_virtual_terminal_processing,
        )
    )


@dataclass
class Actor:
    name: str
    roles: list[str]
    requested_port: int
    directory: Path
    config_path: Path
    token: str
    process: subprocess.Popen[str] | None = None
    control_port: int | None = None
    ready: bool = False
    events: deque[str] = field(default_factory=lambda: deque(maxlen=5))


class Display:
    def __init__(self, actors: list[Actor], plain: bool) -> None:
        self._actors = actors
        self._plain = plain or not enable_ansi_output()
        self._controller = "Preparing scenario"

    def event(self, actor: Actor, line: str) -> None:
        try:
            message = json.loads(line)
            event = message.get("event", "message")
            details = message.get("details", {})
            summary = f"{event}: {json.dumps(details, separators=(',', ':'))}"
        except json.JSONDecodeError:
            summary = line
        actor.events.append(summary)
        if self._plain:
            print(f"[{actor.name}] {summary}", flush=True)
        else:
            self.render()

    def controller(self, message: str) -> None:
        self._controller = message
        if self._plain:
            print(f"[runner] {message}", flush=True)
        else:
            self.render()

    def render(self) -> None:
        width = max(60, shutil.get_terminal_size((120, 30)).columns)
        columns = 3 if width >= 150 else 2 if width >= 100 else 1
        cell_width = max(20, width // columns - 1)
        panels: list[list[str]] = []
        for actor in self._actors:
            state = "ready" if actor.ready else "starting"
            title = f" {actor.name} [{state}] "
            lines = [title.center(cell_width, "-"), *actor.events]
            while len(lines) < 6:
                lines.append("")
            panels.append([line[:cell_width].ljust(cell_width) for line in lines[:6]])

        output = ["\x1b[2J\x1b[H", f"BBC scenario: {self._controller}"[:width], ""]
        for start in range(0, len(panels), columns):
            row = panels[start : start + columns]
            for line_index in range(6):
                output.append(" ".join(panel[line_index] for panel in row))
            output.append("")
        print("\n".join(output), end="", flush=True)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run a local BBC network scenario.")
    parser.add_argument("scenario", type=Path, help="JSON scenario file")
    parser.add_argument("--executable", type=Path, help="path to the built bbc executable")
    parser.add_argument("--no-ui", action="store_true", help="use prefixed line output")
    parser.add_argument(
        "--keep",
        action="store_true",
        help="keep generated actor configuration files after success",
    )
    return parser.parse_args()


def load_document(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except OSError as error:
        raise ScenarioError(f"Could not read scenario: {error}") from error
    except json.JSONDecodeError as error:
        raise ScenarioError(f"Scenario is not valid JSON: {error}") from error
    if not isinstance(document, dict) or document.get("schema_version") != 1:
        raise ScenarioError("Scenario schema_version must be 1.")
    return document


def parse_control(value: Any) -> int:
    if not isinstance(value, str):
        raise ScenarioError("Actor control endpoint must be a string.")
    host, separator, encoded_port = value.rpartition(":")
    if separator != ":" or host != "127.0.0.1":
        raise ScenarioError("Actor control endpoint must use 127.0.0.1.")
    try:
        port = int(encoded_port)
    except ValueError as error:
        raise ScenarioError("Actor control port must be an integer.") from error
    if port < 0 or port > 65_535:
        raise ScenarioError("Actor control port must be between 0 and 65535.")
    return port


def create_actors(document: dict[str, Any], run_directory: Path) -> list[Actor]:
    encoded_actors = document.get("actors")
    if not isinstance(encoded_actors, list) or not encoded_actors:
        raise ScenarioError("Scenario must contain at least one actor.")

    actors: list[Actor] = []
    names: set[str] = set()
    fixed_ports: set[int] = set()
    for encoded in encoded_actors:
        if not isinstance(encoded, dict):
            raise ScenarioError("Every actor must be an object.")
        name = encoded.get("name")
        roles = encoded.get("roles")
        if not isinstance(name, str) or not ACTOR_NAME.fullmatch(name):
            raise ScenarioError("Actor names must use letters, digits, '_' or '-'.")
        if name in names:
            raise ScenarioError(f"Duplicate actor name: {name}")
        if (
            not isinstance(roles, list)
            or not roles
            or any(not isinstance(role, str) or role not in KNOWN_ROLES for role in roles)
            or len(set(roles)) != len(roles)
        ):
            raise ScenarioError(f"Actor {name} has invalid roles.")
        port = parse_control(encoded.get("control"))
        if port != 0 and port in fixed_ports:
            raise ScenarioError(f"Duplicate fixed control port: {port}")
        names.add(name)
        if port != 0:
            fixed_ports.add(port)
        actor_directory = run_directory / "actors" / name
        actors.append(
            Actor(
                name=name,
                roles=roles,
                requested_port=port,
                directory=actor_directory,
                config_path=actor_directory / "actor.generated.json",
                token=secrets.token_hex(32),
            )
        )
    return actors


def write_actor_config(actor: Actor) -> None:
    actor.directory.mkdir(parents=True, exist_ok=True)
    config = {
        "schema_version": 1,
        "name": actor.name,
        "roles": actor.roles,
        "data_directory": str((actor.directory / "data").resolve()),
        "control": {
            "host": "127.0.0.1",
            "port": actor.requested_port,
            "token": actor.token,
        },
    }
    actor.config_path.write_text(
        json.dumps(config, indent=2) + "\n",
        encoding="utf-8",
    )


def actor_output_reader(actor: Actor, output_queue: queue.Queue[tuple[Actor, str]]) -> None:
    assert actor.process is not None
    assert actor.process.stdout is not None
    log_path = actor.directory / "actor.events.jsonl"
    with log_path.open("w", encoding="utf-8", newline="\n") as log:
        for line in actor.process.stdout:
            value = line.rstrip("\r\n")
            log.write(value + "\n")
            log.flush()
            output_queue.put((actor, value))


def start_actors(
    actors: list[Actor],
    executable: Path,
    output_queue: queue.Queue[tuple[Actor, str]],
) -> None:
    for actor in actors:
        try:
            write_actor_config(actor)
            actor.process = subprocess.Popen(
                [str(executable), "node", "run", "--config", str(actor.config_path)],
                cwd=PROJECT_ROOT,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
        except OSError as error:
            raise ScenarioError(f"Could not start actor {actor.name}: {error}") from error
        threading.Thread(
            target=actor_output_reader,
            args=(actor, output_queue),
            daemon=True,
        ).start()


def process_event(actor: Actor, line: str, display: Display) -> None:
    display.event(actor, line)
    try:
        message = json.loads(line)
    except json.JSONDecodeError:
        return
    if message.get("event") == "ready":
        details = message.get("details", {})
        port = details.get("control_port")
        if isinstance(port, int) and 0 < port <= 65_535:
            actor.control_port = port
            actor.ready = True


def wait_for_ready(
    actors: list[Actor],
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    while not all(actor.ready for actor in actors):
        for actor in actors:
            if actor.process is not None and actor.process.poll() is not None and not actor.ready:
                raise ScenarioError(
                    f"Actor {actor.name} exited before readiness with code "
                    f"{actor.process.returncode}."
                )
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            missing = ", ".join(actor.name for actor in actors if not actor.ready)
            raise ScenarioError(f"Timed out waiting for ready actors: {missing}")
        try:
            actor, line = output_queue.get(timeout=min(0.2, remaining))
        except queue.Empty:
            continue
        process_event(actor, line, display)


def control_request(actor: Actor, request_id: int, method: str, timeout: float) -> Any:
    if actor.control_port is None:
        raise ScenarioError(f"Actor {actor.name} has no control port.")
    request = {
        "id": request_id,
        "method": method,
        "token": actor.token,
    }
    try:
        with socket.create_connection(("127.0.0.1", actor.control_port), timeout) as connection:
            connection.settimeout(timeout)
            connection.sendall((json.dumps(request) + "\n").encode("utf-8"))
            response_bytes = bytearray()
            while not response_bytes.endswith(b"\n"):
                chunk = connection.recv(4096)
                if not chunk:
                    break
                response_bytes.extend(chunk)
                if len(response_bytes) > 64 * 1024:
                    raise ScenarioError(f"Oversized control response from {actor.name}.")
    except OSError as error:
        raise ScenarioError(f"Control request to {actor.name} failed: {error}") from error
    try:
        response = json.loads(response_bytes.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ScenarioError(f"Invalid control response from {actor.name}.") from error
    if response.get("id") != request_id or response.get("ok") is not True:
        raise ScenarioError(f"Control request {method} failed for {actor.name}: {response}")
    return response.get("result")


def actor_selection(value: Any, actors: list[Actor]) -> list[Actor]:
    if value == "all":
        return actors
    if not isinstance(value, list) or any(not isinstance(item, str) for item in value):
        raise ScenarioError("Actor selection must be 'all' or an array of names.")
    by_name = {actor.name: actor for actor in actors}
    try:
        return [by_name[name] for name in value]
    except KeyError as error:
        raise ScenarioError(f"Unknown actor in selection: {error.args[0]}") from error


def run_steps(
    steps: Any,
    actors: list[Actor],
    executable: Path,
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
) -> dict[str, Any]:
    if not isinstance(steps, list):
        raise ScenarioError("Scenario steps must be an array.")
    started = False
    dumps: dict[str, Any] = {}
    request_id = 1
    for step in steps:
        if not isinstance(step, dict):
            raise ScenarioError("Every scenario step must be an object.")
        if step.get("command") == "start_all":
            if started:
                raise ScenarioError("start_all may appear only once.")
            display.controller("Starting actors")
            start_actors(actors, executable, output_queue)
            started = True
        elif step.get("wait") == "all_ready":
            if not started:
                raise ScenarioError("Actors must be started before all_ready.")
            display.controller("Waiting for all actors")
            wait_for_ready(actors, output_queue, display, timeout_seconds)
            display.controller("All actors are ready")
        elif step.get("command") == "dump":
            if not all(actor.ready for actor in actors):
                raise ScenarioError("All actors must be ready before dump.")
            display.controller("Collecting state dumps")
            for actor in actor_selection(step.get("actors"), actors):
                result = control_request(actor, request_id, "dump", timeout_seconds)
                request_id += 1
                dumps[actor.name] = result
                dump_path = actor.directory / "final-dump.json"
                dump_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        else:
            raise ScenarioError(f"Unsupported Stage 7.0 step: {step}")
    if not started:
        raise ScenarioError("Scenario must contain start_all.")
    return dumps


def run_assertions(assertions: Any, dumps: dict[str, Any], actors: list[Actor]) -> None:
    if not isinstance(assertions, list):
        raise ScenarioError("Scenario assertions must be an array.")
    for assertion in assertions:
        if not isinstance(assertion, dict):
            raise ScenarioError("Every assertion must be an object.")
        if assertion.get("all_ready") is True:
            if not all(actor.ready for actor in actors):
                raise ScenarioError("Assertion failed: not all actors are ready.")
        elif "same_tip" in assertion:
            names = assertion["same_tip"]
            if not isinstance(names, list) or len(names) < 2:
                raise ScenarioError("same_tip requires at least two actor names.")
            selected = actor_selection(names, actors)
            try:
                tips = [dumps[actor.name]["chain"]["tip"] for actor in selected]
            except KeyError as error:
                raise ScenarioError(
                    f"same_tip has no full-node dump field: {error.args[0]}"
                ) from error
            if len(set(tips)) != 1:
                raise ScenarioError(f"Assertion failed: chain tips differ: {tips}")
        elif "height" in assertion:
            expected = assertion["height"]
            if not isinstance(expected, dict) or not isinstance(expected.get("value"), int):
                raise ScenarioError("height assertion is invalid.")
            for actor in actor_selection(expected.get("actors"), actors):
                try:
                    actual = dumps[actor.name]["chain"]["height"]
                except KeyError as error:
                    raise ScenarioError(
                        f"height has no full-node dump field for {actor.name}."
                    ) from error
                if actual != expected["value"]:
                    raise ScenarioError(
                        f"Assertion failed: {actor.name} height is {actual}, "
                        f"expected {expected['value']}."
                    )
        elif "mempool_size" in assertion:
            expected = assertion["mempool_size"]
            if not isinstance(expected, int) or expected < 0:
                raise ScenarioError("mempool_size must be a nonnegative integer.")
            full_nodes = [actor for actor in actors if "full_node" in actor.roles]
            for actor in full_nodes:
                try:
                    actual = dumps[actor.name]["chain"]["mempool_size"]
                except KeyError as error:
                    raise ScenarioError(
                        f"mempool_size has no full-node dump field for {actor.name}."
                    ) from error
                if actual != expected:
                    raise ScenarioError(
                        f"Assertion failed: {actor.name} mempool size is {actual}, "
                        f"expected {expected}."
                    )
        else:
            raise ScenarioError(f"Unsupported Stage 7.0 assertion: {assertion}")


def shutdown_actors(actors: Iterable[Actor], timeout_seconds: float) -> None:
    request_id = 1_000_000
    for actor in actors:
        if actor.process is None or actor.process.poll() is not None or not actor.ready:
            continue
        try:
            control_request(actor, request_id, "shutdown", timeout_seconds)
        except ScenarioError:
            pass
        request_id += 1
    deadline = time.monotonic() + timeout_seconds
    for actor in actors:
        if actor.process is None:
            continue
        remaining = max(0.0, deadline - time.monotonic())
        try:
            actor.process.wait(timeout=remaining)
        except subprocess.TimeoutExpired:
            actor.process.terminate()
    for actor in actors:
        if actor.process is None or actor.process.poll() is not None:
            continue
        try:
            actor.process.wait(timeout=2)
        except subprocess.TimeoutExpired:
            actor.process.kill()
            actor.process.wait()


def executable_path(argument: Path | None) -> Path:
    candidate = argument or DEFAULT_EXECUTABLES.get(platform.system())
    if candidate is None:
        raise ScenarioError(f"Unsupported platform: {platform.system()}")
    resolved = candidate.resolve()
    if not resolved.is_file():
        raise ScenarioError(f"BBC executable not found: {resolved}")
    return resolved


def main() -> int:
    arguments = parse_arguments()
    scenario_path = arguments.scenario.resolve()
    document = load_document(scenario_path)
    name = document.get("name")
    if not isinstance(name, str) or not ACTOR_NAME.fullmatch(name):
        raise ScenarioError("Scenario name is invalid.")
    network = document.get("network", {})
    if not isinstance(network, dict):
        raise ScenarioError("Scenario network must be an object.")
    timeout_ms = network.get("step_timeout_ms", 10_000)
    if not isinstance(timeout_ms, int) or timeout_ms <= 0 or timeout_ms > 300_000:
        raise ScenarioError("step_timeout_ms must be between 1 and 300000.")
    timeout_seconds = timeout_ms / 1000

    run_id = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S") + f"-{os.getpid()}"
    run_directory = PROJECT_ROOT / "build" / "scenarios" / name / run_id
    run_directory.mkdir(parents=True)
    actors = create_actors(document, run_directory)
    display = Display(actors, arguments.no_ui)
    output_queue: queue.Queue[tuple[Actor, str]] = queue.Queue()
    success = False
    summary: dict[str, Any] = {"scenario": name, "run_id": run_id, "success": False}
    try:
        (run_directory / "scenario.resolved.json").write_text(
            json.dumps(document, indent=2) + "\n",
            encoding="utf-8",
        )
        dumps = run_steps(
            document.get("steps"),
            actors,
            executable_path(arguments.executable),
            output_queue,
            display,
            timeout_seconds,
        )
        run_assertions(document.get("assertions", []), dumps, actors)
        success = True
        summary["success"] = True
        summary["actors"] = [actor.name for actor in actors]
        display.controller("Scenario passed")
        return 0
    except (ScenarioError, KeyboardInterrupt) as error:
        summary["error"] = str(error) or "Interrupted"
        display.controller(f"Scenario failed: {summary['error']}")
        return 1
    finally:
        shutdown_actors(actors, timeout_seconds)
        summary_path = run_directory / "summary.json"
        summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
        if success and not arguments.keep:
            for actor in actors:
                try:
                    actor.config_path.unlink()
                except FileNotFoundError:
                    pass
        print(f"Artifacts: {run_directory}", flush=True)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except ScenarioError as error:
        print(f"Error: {error}", file=sys.stderr)
        raise SystemExit(1) from error
