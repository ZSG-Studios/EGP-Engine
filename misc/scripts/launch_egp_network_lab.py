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
        "--server-restart-at",
        type=bounded_float(0, 90),
        default=0.0,
        help="Replace the dedicated server while clients stay running",
    )
    parser.add_argument("--server-restart-mode", choices=("graceful", "abrupt"), default="graceful")
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
        "server_restart": {
            "requested": bool(args.server_restart_at),
            "at_seconds": args.server_restart_at,
            "mode": args.server_restart_mode,
            "down_seconds": args.server_down_for,
        },
        "simulation": simulation,
        "simulate_on": args.simulate_on,
        "processes": [],
        "window_observation_supported": os.name == "nt",
        "visible_window_observations": [],
        "scope": "Local encrypted admission, account identity, replies, tick replication and optional reconnect/server replacement. Replacement uses fresh tokens and explicit application checkpoint restoration, owner-authorized input and stale-entity checks. No automatic persistence, gameplay, physics rollback, remote auth or performance qualification.",
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
                    f"--port={args.port if port is None else port}",
                    f"--restart-enabled={int(bool(args.server_restart_at))}",
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
        receipt["elapsed_seconds"] = round(time.monotonic() - started, 3)
        receipt["visible_window_observations"] = list(windows_observed.values())
        receipt["max_simultaneous_visible_windows"] = max_visible_window_count
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"{'PASS' if receipt['passed'] else 'FAIL'}: {output / 'receipt.json'}", flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
