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
from concurrent.futures import ThreadPoolExecutor
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
PANEL_EVENT_ROWS = 10


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
    requested_control_port: int
    requested_p2p_port: int | None
    peers: list[str]
    mining_source: str | None
    directory: Path
    config_path: Path
    token: str
    network_profile: str
    process: subprocess.Popen[str] | None = None
    control_port: int | None = None
    p2p_port: int | None = None
    ready: bool = False
    observed_events: set[str] = field(default_factory=set)
    events: deque[str] = field(
        default_factory=lambda: deque(maxlen=PANEL_EVENT_ROWS)
    )


class Display:
    def __init__(self, actors: list[Actor], plain: bool) -> None:
        self._actors = actors
        self._plain = plain or not enable_ansi_output()
        self._controller = "Preparing scenario"

    def event(self, actor: Actor, line: str) -> None:
        event = "message"
        try:
            message = json.loads(line)
            event = message.get("event", "message")
            details = message.get("details", {})
            summary = self._event_summary(event, details)
        except json.JSONDecodeError:
            summary = line
        if (
            event == "mining_progress"
            and actor.events
            and actor.events[-1].startswith("[MINING]")
        ):
            actor.events[-1] = summary
        else:
            actor.events.append(summary)
        if self._plain:
            print(f"[{actor.name}] {summary}", flush=True)
        else:
            self.render()

    @staticmethod
    def _event_summary(event: str, details: Any) -> str:
        if not isinstance(details, dict):
            return f"{event}: {json.dumps(details, separators=(',', ':'))}"

        height = details.get("height", "?")
        block_id = details.get("block_id")
        short_id = f"{block_id[:12]}..." if isinstance(block_id, str) else "?"
        attempts = details.get("attempts")
        encoded_attempts = f"{attempts:,}" if isinstance(attempts, int) else "?"
        if event == "mining_progress":
            return f"[MINING] height={height} attempts={encoded_attempts}"
        if event == "block_found":
            return (
                f"[BLOCK MINED] height={height} id={short_id} "
                f"attempts={encoded_attempts}"
            )
        if event == "block_accepted":
            return (
                f"[BLOCK ACCEPTED] height={height} id={short_id} "
                f"peer={details.get('peer_id', '?')}"
            )
        if event == "block_stored":
            return f"[SIDE BLOCK STORED] height={height} id={short_id}"
        if event == "chain_reorganized":
            return (
                f"[CHAIN REORG] height={height} id={short_id} "
                f"detached={details.get('detached_blocks', '?')} "
                f"attached={details.get('attached_blocks', '?')}"
            )
        if event == "mempool_reorg_completed":
            return (
                "[MEMPOOL REORG] "
                f"detached={details.get('detached_transactions', '?')} "
                f"restored={details.get('restored_transactions', '?')}"
            )
        if event == "sync_block_stored":
            return f"[SYNC SIDE BLOCK] height={height} id={short_id}"
        if event == "block_rejected":
            return f"[BLOCK REJECTED] id={short_id} reason={details.get('reason', '?')}"
        if event == "mining_accepted":
            return f"[MINER WON] height={height} id={short_id}"
        if event == "mining_cancelled":
            return f"[MINER STOPPED] height={height} reason={details.get('reason', '?')}"
        if event == "mining_rejected":
            return f"[MINER REJECTED] height={height} reason={details.get('reason', '?')}"
        return f"{event}: {json.dumps(details, separators=(',', ':'))}"

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
        panel_height = PANEL_EVENT_ROWS + 1
        for actor in self._actors:
            state = "ready" if actor.ready else "starting"
            title = f" {actor.name} [{state}] "
            lines = [title.center(cell_width, "-"), *actor.events]
            while len(lines) < panel_height:
                lines.append("")
            panels.append(
                [line[:cell_width].ljust(cell_width) for line in lines[:panel_height]]
            )

        output = ["\x1b[2J\x1b[H", f"BBC scenario: {self._controller}"[:width], ""]
        for start in range(0, len(panels), columns):
            row = panels[start : start + columns]
            for line_index in range(panel_height):
                output.append(" ".join(panel[line_index] for panel in row))
            output.append("")
        print("\n".join(output), end="", flush=True)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run a local BBC network scenario.")
    parser.add_argument("scenario", type=Path, help="JSON scenario file")
    parser.add_argument("--executable", type=Path, help="path to the built bbc executable")
    parser.add_argument(
        "--regtest",
        action="store_true",
        help="use the fast isolated regtest profile instead of development",
    )
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


def parse_endpoint(value: Any, field_name: str) -> int:
    if not isinstance(value, str):
        raise ScenarioError(f"Actor {field_name} endpoint must be a string.")
    host, separator, encoded_port = value.rpartition(":")
    if separator != ":" or host != "127.0.0.1":
        raise ScenarioError(f"Actor {field_name} endpoint must use 127.0.0.1.")
    try:
        port = int(encoded_port)
    except ValueError as error:
        raise ScenarioError(f"Actor {field_name} port must be an integer.") from error
    if port < 0 or port > 65_535:
        raise ScenarioError(f"Actor {field_name} port must be between 0 and 65535.")
    return port


def create_actors(document: dict[str, Any], run_directory: Path) -> list[Actor]:
    encoded_actors = document.get("actors")
    if not isinstance(encoded_actors, list) or not encoded_actors:
        raise ScenarioError("Scenario must contain at least one actor.")

    network = document.get("network", {})
    profile = network.get("profile") if isinstance(network, dict) else None
    if profile not in {"development", "regtest"}:
        raise ScenarioError("Network profile must be development or regtest.")
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
        control_port = parse_endpoint(encoded.get("control"), "control")
        full_node = "full_node" in roles
        p2p_value = encoded.get("p2p")
        p2p_port = parse_endpoint(p2p_value, "P2P") if full_node else None
        if not full_node and p2p_value is not None:
            raise ScenarioError(f"Actor {name} has P2P without the full_node role.")
        peers = encoded.get("peers", [])
        if (
            not isinstance(peers, list)
            or any(not isinstance(peer, str) for peer in peers)
            or len(set(peers)) != len(peers)
            or (peers and not full_node)
        ):
            raise ScenarioError(f"Actor {name} has invalid peers.")
        mining_source = encoded.get("mining_source")
        if "miner" in roles:
            if not isinstance(mining_source, str):
                raise ScenarioError(f"Miner {name} requires mining_source.")
        elif mining_source is not None:
            raise ScenarioError(f"Actor {name} has mining_source without the miner role.")
        for port in (control_port, p2p_port):
            if port is not None and port != 0 and port in fixed_ports:
                raise ScenarioError(f"Duplicate fixed actor port: {port}")
        names.add(name)
        for port in (control_port, p2p_port):
            if port is not None and port != 0:
                fixed_ports.add(port)
        actor_directory = run_directory / "actors" / name
        actors.append(
            Actor(
                name=name,
                roles=roles,
                requested_control_port=control_port,
                requested_p2p_port=p2p_port,
                peers=peers,
                mining_source=mining_source,
                directory=actor_directory,
                config_path=actor_directory / "actor.generated.json",
                token=secrets.token_hex(32),
                network_profile=profile,
            )
        )
    by_name = {actor.name: actor for actor in actors}
    for actor in actors:
        for peer_name in actor.peers:
            if peer_name == actor.name:
                raise ScenarioError(f"Actor {actor.name} cannot name itself as a peer.")
            peer = by_name.get(peer_name)
            if peer is None:
                raise ScenarioError(f"Actor {actor.name} references unknown peer {peer_name}.")
            if "full_node" not in peer.roles:
                raise ScenarioError(f"Peer {peer_name} is not a full node.")
        if actor.mining_source is not None:
            source = by_name.get(actor.mining_source)
            if source is None or "full_node" not in source.roles:
                raise ScenarioError(
                    f"Mining source {actor.mining_source} is not a full node."
                )
    return actors


def write_actor_config(actor: Actor) -> None:
    actor.directory.mkdir(parents=True, exist_ok=True)
    config = {
        "schema_version": 1,
        "name": actor.name,
        "roles": actor.roles,
        "data_directory": str((actor.directory / "data").resolve()),
        "network": actor.network_profile,
        "control": {
            "host": "127.0.0.1",
            "port": actor.requested_control_port,
            "token": actor.token,
        },
    }
    if actor.requested_p2p_port is not None:
        config["p2p"] = {
            "host": "127.0.0.1",
            "port": actor.requested_p2p_port,
        }
    actor.config_path.write_text(
        json.dumps(config, indent=2) + "\n",
        encoding="utf-8",
    )


def actor_output_reader(actor: Actor, output_queue: queue.Queue[tuple[Actor, str]]) -> None:
    assert actor.process is not None
    assert actor.process.stdout is not None
    log_path = actor.directory / "actor.events.jsonl"
    with log_path.open("a", encoding="utf-8", newline="\n") as log:
        for line in actor.process.stdout:
            value = line.rstrip("\r\n")
            log.write(value + "\n")
            log.flush()
            output_queue.put((actor, value))


def start_actor(
    actor: Actor,
    executable: Path,
    output_queue: queue.Queue[tuple[Actor, str]],
) -> None:
    try:
        write_actor_config(actor)
        actor.process = subprocess.Popen(
            [
                str(executable),
                "node",
                "run",
                "--scenario-config",
                str(actor.config_path),
            ],
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


def start_actors(
    actors: list[Actor],
    executable: Path,
    output_queue: queue.Queue[tuple[Actor, str]],
) -> None:
    for actor in actors:
        start_actor(actor, executable, output_queue)


def restart_actor(
    actor: Actor,
    executable: Path,
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
    request_id: int,
) -> int:
    if actor.process is None or actor.process.poll() is not None or not actor.ready:
        raise ScenarioError(f"Actor {actor.name} is not running.")
    previous_p2p_port = actor.p2p_port
    control_request(actor, request_id, "shutdown", timeout_seconds)
    request_id += 1
    try:
        actor.process.wait(timeout=timeout_seconds)
    except subprocess.TimeoutExpired as error:
        raise ScenarioError(f"Actor {actor.name} did not stop for restart.") from error
    drain_events(output_queue, display)
    actor.ready = False
    actor.control_port = None
    if previous_p2p_port is not None:
        actor.requested_p2p_port = previous_p2p_port
    start_actor(actor, executable, output_queue)
    wait_for_ready([actor], output_queue, display, timeout_seconds)
    return request_id


def process_event(actor: Actor, line: str, display: Display) -> None:
    display.event(actor, line)
    try:
        message = json.loads(line)
    except json.JSONDecodeError:
        return
    event = message.get("event")
    if isinstance(event, str):
        actor.observed_events.add(event)
    if event == "ready":
        details = message.get("details", {})
        port = details.get("control_port")
        if isinstance(port, int) and 0 < port <= 65_535:
            actor.control_port = port
            if "full_node" not in actor.roles:
                actor.ready = True
        p2p_port = details.get("p2p_port")
        if isinstance(p2p_port, int) and 0 < p2p_port <= 65_535:
            actor.p2p_port = p2p_port
        if actor.control_port is not None and (
            "full_node" not in actor.roles or actor.p2p_port is not None
        ):
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


def control_request(
    actor: Actor,
    request_id: int,
    method: str,
    timeout: float,
    params: dict[str, Any] | None = None,
) -> Any:
    if actor.control_port is None:
        raise ScenarioError(f"Actor {actor.name} has no control port.")
    request = {
        "id": request_id,
        "method": method,
        "token": actor.token,
    }
    if params is not None:
        request["params"] = params
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


def drain_events(
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
) -> None:
    while True:
        try:
            actor, line = output_queue.get_nowait()
        except queue.Empty:
            return
        process_event(actor, line, display)


def connect_scenario_peers(
    actors: list[Actor],
    selected: list[Actor],
    timeout_seconds: float,
    request_id: int,
) -> int:
    by_name = {actor.name: actor for actor in actors}
    selected_names = {actor.name for actor in selected}
    for actor in selected:
        for peer_name in actor.peers:
            if peer_name not in selected_names:
                continue
            peer = by_name[peer_name]
            if peer.p2p_port is None:
                raise ScenarioError(f"Peer {peer.name} has no resolved P2P port.")
            control_request(
                actor,
                request_id,
                "connect_peer",
                timeout_seconds,
                {"host": "127.0.0.1", "port": peer.p2p_port},
            )
            request_id += 1
        if actor.mining_source is not None:
            if actor.mining_source not in selected_names:
                continue
            peer = by_name[actor.mining_source]
            if peer.p2p_port is None:
                raise ScenarioError(f"Mining source {peer.name} has no P2P port.")
            control_request(
                actor,
                request_id,
                "connect_peer",
                timeout_seconds,
                {"host": "127.0.0.1", "port": peer.p2p_port},
            )
            request_id += 1
    return request_id


def expected_peer_counts(
    actors: list[Actor],
    selected: list[Actor],
) -> dict[str, int]:
    selected_names = {actor.name for actor in selected}
    neighbors: dict[str, set[str]] = {
        actor.name: set()
        for actor in selected
        if "full_node" in actor.roles or "miner" in actor.roles
    }
    for actor in selected:
        for peer_name in actor.peers:
            if peer_name not in selected_names:
                continue
            neighbors[actor.name].add(peer_name)
            neighbors[peer_name].add(actor.name)
        if actor.mining_source is not None and actor.mining_source in selected_names:
            neighbors[actor.name].add(actor.mining_source)
            neighbors[actor.mining_source].add(actor.name)
    return {name: len(values) for name, values in neighbors.items()}


def wait_for_peer_connections(
    actors: list[Actor],
    selected: list[Actor],
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
) -> None:
    expected = expected_peer_counts(actors, selected)
    by_name = {actor.name: actor for actor in actors}
    deadline = time.monotonic() + timeout_seconds
    request_id = 200_000
    latest: dict[str, int] = {}
    while True:
        drain_events(output_queue, display)
        complete = True
        for name, count in expected.items():
            status = control_request(
                by_name[name], request_id, "status", timeout_seconds
            )
            request_id += 1
            actual = status["p2p"]["handshake_complete_count"]
            latest[name] = actual
            if actual != count:
                complete = False
        if complete:
            return
        if time.monotonic() >= deadline:
            raise ScenarioError(
                f"Timed out waiting for P2P handshakes: {latest}, expected {expected}."
            )
        time.sleep(0.05)


def wait_for_peer_counts(
    expected: dict[str, int],
    actors: list[Actor],
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
) -> None:
    by_name = {actor.name: actor for actor in actors}
    if not expected or any(
        name not in by_name or not isinstance(count, int) or count < 0
        for name, count in expected.items()
    ):
        raise ScenarioError("peer_counts requires known actors and nonnegative counts.")
    deadline = time.monotonic() + timeout_seconds
    request_id = 250_000
    latest: dict[str, int] = {}
    while True:
        drain_events(output_queue, display)
        for name in expected:
            status = control_request(
                by_name[name], request_id, "status", timeout_seconds
            )
            request_id += 1
            latest[name] = status["p2p"]["handshake_complete_count"]
        if latest == expected:
            return
        if time.monotonic() >= deadline:
            raise ScenarioError(
                f"Timed out waiting for peer counts: {latest}, expected {expected}."
            )
        time.sleep(0.05)


def wait_for_pongs(
    expected: dict[str, int],
    actors: list[Actor],
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
) -> None:
    by_name = {actor.name: actor for actor in actors}
    deadline = time.monotonic() + timeout_seconds
    request_id = 300_000
    while True:
        drain_events(output_queue, display)
        missing: list[str] = []
        for name, nonce in expected.items():
            status = control_request(
                by_name[name], request_id, "status", timeout_seconds
            )
            request_id += 1
            peers = status["p2p"]["peers"]
            if not peers or any(peer["last_pong_nonce"] != nonce for peer in peers):
                missing.append(name)
        if not missing:
            return
        if time.monotonic() >= deadline:
            raise ScenarioError(
                f"Timed out waiting for PONG responses from: {', '.join(missing)}."
            )
        time.sleep(0.05)


def wait_for_mining(
    miners: list[Actor],
    full_nodes: list[Actor],
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
    height: int,
) -> str:
    deadline = time.monotonic() + timeout_seconds
    request_id = 400_000
    while True:
        drain_events(output_queue, display)
        miner_states: list[str] = []
        miner_heights: list[int] = []
        heights: list[int] = []
        for actor in miners:
            status = control_request(actor, request_id, "status", timeout_seconds)
            request_id += 1
            miner_states.append(status["mining"]["state"])
            miner_heights.append(status["mining"]["known_height"])
        for actor in full_nodes:
            status = control_request(actor, request_id, "status", timeout_seconds)
            request_id += 1
            heights.append(status["chain"]["height"])
        if heights and all(value == height for value in heights) and all(
            state in {"accepted", "cancelled", "rejected"} for state in miner_states
        ) and all(value == height for value in miner_heights):
            winners = [
                actor.name
                for actor, state in zip(miners, miner_states)
                if state == "accepted"
            ]
            if len(winners) != 1:
                raise ScenarioError(
                    f"Mining round has {len(winners)} winners: {winners}."
                )
            return winners[0]
        if time.monotonic() >= deadline:
            raise ScenarioError(
                "Timed out waiting for mining: "
                f"states={miner_states}, miner_heights={miner_heights}, "
                f"full_node_heights={heights}."
            )
        time.sleep(0.02)


def wait_for_miners_ready(
    miners: list[Actor],
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    request_id = 450_000
    while True:
        drain_events(output_queue, display)
        states: list[str] = []
        for actor in miners:
            status = control_request(actor, request_id, "status", timeout_seconds)
            request_id += 1
            states.append(status["mining"]["state"])
        if states and all(state == "ready" for state in states):
            return
        if time.monotonic() >= deadline:
            raise ScenarioError(
                f"Timed out preparing the mining race: states={states}."
            )
        time.sleep(0.02)


def wait_for_miner_height(
    miners: list[Actor],
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
    height: int,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    request_id = 475_000
    latest: dict[str, int] = {}
    while True:
        drain_events(output_queue, display)
        for actor in miners:
            status = control_request(actor, request_id, "status", timeout_seconds)
            request_id += 1
            latest[actor.name] = status["mining"]["known_height"]
        if latest and all(value == height for value in latest.values()):
            return
        if time.monotonic() >= deadline:
            raise ScenarioError(
                f"Timed out waiting for miner height {height}: {latest}."
            )
        time.sleep(0.02)


def wait_for_transaction_propagation(
    full_nodes: list[Actor],
    sender: Actor,
    expected_transaction_id: str,
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    request_id = 500_000
    while True:
        drain_events(output_queue, display)
        sender_status = control_request(
            sender, request_id, "status", timeout_seconds
        )
        request_id += 1
        sizes: list[int] = []
        for actor in full_nodes:
            status = control_request(actor, request_id, "status", timeout_seconds)
            request_id += 1
            sizes.append(status["chain"]["mempool_size"])
        transaction_status = sender_status.get("transaction", {})
        if (
            transaction_status.get("state") == "accepted"
            and transaction_status.get("id") == expected_transaction_id
            and sizes
            and all(size == 1 for size in sizes)
        ):
            return
        if time.monotonic() >= deadline:
            raise ScenarioError(
                "Timed out waiting for transaction propagation: "
                f"sender={transaction_status}, mempools={sizes}."
            )
        time.sleep(0.02)


def wait_for_sync(
    selected: list[Actor],
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
    height: int,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    request_id = 550_000
    latest: dict[str, Any] = {}
    while True:
        drain_events(output_queue, display)
        complete = True
        for actor in selected:
            status = control_request(actor, request_id, "status", timeout_seconds)
            request_id += 1
            latest[actor.name] = {
                "height": status["chain"]["height"],
                "sync": status["sync"]["state"],
            }
            if status["chain"]["height"] != height or status["sync"]["state"] != "complete":
                complete = False
        if complete:
            return
        if time.monotonic() >= deadline:
            raise ScenarioError(
                f"Timed out waiting for block synchronization: {latest}."
            )
        time.sleep(0.02)


def wait_for_sync_state(
    selected: list[Actor],
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
    height: int,
    expected_state: str,
) -> None:
    deadline = time.monotonic() + timeout_seconds
    request_id = 575_000
    latest: dict[str, Any] = {}
    while True:
        drain_events(output_queue, display)
        for actor in selected:
            status = control_request(actor, request_id, "status", timeout_seconds)
            request_id += 1
            latest[actor.name] = {
                "height": status["chain"]["height"],
                "sync": status["sync"]["state"],
            }
        if latest and all(
            value["height"] == height and value["sync"] == expected_state
            for value in latest.values()
        ):
            return
        if time.monotonic() >= deadline:
            raise ScenarioError(
                f"Timed out waiting for synchronization state {expected_state}: "
                f"{latest}."
            )
        time.sleep(0.02)


def run_steps(
    steps: Any,
    actors: list[Actor],
    executable: Path,
    output_queue: queue.Queue[tuple[Actor, str]],
    display: Display,
    timeout_seconds: float,
) -> tuple[dict[str, Any], dict[str, Any]]:
    if not isinstance(steps, list):
        raise ScenarioError("Scenario steps must be an array.")
    started_names: set[str] = set()
    dumps: dict[str, Any] = {}
    expected_pongs: dict[str, int] = {}
    submitted_transaction: tuple[Actor, str] | None = None
    scenario_state: dict[str, Any] = {
        "mining_winners": [],
        "transactions": [],
    }
    request_id = 1
    for step in steps:
        if not isinstance(step, dict):
            raise ScenarioError("Every scenario step must be an object.")
        if step.get("command") == "start_all":
            if started_names:
                raise ScenarioError("start_all may appear only once.")
            display.controller("Starting actors")
            start_actors(actors, executable, output_queue)
            started_names.update(actor.name for actor in actors)
        elif step.get("command") == "start":
            selected = actor_selection(step.get("actors"), actors)
            duplicates = [actor.name for actor in selected if actor.name in started_names]
            if duplicates:
                raise ScenarioError(f"Actors already started: {', '.join(duplicates)}.")
            display.controller("Starting selected actors")
            start_actors(selected, executable, output_queue)
            started_names.update(actor.name for actor in selected)
        elif step.get("wait") == "all_ready":
            if len(started_names) != len(actors):
                raise ScenarioError("Actors must be started before all_ready.")
            display.controller("Waiting for all actors")
            wait_for_ready(actors, output_queue, display, timeout_seconds)
            display.controller("All actors are ready")
        elif step.get("wait") == "actors_ready":
            selected = actor_selection(step.get("actors"), actors)
            if any(actor.name not in started_names for actor in selected):
                raise ScenarioError("Selected actors must be started before actors_ready.")
            display.controller("Waiting for selected actors")
            wait_for_ready(selected, output_queue, display, timeout_seconds)
            display.controller("Selected actors are ready")
        elif step.get("command") == "connect_all":
            selected = actor_selection(step.get("actors", "all"), actors)
            if not all(actor.ready for actor in selected):
                raise ScenarioError("Selected actors must be ready before connect_all.")
            display.controller("Connecting scenario peers")
            request_id = connect_scenario_peers(
                actors, selected, timeout_seconds, request_id
            )
        elif step.get("command") in {"connect", "disconnect"}:
            source_name = step.get("from")
            target_name = step.get("to")
            if not isinstance(source_name, str) or not isinstance(target_name, str):
                raise ScenarioError("connect and disconnect require from and to actors.")
            source = actor_selection([source_name], actors)[0]
            target = actor_selection([target_name], actors)[0]
            if not source.ready or not target.ready or target.p2p_port is None:
                raise ScenarioError("Both link actors must be ready full-node peers.")
            method = (
                "connect_peer" if step["command"] == "connect" else "disconnect_peer"
            )
            display.controller(
                f"{step['command'].capitalize()}ing {source.name} and {target.name}"
            )
            control_request(
                source,
                request_id,
                method,
                timeout_seconds,
                {"host": "127.0.0.1", "port": target.p2p_port},
            )
            request_id += 1
        elif step.get("wait") == "full_nodes_connected":
            selected = actor_selection(step.get("actors", "all"), actors)
            display.controller("Waiting for P2P handshakes")
            wait_for_peer_connections(
                actors, selected, output_queue, display, timeout_seconds
            )
            display.controller("Full nodes are connected")
        elif step.get("wait") == "peer_counts":
            expected = step.get("values")
            if not isinstance(expected, dict):
                raise ScenarioError("peer_counts requires a values object.")
            display.controller("Waiting for requested P2P topology")
            wait_for_peer_counts(
                expected, actors, output_queue, display, timeout_seconds
            )
            display.controller("Requested P2P topology is active")
        elif step.get("command") == "ping":
            selected = actor_selection(step.get("actors"), actors)
            display.controller("Sending PING messages")
            expected_pongs.clear()
            for actor in selected:
                result = control_request(
                    actor, request_id, "ping", timeout_seconds
                )
                request_id += 1
                expected_pongs[actor.name] = result["nonce"]
        elif step.get("wait") == "pongs":
            if not expected_pongs:
                raise ScenarioError("PING must be sent before waiting for PONG.")
            display.controller("Waiting for PONG responses")
            wait_for_pongs(
                expected_pongs, actors, output_queue, display, timeout_seconds
            )
            display.controller("All PONG responses received")
        elif step.get("command") == "start_mining":
            selected = actor_selection(step.get("actors"), actors)
            if any("miner" not in actor.roles for actor in selected):
                raise ScenarioError("start_mining may target only miners.")
            display.controller("Preparing mining race")
            with ThreadPoolExecutor(max_workers=len(selected)) as executor:
                futures = [
                    executor.submit(
                        control_request,
                        actor,
                        request_id + index,
                        "start_mining",
                        timeout_seconds,
                        {"defer_work": True},
                    )
                    for index, actor in enumerate(selected)
                ]
                for future in futures:
                    future.result()
            request_id += len(selected)
            wait_for_miners_ready(
                selected, output_queue, display, timeout_seconds
            )
            start_at_unix_ms = int(time.time() * 1000) + 250
            display.controller("Starting mining race")
            with ThreadPoolExecutor(max_workers=len(selected)) as executor:
                futures = [
                    executor.submit(
                        control_request,
                        actor,
                        request_id + index,
                        "begin_mining",
                        timeout_seconds,
                        {"start_at_unix_ms": start_at_unix_ms},
                    )
                    for index, actor in enumerate(selected)
                ]
                for future in futures:
                    future.result()
            request_id += len(selected)
        elif step.get("wait") == "miner_height":
            selected = actor_selection(step.get("actors"), actors)
            height = step.get("height")
            if (
                not selected
                or any("miner" not in actor.roles for actor in selected)
                or not isinstance(height, int)
                or height < 0
            ):
                raise ScenarioError("miner_height has invalid parameters.")
            display.controller(f"Waiting for miners to learn height {height}")
            wait_for_miner_height(
                selected, output_queue, display, timeout_seconds, height
            )
            display.controller(f"Selected miners know height {height}")
        elif step.get("wait") == "mining_complete":
            height = step.get("height")
            if not isinstance(height, int) or height < 1:
                raise ScenarioError("mining_complete requires a positive height.")
            selected_miners = actor_selection(
                step.get("miners", [
                    actor.name for actor in actors
                    if actor.ready and "miner" in actor.roles
                ]),
                actors,
            )
            selected_full_nodes = actor_selection(
                step.get("full_nodes", [
                    actor.name for actor in actors
                    if actor.ready and "full_node" in actor.roles
                ]),
                actors,
            )
            if (
                not selected_miners
                or not selected_full_nodes
                or any("miner" not in actor.roles for actor in selected_miners)
                or any("full_node" not in actor.roles for actor in selected_full_nodes)
            ):
                raise ScenarioError("mining_complete has invalid actor selections.")
            display.controller(f"Waiting for block {height}")
            winner = wait_for_mining(
                selected_miners,
                selected_full_nodes,
                output_queue,
                display,
                timeout_seconds,
                height,
            )
            scenario_state["mining_winners"].append(winner)
            display.controller(f"Block {height} accepted; losing miners stopped")
        elif step.get("command") == "submit_transaction":
            sender_name = step.get("from")
            if sender_name == "mining_winner":
                miners = actor_selection(step.get("miners"), actors)
                winners: list[Actor] = []
                for actor in miners:
                    status = control_request(
                        actor, request_id, "status", timeout_seconds
                    )
                    request_id += 1
                    if status["mining"]["state"] == "accepted":
                        winners.append(actor)
                if len(winners) != 1:
                    raise ScenarioError("Could not resolve one mining winner.")
                sender = winners[0]
            elif isinstance(sender_name, str):
                sender = actor_selection([sender_name], actors)[0]
            else:
                raise ScenarioError("submit_transaction requires a sender.")
            recipient_name = step.get("to")
            if not isinstance(recipient_name, str):
                raise ScenarioError("submit_transaction requires a recipient actor.")
            recipient = actor_selection([recipient_name], actors)[0]
            recipient_status = control_request(
                recipient, request_id, "status", timeout_seconds
            )
            request_id += 1
            recipient_address = recipient_status.get("wallet_address")
            if not isinstance(recipient_address, str):
                raise ScenarioError(f"Actor {recipient.name} has no wallet address.")
            amount = step.get("amount")
            fee = step.get("fee")
            nonce = step.get("nonce")
            if any(not isinstance(value, int) or value < 0 for value in (amount, fee, nonce)):
                raise ScenarioError("submit_transaction has invalid numeric fields.")
            display.controller(f"Submitting transaction from {sender.name}")
            result = control_request(
                sender,
                request_id,
                "submit_transaction",
                timeout_seconds,
                {
                    "recipient": recipient_address,
                    "amount": amount,
                    "fee": fee,
                    "nonce": nonce,
                },
            )
            request_id += 1
            submitted_transaction = (sender, result["transaction_id"])
            scenario_state["transactions"].append({
                "id": result["transaction_id"],
                "sender": sender.name,
                "recipient": recipient.name,
                "amount": amount,
                "fee": fee,
                "nonce": nonce,
            })
        elif step.get("wait") == "transaction_propagated":
            if submitted_transaction is None:
                raise ScenarioError("No transaction was submitted.")
            display.controller("Waiting for transaction propagation")
            selected_full_nodes = actor_selection(
                step.get("actors", [
                    actor.name for actor in actors
                    if actor.ready and "full_node" in actor.roles
                ]),
                actors,
            )
            if any("full_node" not in actor.roles for actor in selected_full_nodes):
                raise ScenarioError("transaction_propagated may target only full nodes.")
            wait_for_transaction_propagation(
                selected_full_nodes,
                submitted_transaction[0],
                submitted_transaction[1],
                output_queue,
                display,
                timeout_seconds,
            )
            display.controller("Transaction reached every full-node mempool")
        elif step.get("wait") == "sync_complete":
            selected = actor_selection(step.get("actors"), actors)
            if any("full_node" not in actor.roles for actor in selected):
                raise ScenarioError("sync_complete may target only full nodes.")
            height = step.get("height")
            if not isinstance(height, int) or height < 0:
                raise ScenarioError("sync_complete requires a nonnegative height.")
            display.controller(f"Waiting for synchronization to height {height}")
            wait_for_sync(
                selected, output_queue, display, timeout_seconds, height
            )
            display.controller("Selected full nodes are synchronized")
        elif step.get("wait") == "sync_state":
            selected = actor_selection(step.get("actors"), actors)
            height = step.get("height")
            state = step.get("state")
            if (
                any("full_node" not in actor.roles for actor in selected)
                or not isinstance(height, int)
                or height < 0
                or not isinstance(state, str)
            ):
                raise ScenarioError("sync_state has invalid parameters.")
            display.controller(f"Waiting for synchronization state {state}")
            wait_for_sync_state(
                selected,
                output_queue,
                display,
                timeout_seconds,
                height,
                state,
            )
            display.controller(f"Selected full nodes report {state}")
        elif step.get("command") == "restart":
            name = step.get("actor")
            if not isinstance(name, str):
                raise ScenarioError("restart requires an actor name.")
            actor = actor_selection([name], actors)[0]
            display.controller(f"Restarting {actor.name}")
            request_id = restart_actor(
                actor,
                executable,
                output_queue,
                display,
                timeout_seconds,
                request_id,
            )
            display.controller(f"{actor.name} restarted")
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
            raise ScenarioError(f"Unsupported scenario step: {step}")
    if len(started_names) != len(actors):
        missing = sorted({actor.name for actor in actors} - started_names)
        raise ScenarioError(f"Scenario did not start actors: {', '.join(missing)}.")
    return dumps, scenario_state


def run_assertions(
    assertions: Any,
    dumps: dict[str, Any],
    actors: list[Actor],
    scenario_state: dict[str, Any],
) -> None:
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
        elif "same_state" in assertion:
            selected = actor_selection(assertion["same_state"], actors)
            states = [
                dumps[actor.name]["chain"]["accounts"]
                for actor in selected
            ]
            if not states or any(value != states[0] for value in states[1:]):
                raise ScenarioError("Assertion failed: account states differ.")
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
        elif "stored_block_count" in assertion:
            expected = assertion["stored_block_count"]
            if not isinstance(expected, dict) or not isinstance(expected.get("value"), int):
                raise ScenarioError("stored_block_count assertion is invalid.")
            for actor in actor_selection(expected.get("actors"), actors):
                actual = dumps[actor.name]["chain"]["stored_block_count"]
                if actual != expected["value"]:
                    raise ScenarioError(
                        f"Assertion failed: {actor.name} stores {actual} blocks, "
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
        elif "tip_transaction_count" in assertion:
            expected = assertion["tip_transaction_count"]
            if not isinstance(expected, dict) or not isinstance(expected.get("value"), int):
                raise ScenarioError("tip_transaction_count assertion is invalid.")
            for actor in actor_selection(expected.get("actors"), actors):
                actual = dumps[actor.name]["chain"]["tip_transaction_count"]
                if actual != expected["value"]:
                    raise ScenarioError(
                        f"Assertion failed: {actor.name} tip contains {actual} "
                        f"transactions, expected {expected['value']}."
                    )
        elif "same_mempool" in assertion:
            selected = actor_selection(assertion["same_mempool"], actors)
            mempools = [
                dumps[actor.name]["chain"]["mempool_transactions"]
                for actor in selected
            ]
            if not mempools or any(value != mempools[0] for value in mempools[1:]):
                raise ScenarioError(f"Full-node mempools differ: {mempools}.")
        elif "last_transaction_in_mempool" in assertion:
            selected = actor_selection(
                assertion["last_transaction_in_mempool"], actors
            )
            transactions = scenario_state["transactions"]
            if not transactions:
                raise ScenarioError("No submitted transaction is available.")
            transaction_id = transactions[-1]["id"]
            for actor in selected:
                pending = dumps[actor.name]["chain"]["mempool_transactions"]
                if transaction_id not in pending:
                    raise ScenarioError(
                        f"Transaction {transaction_id} is absent from {actor.name} mempool."
                    )
        elif "account" in assertion:
            expected = assertion["account"]
            if (
                not isinstance(expected, dict)
                or not isinstance(expected.get("wallet"), str)
                or not isinstance(expected.get("balance"), int)
                or not isinstance(expected.get("next_nonce"), int)
            ):
                raise ScenarioError("account assertion is invalid.")
            wallet_actor = actor_selection([expected["wallet"]], actors)[0]
            address = dumps[wallet_actor.name].get("wallet_address")
            if not isinstance(address, str):
                raise ScenarioError(f"Actor {wallet_actor.name} has no wallet address.")
            for full_node in actor_selection(expected.get("full_nodes"), actors):
                accounts = {
                    account["address"]: account
                    for account in dumps[full_node.name]["chain"]["accounts"]
                }
                actual = accounts.get(address, {"balance": 0, "next_nonce": 0})
                if (
                    actual["balance"] != expected["balance"]
                    or actual["next_nonce"] != expected["next_nonce"]
                ):
                    raise ScenarioError(
                        f"Assertion failed: {full_node.name} account for "
                        f"{wallet_actor.name} is {actual}."
                    )
        elif "event_seen" in assertion:
            expected = assertion["event_seen"]
            if (
                not isinstance(expected, dict)
                or not isinstance(expected.get("actor"), str)
                or not isinstance(expected.get("event"), str)
            ):
                raise ScenarioError("event_seen assertion is invalid.")
            actor = actor_selection([expected["actor"]], actors)[0]
            if expected["event"] not in actor.observed_events:
                raise ScenarioError(
                    f"Actor {actor.name} did not emit {expected['event']}."
                )
        elif "peer_count" in assertion:
            expected = assertion["peer_count"]
            if (
                not isinstance(expected, dict)
                or not isinstance(expected.get("actor"), str)
                or not isinstance(expected.get("value"), int)
            ):
                raise ScenarioError("peer_count assertion is invalid.")
            actor = actor_selection([expected["actor"]], actors)[0]
            try:
                actual = dumps[actor.name]["p2p"]["handshake_complete_count"]
            except KeyError as error:
                raise ScenarioError(
                    f"peer_count has no P2P dump field for {actor.name}."
                ) from error
            if actual != expected["value"]:
                raise ScenarioError(
                    f"Assertion failed: {actor.name} has {actual} peers, "
                    f"expected {expected['value']}."
                )
        elif "sync_state" in assertion:
            expected = assertion["sync_state"]
            if (
                not isinstance(expected, dict)
                or not isinstance(expected.get("actor"), str)
                or not isinstance(expected.get("value"), str)
            ):
                raise ScenarioError("sync_state assertion is invalid.")
            actor = actor_selection([expected["actor"]], actors)[0]
            try:
                actual = dumps[actor.name]["sync"]
            except KeyError as error:
                raise ScenarioError(
                    f"sync_state has no sync dump field for {actor.name}."
                ) from error
            if actual["state"] != expected["value"]:
                raise ScenarioError(
                    f"Assertion failed: {actor.name} sync state is "
                    f"{actual['state']}, expected {expected['value']}."
                )
            received_blocks = expected.get("received_blocks")
            if received_blocks is not None and (
                not isinstance(received_blocks, int)
                or actual["received_blocks"] != received_blocks
            ):
                raise ScenarioError(
                    f"Assertion failed: {actor.name} received "
                    f"{actual['received_blocks']} sync blocks, expected "
                    f"{received_blocks}."
                )
        elif "mining_outcome" in assertion:
            expected = assertion["mining_outcome"]
            selected = actor_selection(expected.get("actors"), actors)
            states = [dumps[actor.name]["mining"]["state"] for actor in selected]
            if states.count("accepted") != 1 or states.count("cancelled") != len(states) - 1:
                raise ScenarioError(f"Unexpected mining outcome: {states}.")
        elif "winner_reward" in assertion:
            expected = assertion["winner_reward"]
            miners = actor_selection(expected.get("miners"), actors)
            full_node = actor_selection([expected.get("full_node")], actors)[0]
            amount = expected.get("amount")
            winners = [
                actor for actor in miners
                if dumps[actor.name]["mining"]["state"] == "accepted"
            ]
            if len(winners) != 1 or not isinstance(amount, int):
                raise ScenarioError("winner_reward has an invalid winner or amount.")
            winner_address = dumps[winners[0].name]["wallet_address"]
            balances = {
                account["address"]: account["balance"]
                for account in dumps[full_node.name]["chain"]["accounts"]
            }
            if balances.get(winner_address) != amount:
                raise ScenarioError(
                    f"Winner {winners[0].name} has balance "
                    f"{balances.get(winner_address)}, expected {amount}."
                )
        elif "payment_confirmed" in assertion:
            expected = assertion["payment_confirmed"]
            if not isinstance(expected, dict):
                raise ScenarioError("payment_confirmed assertion is invalid.")
            full_nodes = actor_selection(expected.get("full_nodes"), actors)
            recipient = actor_selection([expected.get("recipient")], actors)[0]
            amount = expected.get("amount")
            fee = expected.get("fee")
            reward = expected.get("block_reward")
            winners = scenario_state["mining_winners"]
            transactions = scenario_state["transactions"]
            if (
                len(winners) < 2
                or len(transactions) != 1
                or any(not isinstance(value, int) or value < 0
                       for value in (amount, fee, reward))
            ):
                raise ScenarioError("payment_confirmed has incomplete scenario state.")
            transaction = transactions[0]
            if (
                transaction["sender"] != winners[0]
                or transaction["recipient"] != recipient.name
                or transaction["amount"] != amount
                or transaction["fee"] != fee
                or transaction["nonce"] != 0
            ):
                raise ScenarioError("Confirmed payment does not match the scenario.")

            by_name = {actor.name: actor for actor in actors}
            first_miner = by_name[winners[0]]
            second_miner = by_name[winners[1]]
            involved = {*winners, recipient.name}
            addresses = {
                name: dumps[name]["wallet_address"]
                for name in involved
            }
            expected_balances: dict[str, int] = {}
            for winner_name in winners:
                address = addresses[winner_name]
                expected_balances[address] = expected_balances.get(address, 0) + reward
            sender_address = addresses[first_miner.name]
            recipient_address = addresses[recipient.name]
            expected_balances[sender_address] -= amount + fee
            expected_balances[recipient_address] = (
                expected_balances.get(recipient_address, 0) + amount
            )
            second_miner_address = addresses[second_miner.name]
            expected_balances[second_miner_address] = (
                expected_balances.get(second_miner_address, 0) + fee
            )

            for full_node in full_nodes:
                accounts = {
                    account["address"]: account
                    for account in dumps[full_node.name]["chain"]["accounts"]
                }
                for address, expected_balance in expected_balances.items():
                    actual = accounts.get(address, {"balance": 0})["balance"]
                    if actual != expected_balance:
                        raise ScenarioError(
                            f"{full_node.name} balance for {address} is {actual}, "
                            f"expected {expected_balance}."
                        )
                sender_nonce = accounts[sender_address]["next_nonce"]
                if sender_nonce != 1:
                    raise ScenarioError(
                        f"{full_node.name} sender nonce is {sender_nonce}, expected 1."
                    )
                total_supply = sum(account["balance"] for account in accounts.values())
                expected_supply = reward * len(winners)
                if total_supply != expected_supply:
                    raise ScenarioError(
                        f"{full_node.name} supply is {total_supply}, "
                        f"expected {expected_supply}."
                    )
        else:
            raise ScenarioError(f"Unsupported scenario assertion: {assertion}")


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
    network_profile = "regtest" if arguments.regtest else "development"
    network["profile"] = network_profile
    document["network"] = network
    default_timeout_ms = 15_000 if arguments.regtest else 300_000
    timeout_ms = network.get("step_timeout_ms", default_timeout_ms)
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
    summary: dict[str, Any] = {
        "scenario": name,
        "network": network_profile,
        "run_id": run_id,
        "success": False,
    }
    try:
        (run_directory / "scenario.resolved.json").write_text(
            json.dumps(document, indent=2) + "\n",
            encoding="utf-8",
        )
        dumps, scenario_state = run_steps(
            document.get("steps"),
            actors,
            executable_path(arguments.executable),
            output_queue,
            display,
            timeout_seconds,
        )
        run_assertions(
            document.get("assertions", []), dumps, actors, scenario_state
        )
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
