"""Run encrypted local clients against a dedicated server or visible listen host."""

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PRESETS = {"local": (0, 0, 0), "wifi": (40, 10, 1), "wan": (100, 25, 3), "poor": (200, 60, 10)}


def bounded_float(low, high):
    def parse(value):
        number = float(value)
        if not math.isfinite(number) or not low <= number <= high:
            raise argparse.ArgumentTypeError(f"Expected a finite number from {low} to {high}")
        return number

    return parse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True, help="EGP editor containing EGPNetSession")
    parser.add_argument("--clients", type=int, choices=range(1, 65), metavar="1..64", default=2)
    parser.add_argument("--mode", choices=("dedicated", "host"), default="dedicated")
    parser.add_argument("--visible", action="store_true", help="Show client windows and the listen-host window")
    parser.add_argument("--duration", type=bounded_float(5, 100), default=8.0, help="Client lifetime in seconds")
    parser.add_argument("--reconnect-at", type=bounded_float(0, 90), default=0.0, help="Reconnect every client once")
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
    if not 0 <= args.port <= 65535:
        parser.error("--port must be 0..65535")
    if args.reconnect_at and not 1 <= args.reconnect_at <= args.duration - 3:
        parser.error("--reconnect-at must leave at least 3 seconds before the duration ends")
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
        "mode": args.mode,
        "clients": args.clients,
        "visible": args.visible,
        "duration_seconds": args.duration,
        "reconnect_at_seconds": args.reconnect_at,
        "simulation": simulation,
        "simulate_on": args.simulate_on,
        "processes": [],
        "scope": "Local encrypted admission, account identity, replies, tick replication and optional reconnect. Host mode includes a server-owned test entity. No gameplay, physics rollback, remote auth or performance qualification.",
    }
    engine_main = engine.with_name(engine.name.replace(".console.exe", ".exe"))
    receipt["engine_artifacts"] = {
        str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in {engine, engine_main} if path.is_file()
    }
    children = []
    logs = []
    started = time.monotonic()
    try:
        with (output / "import.log").open("w", encoding="utf-8") as log:
            imported = subprocess.run(
                [str(engine), "--headless", "--editor", "--import", "--path", str(project), "--max-fps", "30"],
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=90,
            )
        import_text = (output / "import.log").read_text(encoding="utf-8", errors="replace")
        if imported.returncode or "ERROR:" in import_text:
            raise RuntimeError("Lab import failed; see import.log")
        # Tokens never enter the retained project, logs or receipt.
        with tempfile.TemporaryDirectory(prefix="egp-network-lab-admission-") as admission_directory:
            admission = Path(admission_directory)
            roles = ["host" if args.mode == "host" else "server"] + ["client"] * args.clients
            for ordinal, role in enumerate(roles):
                if ordinal:
                    deadline = time.monotonic() + 10
                    while not (admission / "ready.json").is_file():
                        if children[0].poll() is not None or time.monotonic() >= deadline:
                            raise RuntimeError("Server did not publish readiness; see server.log")
                        time.sleep(0.05)
                    receipt["listener"] = json.loads((admission / "ready.json").read_text(encoding="utf-8"))
                visible = args.visible and role != "server"
                command = [str(engine), "--path", str(project), "--max-fps", "60"]
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
                    f"--duration={args.duration}",
                    f"--reconnect-at={args.reconnect_at}",
                    f"--port={args.port}",
                ] + [f"--{key}={value}" for key, value in effective.items()]
                name = "server" if ordinal == 0 else f"client-{ordinal - 1}"
                log = (output / (name + ".log")).open("w", encoding="utf-8")
                logs.append(log)
                launch_options = {}
                if os.name == "nt":
                    launch_options["creationflags"] = subprocess.CREATE_NO_WINDOW
                child = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, **launch_options)
                children.append(child)
                print(f"Started {name} (PID {child.pid}, {'window' if visible else 'headless'})", flush=True)
            deadline = time.monotonic() + args.duration + 15
            while any(child.poll() is None for child in children):
                if any(child.poll() not in (None, 0) for child in children):
                    raise RuntimeError("A lab process failed; see individual logs")
                if time.monotonic() > deadline:
                    raise RuntimeError("Lab watchdog expired")
                time.sleep(0.05)
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
        for ordinal, child in enumerate(children):
            name = "server" if ordinal == 0 else f"client-{ordinal - 1}"
            text = (output / (name + ".log")).read_text(encoding="utf-8", errors="replace")
            match = re.search(r"EGP_NETWORK_LAB (\{[^\n]+\})", text)
            try:
                result = json.loads(match.group(1)) if match else {"passed": False, "message": "missing result marker"}
            except ValueError:
                result = {"passed": False, "message": "invalid result marker"}
            receipt["processes"].append({"name": name, "exit_code": child.returncode, "result": result})
            if child.returncode or not result.get("passed") or "ERROR:" in text:
                receipt["passed"] = False
                receipt.setdefault("error", f"{name} did not pass cleanly; see its log")
        receipt["elapsed_seconds"] = round(time.monotonic() - started, 3)
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"{'PASS' if receipt['passed'] else 'FAIL'}: {output / 'receipt.json'}", flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
