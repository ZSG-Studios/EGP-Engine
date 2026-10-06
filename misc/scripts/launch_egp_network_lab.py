"""Run encrypted local clients against a dedicated server or visible listen host."""

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import signal
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PRESETS = {"local": (0, 0, 0), "wifi": (40, 10, 1), "wan": (100, 25, 3), "poor": (200, 60, 10)}


def visible_windows(process_ids):
    """Read visible top-level windows for owned Windows processes without focus/input."""
    import ctypes
    from ctypes import wintypes

    user32 = ctypes.WinDLL("user32", use_last_error=True)
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows.argtypes = [callback_type, wintypes.LPARAM]
    user32.EnumWindows.restype = wintypes.BOOL
    user32.IsWindowVisible.argtypes = [wintypes.HWND]
    user32.IsWindowVisible.restype = wintypes.BOOL
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    found = {}

    @callback_type
    def visit(window, _):
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(window, ctypes.byref(pid))
        if pid.value in process_ids and user32.IsWindowVisible(window):
            title = ctypes.create_unicode_buffer(512)
            user32.GetWindowTextW(window, title, len(title))
            found[pid.value] = {"pid": pid.value, "handle": int(window), "title": title.value}
        return True

    if not user32.EnumWindows(visit, 0):
        raise ctypes.WinError(ctypes.get_last_error())
    return found


def bounded_float(low, high):
    def parse(value):
        number = float(value)
        if not math.isfinite(number) or not low <= number <= high:
            raise argparse.ArgumentTypeError(f"Expected a finite number from {low} to {high}")
        return number

    return parse


def restart_failure(receipt):
    """Require independent process evidence, not just a successful result marker."""
    restart = receipt["server_restart"]
    checkpoint = restart.get("checkpoint", {})
    clients = receipt["clients"]
    original, replacement = restart.get("initial_pid"), restart.get("replacement_pid")
    if not original or not replacement or original == replacement or checkpoint.get("pid") != original:
        return "Server replacement identities do not match the checkpoint"
    if (
        checkpoint.get("generation") != 1
        or checkpoint.get("admitted_clients") != clients
        or checkpoint.get("input_clients") != clients
        or checkpoint.get("root_authority") != -1
        or checkpoint.get("tick", 0) < 2
    ):
        return "Initial checkpoint lacks authenticated ownership and input evidence"
    if restart.get("replacement_listener", {}).get("port") != receipt["listener"]["port"]:
        return "Replacement endpoint changed"
    records = receipt["processes"]
    peers = [record for record in records if record["role"] == "client"]
    if len(peers) != clients or {record["result"].get("index") for record in peers} != set(range(clients)):
        return "Missing or duplicate client process evidence"
    value = checkpoint.get("persistent_value")
    if not isinstance(value, int) or value < 100:
        return "Invalid application checkpoint value"
    for record in peers:
        result = record["result"]
        if (
            not result.get("passed")
            or record["exit_code"] != 0
            or result.get("generation") != 2
            or not result.get("disconnected")
            or not result.get("cleared_on_disconnect")
            or result.get("epoch_ticks", {}).get("1", 0) < 2
            or result.get("epoch_ticks", {}).get("2", 0) < 2
            or result.get("epoch_inputs", {}).get("1") is not True
            or result.get("epoch_inputs", {}).get("2") is not True
            or result.get("restored_value") != value
            or result.get("persistent_value", 0) <= value
            or result.get("epoch_server_pids", {}).get("1") != original
            or result.get("epoch_server_pids", {}).get("2") != replacement
        ):
            return "Client lost disconnect, cleared entities, ownership input or restored authority/state evidence"
        states = result.get("connection_states", [])
        if "Disconnected" not in states:
            return "Client did not observe transport disconnection"
        split = states.index("Disconnected")
        if "Connected" not in states[:split] or "Connected" not in states[split + 1 :]:
            return "Client did not connect on both sides of the outage"
    servers = [record for record in records if record["role"] == "server"]
    if len(servers) != 2 or {record["pid"] for record in servers} != {original, replacement}:
        return "Missing original or replacement server process"
    restored = next(record for record in servers if record["pid"] == replacement)
    result = restored["result"]
    if (
        restored["exit_code"] != 0
        or not result.get("passed")
        or result.get("generation") != 2
        or result.get("input_clients") != clients
        or result.get("admitted_clients") != clients
        or result.get("restored_value") != value
        or result.get("persistent_value") != value + clients * (clients + 1) // 2
    ):
        return "Replacement server did not resume all owner-authorized inputs and checkpoint state"
    return None


def stall_failure(receipt):
    """Verify every rejected gap and fresh authority in the original processes."""
    fault = receipt["client_stall"]
    count = fault["count"]
    records = receipt["processes"]
    servers = [r for r in records if r["role"] in ("server", "host")]
    peers = [r for r in records if r["role"] == "client"]
    if (
        len(servers) != 1
        or len(peers) != receipt["clients"]
        or {r["result"].get("index") for r in peers} != set(range(receipt["clients"]))
    ):
        return "Stall test lost its persistent server/client processes"
    server = servers[0]
    if server["exit_code"] != 0 or not server["result"].get("passed"):
        return "Authoritative server failed during client recovery"
    target = next(r for r in peers if r["result"]["index"] == fault["index"])
    result = target["result"]
    proofs = result.get("stall_proofs", [])
    states = result.get("connection_states", [])
    if (
        not result.get("passed")
        or target["exit_code"] != 0
        or len(proofs) != count
        or result.get("stall_proof") != proofs[-1]
        or result.get("generation") != count + 1
        or result.get("replies") != count + 1
        or result.get("diagnostics")
        != ["Fixed simulation exceeded its catch-up budget; resynchronization required."] * count
        or states.count("Connected") != count + 1
        or states[-1:] != ["Stopped"]
        or states[:-1].count("Stopped") != count
        or "Disconnected" in states
    ):
        return "Stalled client lacks the requested number of rejected gaps and recovered connections"
    for i, proof in enumerate(proofs):
        old, new = i + 1, i + 2
        if (
            proof.get("elapsed_ms", 0) < fault["milliseconds"]
            or proof.get("elapsed_ms", 0) <= 500
            or proof.get("injected_at_ms", 0) < (fault["at_seconds"] + i * fault["interval_seconds"]) * 1000
            or proof.get("old_generation") != old
            or proof.get("new_generation") != new
            or proof.get("poll_error") != 1
            or proof.get("state_after_failure") != "Stopped"
            or proof.get("entities_after_failure") != 0
            or proof.get("tick_after_failure") != 0
            or proof.get("input_after_failure") != 3
            or proof.get("stale_input_enqueued") is not True
            or proof.get("input_acknowledged") is not True
            or not proof.get("old_peer")
            or proof.get("old_peer") == proof.get("new_peer")
            or not proof.get("old_entity")
            or proof.get("old_entity") == proof.get("new_entity")
            or result.get("epoch_inputs", {}).get(str(old)) is not True
            or result.get("epoch_inputs", {}).get(str(new)) is not True
            or result.get("epoch_ticks", {}).get(str(new), 0) <= proof.get("before_tick", 0)
            or proof.get("recovered_tick", 0) <= proof.get("before_tick", 0)
            or proof.get("recovered_value", 0) <= proof.get("before_value", 0)
            or any(result.get("epoch_server_pids", {}).get(str(epoch)) != server["pid"] for epoch in (old, new))
        ):
            return (
                f"Stall {old} lacks rejected poll, cleared state, fresh peer/owner or authoritative recovery evidence"
            )
        if i and (
            proof["old_peer"] != proofs[i - 1]["new_peer"]
            or proof["old_entity"] != proofs[i - 1]["new_entity"]
            or proof["before_tick"] < proofs[i - 1]["recovered_tick"]
            or proof["before_value"] < proofs[i - 1]["recovered_value"]
        ):
            return "Consecutive recovery identities or authoritative progress do not form one continuous client history"
    if (
        len({proof["old_peer"] for proof in proofs} | {proofs[-1]["new_peer"]}) != count + 1
        or len({proof["old_entity"] for proof in proofs} | {proofs[-1]["new_entity"]}) != count + 1
    ):
        return "Repeated recovery reused a retired peer or entity identity"
    if [state for state in states[:-1] if state in ("Connected", "Stopped")] != ["Connected", "Stopped"] * count + [
        "Connected"
    ]:
        return "Stalled client connection/stop history does not match the requested recovery sequence"
    authoritative = server["result"]
    history_peers = {str(proof["old_peer"]) for proof in proofs} | {str(proofs[-1]["new_peer"])}
    if set(authoritative.get("peer_generations", {}).get(str(fault["index"]), {})) != history_peers:
        return "Client recovery peer history does not match authoritative server admissions"
    expected = 100 + receipt["clients"] * (receipt["clients"] + 1) // 2 + count * (fault["index"] + 1)
    if authoritative.get("persistent_value") != expected or result.get("persistent_value") != expected:
        return "Server/client counter includes missing or extra owner-authorized input"
    for peer in peers:
        data = peer["result"]
        index = data.get("index")
        expected_generations = count + 1 if index == fault["index"] else 1
        if (
            not data.get("passed")
            or peer["exit_code"] != 0
            or data.get("highest_tick", 0) <= proofs[-1]["before_tick"]
            or data.get("persistent_value") != expected
            or len(authoritative.get("peer_generations", {}).get(str(index), {})) != expected_generations
            or set(authoritative.get("input_generations", {}).get(str(index), {}))
            != {str(i) for i in range(1, expected_generations + 1)}
            or (
                index != fault["index"]
                and (
                    data.get("diagnostics")
                    or data.get("generation") != 1
                    or data.get("replies") != 1
                    or data.get("connection_states", []).count("Connected") != 1
                    or "Stopped" in data.get("connection_states", [])[:-1]
                    or "Disconnected" in data.get("connection_states", [])
                )
            )
        ):
            return "Healthy-client progress or per-connection ownership input evidence failed"
    return None


def server_stall_failure(receipt):
    """Require stopped authority, fresh admission and restoration in the same server PID."""
    fault = receipt["server_stall"]
    clients = receipt["clients"]
    records = receipt["processes"]
    servers = [record for record in records if record["role"] in ("server", "host")]
    peers = [record for record in records if record["role"] == "client"]
    if (
        len(servers) != 1
        or len(peers) != clients
        or {record["result"].get("index") for record in peers} != set(range(clients))
        or len({record["pid"] for record in records}) != clients + 1
    ):
        return "Server stall lost its original server/client processes"
    server = servers[0]
    result = server["result"]
    proof = result.get("server_stall_proof", {})
    checkpoint = proof.get("checkpoint", {})
    initial = 100 + clients * (clients + 1) // 2
    final = initial + clients * (clients + 1) // 2
    if (
        server["exit_code"] != 0
        or not result.get("passed")
        or checkpoint.get("pid") != server["pid"]
        or checkpoint.get("generation") != 1
        or checkpoint.get("admitted_clients") != clients
        or checkpoint.get("input_clients") != clients
        or checkpoint.get("root_authority") != -1
        or checkpoint.get("tick", 0) < 2
        or checkpoint.get("persistent_value") != initial
        or result.get("generation") != 2
        or result.get("admitted_clients") != clients
        or result.get("input_clients") != clients
        or result.get("restored_value") != initial
        or result.get("persistent_value") != final
    ):
        return (
            "Server did not restore its authenticated application checkpoint and accept exactly one new input per owner"
        )
    if (
        result.get("diagnostics") != ["Fixed simulation exceeded its catch-up budget; resynchronization required."]
        or proof.get("elapsed_ms", 0) < fault["milliseconds"]
        or proof.get("injected_at_ms", 0) < fault["at_seconds"] * 1000
        or proof.get("poll_error") != 1
        or proof.get("state_after_failure") != "Stopped"
        or proof.get("entities_after_failure") != 0
        or proof.get("peers_after_failure") != 0
        or proof.get("tick_after_failure") != 0
        or proof.get("spawn_after_failure") != 0
        or proof.get("update_after_failure") != 4
        or not proof.get("old_root")
        or not proof.get("new_root")
        or proof["old_root"] == proof["new_root"]
        or proof.get("port") != receipt["listener"]["port"]
        or proof.get("recovered_port") != receipt["listener"]["port"]
        or result.get("connection_states") != ["Listening", "Stopped", "Listening", "Stopped"]
    ):
        return "Server clock failure did not reject work, clear authority or resume on the same endpoint"
    retired = proof.get("retired_admission", {})
    states = retired.get("states", [])
    if (
        "Connecting" not in states
        or states[-1:] != ["Disconnected"]
        or any(state in states for state in ("Connected", "Synchronizing"))
        or retired.get("entities") != 0
        or retired.get("peers") != 0
    ):
        return "Retired admission was not rejected by the recovered secure listener"
    old_entities = {proof["old_root"]}
    new_entities = {proof["new_root"]}
    for record in peers:
        data = record["result"]
        index = str(data.get("index"))
        owners = data.get("epoch_owners", {})
        old, new = owners.get("1", {}), owners.get("2", {})
        history = result.get("peer_generations", {}).get(index, {})
        connection = data.get("connection_states", [])
        if (
            record["exit_code"] != 0
            or not data.get("passed")
            or data.get("diagnostics")
            or data.get("generation") != 2
            or data.get("replies") != 2
            or not data.get("disconnected")
            or not data.get("cleared_on_disconnect")
            or data.get("epoch_inputs") != {"1": True, "2": True}
            or data.get("epoch_ticks", {}).get("1", 0) < 2
            or data.get("epoch_ticks", {}).get("2", 0) < 2
            or data.get("epoch_server_pids") != {"1": server["pid"], "2": server["pid"]}
            or data.get("restored_value") != initial
            or data.get("persistent_value") != final
            or not old.get("peer")
            or not new.get("peer")
            or old["peer"] == new["peer"]
            or not old.get("entity")
            or not new.get("entity")
            or old["entity"] == new["entity"]
            or new.get("stale_input_enqueued") is not True
            or proof.get("old_owners", {}).get(index) != old["entity"]
            or set(proof.get("old_peers", {}).get(index, {})) != {str(old["peer"])}
            or set(history) != {str(old["peer"]), str(new["peer"])}
            or result.get("input_generations", {}).get(index) != {"1": True, "2": True}
            or connection.count("Connected") != 2
            or connection.count("Disconnected") != 1
        ):
            return "Client lost disconnect, cleared caches, fresh ownership or acknowledged authoritative recovery evidence"
        split = connection.index("Disconnected")
        if "Connected" not in connection[:split] or "Connected" not in connection[split + 1 :]:
            return "Client connection history does not span the server outage"
        old_entities.add(old["entity"])
        new_entities.add(new["entity"])
    if len(old_entities) != clients + 1 or len(new_entities) != clients + 1 or old_entities & new_entities:
        return "Recovered server reused or duplicated a retired entity handle"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True, help="EGP editor or template containing EGPNetSession")
    parser.add_argument(
        "--editor", type=Path, help="Matching Windows editor to export the lab when --engine is a template"
    )
    parser.add_argument("--clients", type=int, choices=range(1, 65), metavar="1..64", default=2)
    parser.add_argument("--mode", choices=("dedicated", "host"), default="dedicated")
    parser.add_argument("--visible", action="store_true", help="Show client windows and the listen-host window")
    parser.add_argument("--duration", type=bounded_float(5, 100), default=8.0, help="Client lifetime in seconds")
    parser.add_argument("--reconnect-at", type=bounded_float(0, 90), default=0.0, help="Reconnect every client once")
    parser.add_argument(
        "--client-stall-at",
        type=bounded_float(0, 90),
        default=0.0,
        help="Delay one admitted client's poll, then verify explicit fresh admission and ownership recovery",
    )
    parser.add_argument(
        "--client-stall-ms", type=int, default=750, help="Injected clock gap in milliseconds (550..5000)"
    )
    parser.add_argument(
        "--client-stall-index", type=int, default=0, help="Client to stall; the other clients remain connected"
    )
    parser.add_argument(
        "--client-stall-count",
        type=int,
        choices=range(1, 9),
        metavar="1..8",
        default=1,
        help="Number of sequential gaps in the selected client",
    )
    parser.add_argument(
        "--client-stall-interval",
        type=bounded_float(1, 90),
        default=8.0,
        help="Seconds between scheduled client stalls; each waits for verified prior recovery",
    )
    parser.add_argument(
        "--server-restart-at",
        type=bounded_float(0, 90),
        default=0.0,
        help="Replace the dedicated server while clients stay running",
    )
    parser.add_argument("--server-restart-mode", choices=("graceful", "abrupt"), default="graceful")
    parser.add_argument(
        "--server-stall-at",
        type=bounded_float(0, 90),
        default=0.0,
        help="Delay the authoritative poll, then restore the same server and readmit its clients",
    )
    parser.add_argument("--server-stall-ms", type=int, default=750, help="Server clock gap in ms (550..5000)")
    parser.add_argument(
        "--server-down-for", type=bounded_float(0, 10), default=1.0, help="Server outage duration in seconds"
    )
    parser.add_argument("--preset", choices=PRESETS, default="local")
    parser.add_argument("--latency", type=bounded_float(0, 5000), help="Outgoing one-way latency in ms")
    parser.add_argument("--jitter", type=bounded_float(0, 5000), help="Outgoing jitter in ms")
    parser.add_argument("--loss", type=bounded_float(0, 100), help="Outgoing packet loss percentage")
    parser.add_argument("--simulate-on", choices=("both", "server", "clients"), default="both")
    parser.add_argument("--port", type=int, default=0, help="Loopback UDP port; 0 selects an available port")
    parser.add_argument("--output", type=Path, default=ROOT / ".build/egp-network-lab")
    args = parser.parse_args()
    engine = args.engine.resolve()
    if not engine.is_file():
        parser.error("--engine must point to an existing executable")
    editor = args.editor.resolve() if args.editor else engine
    if not editor.is_file():
        parser.error("--editor must point to an existing executable")
    if not 0 <= args.port <= 65535:
        parser.error("--port must be 0..65535")
    if args.reconnect_at and not 1 <= args.reconnect_at <= args.duration - 3:
        parser.error("--reconnect-at must leave at least 3 seconds before the duration ends")
    if not 550 <= args.server_stall_ms <= 5000:
        parser.error("--server-stall-ms must be 550..5000")
    if args.server_stall_at:
        if args.reconnect_at or args.server_restart_at or args.client_stall_at:
            parser.error("--server-stall-at cannot overlap manual reconnect, server replacement or client stalls")
        if not 3 <= args.server_stall_at <= args.duration - args.server_stall_ms / 1000 - 7:
            parser.error("Server stalls must allow 3 seconds before the gap and 7 seconds afterward")
    elif args.server_stall_ms != 750:
        parser.error("--server-stall-ms requires --server-stall-at")
    if not 550 <= args.client_stall_ms <= 5000 or not 0 <= args.client_stall_index < args.clients:
        parser.error("--client-stall-ms must be 550..5000 and --client-stall-index must name an existing client")
    if args.client_stall_at:
        if args.reconnect_at or args.server_restart_at:
            parser.error("--client-stall-at cannot overlap --reconnect-at or --server-restart-at")
        if args.client_stall_count > 1 and args.client_stall_interval < args.client_stall_ms / 1000 + 6:
            parser.error("--client-stall-interval must leave at least 6 seconds after each gap for recovery")
        last_stall = args.client_stall_at + (args.client_stall_count - 1) * args.client_stall_interval
        if args.client_stall_at < 3 or last_stall > args.duration - args.client_stall_ms / 1000 - 7:
            parser.error("Client stalls must allow 3 seconds before the first gap and 7 seconds after the last")
    elif (
        args.client_stall_ms != 750
        or args.client_stall_index != 0
        or args.client_stall_count != 1
        or args.client_stall_interval != 8
    ):
        parser.error("Client stall options require --client-stall-at")
    if args.server_restart_at:
        if args.mode != "dedicated" or args.reconnect_at:
            parser.error("--server-restart-at requires dedicated mode without --reconnect-at")
        if not 3 <= args.server_restart_at <= args.duration - args.server_down_for - 7:
            parser.error("--server-restart-at must allow 3 seconds before replacement and 7 seconds after the outage")
    elif args.server_restart_mode != "graceful":
        parser.error("--server-restart-mode requires --server-restart-at")
    simulation = dict(zip(("latency", "jitter", "loss"), PRESETS[args.preset]))
    for key in simulation:
        if getattr(args, key) is not None:
            simulation[key] = getattr(args, key)
    output = args.output.resolve() / str(time.time_ns())
    output.mkdir(parents=True)
    project = output / "project"
    shutil.copytree(ROOT / "misc/egp/network_lab", project, ignore=shutil.ignore_patterns(".godot", "*.uid"))
    helpers = project / "addons/egp_net"
    helpers.mkdir(parents=True)
    for source in (ROOT / "modules/egp_net/gdscript").glob("*.gd"):
        shutil.copy2(source, helpers / source.name)
    source_hashes = {
        path.relative_to(project).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in sorted(project.rglob("*"))
        if path.is_file()
    }
    receipt = {
        "passed": False,
        "engine": str(engine),
        "engine_sha256": hashlib.sha256(engine.read_bytes()).hexdigest(),
        "source_hashes": source_hashes,
        "launcher_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "mode": args.mode,
        "clients": args.clients,
        "visible": args.visible,
        "duration_seconds": args.duration,
        "reconnect_at_seconds": args.reconnect_at,
        "client_stall": {
            "requested": bool(args.client_stall_at),
            "at_seconds": args.client_stall_at,
            "milliseconds": args.client_stall_ms,
            "index": args.client_stall_index,
            "count": args.client_stall_count,
            "interval_seconds": args.client_stall_interval,
        },
        "server_restart": {
            "requested": bool(args.server_restart_at),
            "at_seconds": args.server_restart_at,
            "mode": args.server_restart_mode,
            "down_seconds": args.server_down_for,
        },
        "server_stall": {
            "requested": bool(args.server_stall_at),
            "at_seconds": args.server_stall_at,
            "milliseconds": args.server_stall_ms,
        },
        "simulation": simulation,
        "simulate_on": args.simulate_on,
        "processes": [],
        "window_observation_supported": os.name == "nt",
        "visible_window_observations": [],
        "scope": "Local encrypted admission, account identity, replies, tick replication and optional reconnect/server replacement or server clock recovery. Recovery uses fresh tokens and explicit application checkpoint restoration, owner-authorized input and stale-entity checks. No automatic persistence, gameplay, physics rollback, remote auth or performance qualification.",
    }
    engine_main = engine.with_name(engine.name.replace(".console.exe", ".exe"))
    receipt["engine_artifacts"] = {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest()
        for path in {engine, engine_main, editor}
        if path.is_file()
    }
    children = []
    process_records = []
    logs = []
    visible_process_ids = set()
    windows_observed = {}
    max_visible_window_count = 0
    started = time.monotonic()
    try:
        with (output / "import.log").open("w", encoding="utf-8") as log:
            imported = subprocess.run(
                [str(editor), "--headless", "--editor", "--import", "--path", str(project), "--max-fps", "30"],
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=90,
            )
        import_text = (output / "import.log").read_text(encoding="utf-8", errors="replace")
        if imported.returncode or "ERROR:" in import_text:
            raise RuntimeError("Lab import failed; see import.log")
        project_arguments = ["--path", str(project)]
        if args.editor:
            runtime = output / "runtime/EGP.NetworkLab.exe"
            runtime.parent.mkdir()
            pack = runtime.with_suffix(".pck")
            (project / "export_presets.cfg").write_text(
                '[preset.0]\nname="Network Lab"\nplatform="Windows Desktop"\nrunnable=true\n'
                'export_filter="all_resources"\ninclude_filter=""\nexclude_filter=""\n'
                "[preset.0.options]\n"
                f'custom_template/debug="{engine.as_posix()}"\ncustom_template/release="{engine.as_posix()}"\n'
                "binary_format/embed_pck=false\n",
                encoding="utf-8",
            )
            with (output / "export.log").open("w", encoding="utf-8") as log:
                exported = subprocess.run(
                    [
                        str(editor),
                        "--headless",
                        "--path",
                        str(project),
                        "--export-release",
                        "Network Lab",
                        str(runtime),
                    ],
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=90,
                )
            export_text = (output / "export.log").read_text(encoding="utf-8", errors="replace")
            if exported.returncode or "ERROR:" in export_text or not pack.is_file() or not runtime.is_file():
                raise RuntimeError("Lab export failed; see export.log")
            receipt["project_pack"] = {"path": str(pack), "sha256": hashlib.sha256(pack.read_bytes()).hexdigest()}
            receipt["runtime_artifacts"] = {
                path.relative_to(runtime.parent).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(runtime.parent.rglob("*"))
                if path.is_file()
            }
            engine = engine_main = runtime
            project_arguments = []
        # Tokens never enter the retained project, logs or receipt.
        with tempfile.TemporaryDirectory(prefix="egp-network-lab-admission-") as admission_directory:
            admission = Path(admission_directory)

            def read_json(path):
                try:
                    return json.loads(path.read_text(encoding="utf-8"))
                except (OSError, ValueError):
                    return {}

            def wait_ready(child, generation):
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    ready = read_json(admission / f"ready-{generation}.json")
                    if ready.get("generation") == generation and ready.get("pid") == child.pid:
                        return ready
                    if child.poll() is not None:
                        raise RuntimeError("Server exited before publishing readiness")
                    time.sleep(0.05)
                raise RuntimeError("Server readiness watchdog expired")

            def launch(role, ordinal, name, generation=1, lifetime=None, port=None, checkpoint=0):
                visible = args.visible and role != "server"
                # Direct GUI launch makes each observed window belong to its owned
                # process, rather than to a console wrapper's child process.
                executable = engine_main if os.name == "nt" else engine
                command = [str(executable), *project_arguments, "--max-fps", "60"]
                if visible:
                    command += [
                        "--resolution",
                        "640x360",
                        "--position",
                        f"{40 + (ordinal % 3) * 80},{40 + (ordinal % 3) * 80}",
                    ]
                else:
                    command += ["--headless"]
                simulate = args.simulate_on == "both" or (args.simulate_on == "clients") == (role == "client")
                effective = simulation if simulate else dict.fromkeys(simulation, 0)
                command += [
                    "--",
                    f"--role={role}",
                    f"--clients={args.clients}",
                    f"--index={max(0, ordinal - 1)}",
                    f"--admissions={admission}",
                    f"--duration={args.duration if lifetime is None else lifetime}",
                    f"--reconnect-at={args.reconnect_at}",
                    f"--client-stall-at={args.client_stall_at}",
                    f"--client-stall-ms={args.client_stall_ms}",
                    f"--client-stall-index={args.client_stall_index}",
                    f"--client-stall-count={args.client_stall_count}",
                    f"--client-stall-interval={args.client_stall_interval}",
                    f"--port={args.port if port is None else port}",
                    f"--restart-enabled={int(bool(args.server_restart_at or args.server_stall_at))}",
                    f"--server-stall-at={args.server_stall_at}",
                    f"--server-stall-ms={args.server_stall_ms}",
                    f"--generation={generation}",
                    f"--checkpoint-value={checkpoint}",
                    f"--server-down-for={args.server_down_for}",
                ] + [f"--{key}={value}" for key, value in effective.items()]
                log = (output / (name + ".log")).open("w", encoding="utf-8")
                logs.append(log)
                launch_options = {}
                if os.name == "nt":
                    launch_options["creationflags"] = subprocess.CREATE_NO_WINDOW
                child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, **launch_options)
                children.append(child)
                process_records.append({"name": name, "role": role, "generation": generation, "command": command})
                if visible:
                    visible_process_ids.add(child.pid)
                print(f"Started {name} (PID {child.pid}, {'window' if visible else 'headless'})", flush=True)
                return child

            server = launch("host" if args.mode == "host" else "server", 0, "server")
            receipt["listener"] = wait_ready(server, 1)
            for ordinal in range(1, args.clients + 1):
                launch("client", ordinal, f"client-{ordinal - 1}")
            lab_started = time.monotonic()
            replaced = False
            deadline = time.monotonic() + args.duration + 15
            while any(child.poll() is None for child in children):
                if args.visible and os.name == "nt":
                    current_windows = visible_windows(visible_process_ids)
                    windows_observed.update(current_windows)
                    max_visible_window_count = max(max_visible_window_count, len(current_windows))
                if args.server_restart_at and not replaced and time.monotonic() - lab_started >= args.server_restart_at:
                    if server.poll() is not None:
                        raise RuntimeError("Server exited before its planned replacement")
                    publications = sorted(admission.glob("health-1-*.json"))
                    checkpoint = read_json(publications[-1]) if publications else {}
                    receipt["server_restart"]["observed_checkpoint"] = checkpoint
                    receipt["server_restart"]["observed_clients"] = {
                        str(index): read_json(paths[-1]) if paths else {}
                        for index in range(args.clients)
                        for paths in [sorted(admission.glob(f"client-health-{index}-*.json"))]
                    }
                    if (
                        checkpoint.get("pid") != server.pid
                        or checkpoint.get("generation") != 1
                        or checkpoint.get("admitted_clients") != args.clients
                        or checkpoint.get("input_clients") != args.clients
                        or checkpoint.get("root_authority") != -1
                        or checkpoint.get("tick", 0) < 2
                    ):
                        raise RuntimeError(
                            "Initial server did not establish all authenticated owners and inputs before replacement"
                        )
                    if args.server_restart_mode == "graceful":
                        (admission / "stop.request").write_text("stop", encoding="utf-8")
                    else:
                        process_records[0]["intentional_termination"] = True
                        server.terminate()
                    server.wait(timeout=5)
                    if args.server_restart_mode == "abrupt":
                        expected_code = 1 if os.name == "nt" else -signal.SIGTERM
                        if server.returncode != expected_code:
                            raise RuntimeError("Server exit did not match the requested abrupt termination")
                    if args.server_restart_mode == "graceful":
                        checkpoint = read_json(admission / "checkpoint.json")
                        if server.returncode != 0 or checkpoint.get("input_clients") != args.clients:
                            raise RuntimeError("Graceful server checkpoint failed")
                    process_records[0]["checkpoint"] = checkpoint
                    time.sleep(args.server_down_for)
                    replacement = launch(
                        "server",
                        0,
                        "server-2",
                        generation=2,
                        lifetime=max(1, args.duration - (time.monotonic() - lab_started)),
                        port=receipt["listener"]["port"],
                        checkpoint=checkpoint["persistent_value"],
                    )
                    ready = wait_ready(replacement, 2)
                    if ready["port"] != receipt["listener"]["port"] or ready["pid"] == server.pid:
                        raise RuntimeError("Replacement did not use a new PID on the original endpoint")
                    receipt["server_restart"].update({
                        "initial_pid": server.pid,
                        "replacement_pid": replacement.pid,
                        "checkpoint": checkpoint,
                        "replacement_listener": ready,
                    })
                    replaced = True
                if any(
                    child.poll() not in (None, 0) and not record.get("intentional_termination")
                    for child, record in zip(children, process_records)
                ):
                    raise RuntimeError("A lab process failed; see individual logs")
                if time.monotonic() > deadline:
                    raise RuntimeError("Lab watchdog expired")
                time.sleep(0.05)
            if args.server_restart_at and not replaced:
                raise RuntimeError("Requested server replacement never occurred")
            if args.visible and os.name == "nt" and set(windows_observed) != visible_process_ids:
                raise RuntimeError("A requested client/host window was never observed as visible")
            if args.visible and os.name == "nt" and max_visible_window_count != len(visible_process_ids):
                raise RuntimeError("Requested client/host windows were not observed visible together")
        receipt["passed"] = True
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    except KeyboardInterrupt:
        receipt["error"] = "Stopped by user"
    finally:
        for child in children:
            if child.poll() is None:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
                else:
                    child.kill()
                child.wait(timeout=5)
        for log in logs:
            log.close()
        for child, record in zip(children, process_records):
            name = record["name"]
            text = (output / (name + ".log")).read_text(encoding="utf-8", errors="replace")
            match = re.search(r"EGP_NETWORK_LAB (\{[^\n]+\})", text)
            try:
                result = json.loads(match.group(1)) if match else {"passed": False, "message": "missing result marker"}
            except ValueError:
                result = {"passed": False, "message": "invalid result marker"}
            receipt["processes"].append({
                **record,
                "pid": child.pid,
                "exit_code": child.returncode,
                "result": result,
            })
            if record.get("intentional_termination") and record.get("checkpoint"):
                receipt["processes"][-1]["result"] = {
                    "passed": True,
                    "planned_abrupt_termination": True,
                    "proof": "Authenticated ownership/input checkpoint before forced process termination",
                }
            elif child.returncode or not result.get("passed"):
                receipt["passed"] = False
                receipt.setdefault("error", f"{name} did not pass cleanly; see its log")
            if "ERROR:" in text:
                receipt["passed"] = False
                receipt.setdefault("error", f"{name} did not pass cleanly; see its log")
        if args.server_restart_at and receipt["passed"]:
            failure = restart_failure(receipt)
            if failure:
                receipt["passed"] = False
                receipt["error"] = failure
        if args.client_stall_at and receipt["passed"]:
            failure = stall_failure(receipt)
            if failure:
                receipt["passed"] = False
                receipt["error"] = failure
        if args.server_stall_at and receipt["passed"]:
            failure = server_stall_failure(receipt)
            if failure:
                receipt["passed"] = False
                receipt["error"] = failure
        receipt["elapsed_seconds"] = round(time.monotonic() - started, 3)
        receipt["visible_window_observations"] = list(windows_observed.values())
        receipt["max_simultaneous_visible_windows"] = max_visible_window_count
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"{'PASS' if receipt['passed'] else 'FAIL'}: {output / 'receipt.json'}", flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
