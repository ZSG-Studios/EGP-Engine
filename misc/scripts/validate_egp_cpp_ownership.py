#!/usr/bin/env python3
"""Exercise explicit C++ Net/Box3D ownership through two actual compatible DLL reloads."""

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "misc/egp/cpp_reload_ownership"


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def evidence_failure(proof, native_recovery=False):
    if not isinstance(proof, dict) or proof.get("passed") is not True:
        return "Missing literal success"
    if proof.get("native_recovery", False) is not native_recovery:
        return "Recovery mode evidence mismatch"
    phases = proof.get("phases")
    if not isinstance(phases, list) or len(phases) != 3:
        return "Expected three live code versions"
    transfers = proof.get("transfers")
    if (
        type(proof.get("assertions")) is not int
        or proof["assertions"] < 50
        or not isinstance(transfers, list)
        or len(transfers) != 2
    ):
        return "Missing runtime assertions/transfers"
    for version, transfer in enumerate(transfers, 2):
        if not isinstance(transfer, dict) or any(
            type(transfer.get(k)) is not int
            for k in ("version", "node_id", "restored_node_id", "tick", "restored_tick")
        ):
            return "Malformed transfer"
        if (
            transfer["version"] != version
            or transfer["node_id"] == 0
            or transfer["node_id"] != transfer["restored_node_id"]
            or transfer["tick"] != transfer["restored_tick"]
            or transfer["tick"] <= 0
        ):
            return "Unloading changed live node/solver tick"
        if (
            not isinstance(transfer.get("hash"), str)
            or re.fullmatch(r"[0-9a-f]{16}", transfer["hash"]) is None
            or transfer["hash"] != transfer.get("restored_hash")
        ):
            return "Unloading changed exact solver state"
        if native_recovery and (
            type(transfer.get("fault_elapsed_ms")) is not int
            or not 0 <= transfer["fault_elapsed_ms"] < 500
            or transfer.get("parent_name") != f"RecoveredOwnership{version}invalid"
        ):
            return "Missing bounded fault interval/retained parent edit"
    previous = None
    for version, phase in enumerate(phases, 1):
        if not isinstance(phase, dict):
            return "Malformed phase"
        for name in (
            "version",
            "world_tick",
            "world_id",
            "entity",
            "before",
            "after",
            "messages",
            "capsules",
            "before_connections",
            "clock_connections",
        ):
            if type(phase.get(name)) is not int:
                return "Missing integer " + name
        if phase["version"] != version or phase["messages"] != version or phase["world_tick"] < 12:
            return "Code/message/clock sequence mismatch"
        if not isinstance(phase.get("world_hash"), str) or re.fullmatch(r"[0-9a-f]{16}", phase["world_hash"]) is None:
            return "Missing exact solver state hash"
        if phase["before"] != phase["after"] or phase["after"] != phase["world_tick"] or phase.get("failures", 0) != 0:
            return "Callback counts or physics failures"
        if phase["capsules"] != 0 or phase["before_connections"] != 1 or phase["clock_connections"] != 1:
            return "Ownership or callback duplication"
        if phase.get("unexpected_tick_callback", False) is not False:
            return "Explicit disconnect left a callback"
        if phase.get("server_state") != "Listening" or phase.get("client_state") != "Connected":
            return "Authenticated session state"
        if (
            type(phase.get("failures", 0)) is not int
            or type(phase.get("body_y")) not in (int, float)
            or not math.isfinite(phase["body_y"])
            or phase["body_y"] >= 10000
        ):
            return "Malformed/missing moving body evidence"
        mapping = phase.get("body_map")
        record = phase.get("client_entity")
        if (
            not isinstance(mapping, dict)
            or mapping != {str(phase["entity"]): 10000}
            or not isinstance(record, dict)
            or record.get("entity") != phase["entity"]
        ):
            return "Stable authoritative body mapping/baseline"
        state = record.get("state")
        if (
            not isinstance(state, dict)
            or type(state.get("physics_tick")) is not int
            or not 0 < state["physics_tick"] <= phase["world_tick"]
        ):
            return "Missing replicated physics tick"
        for name in ("server", "client", "adapter", "server_session", "client_session"):
            # RefCounted ObjectIDs use the high bit and are signed negative in Godot's int Variant.
            if (
                type(phase.get(name + "_id")) is not int
                or phase[name + "_id"] == 0
                or phase[name + "_id"] != phase.get(name + "_live_id")
            ):
                return "Retained identity " + name
            if previous and previous[name + "_id"] != phase[name + "_id"]:
                return "Identity replaced after reload"
        if previous:
            if (
                phase["world_id"] != previous["world_id"]
                or phase["entity"] != previous["entity"]
                or phase["world_tick"] <= previous["world_tick"]
                or phase["body_y"] >= previous["body_y"]
            ):
                return "World/entity identity or progression"
            if (
                any(type(phase.get(k)) is not int for k in ("handoffs", "restores", "checks"))
                or phase["handoffs"] != version - 1
                or phase["restores"] != version - 1
                or phase["checks"] < 30 * (version - 1)
            ):
                return "Missing capsule controls"
        previous = phase
    for transfer, phase in zip(transfers, phases):
        if transfer["tick"] != phase["world_tick"] or transfer["hash"] != phase.get("world_hash"):
            return "Transfer checkpoint does not match preceding live phase"
    faults = proof.get("faults", [])
    if not isinstance(faults, list) or len(faults) != (4 if native_recovery else 0):
        return "Missing/unexpected failed-library phases"
    for index, fault in enumerate(faults):
        version = 2 + index // 2
        kind = "missing" if index % 2 == 0 else "invalid"
        if not isinstance(fault, dict) or any(
            type(fault.get(k)) is not int for k in ("version", "status", "node_id", "parent_id", "children")
        ):
            return "Malformed failed-library phase"
        if (
            fault["version"] != version
            or fault.get("kind") != kind
            or fault["status"] != 1
            or fault["children"] != 2
            or fault["node_id"] != transfers[index // 2]["node_id"]
            or fault["parent_id"] == 0
            or fault["parent_id"] != faults[0].get("parent_id")
        ):
            return "Failed-library identity/state mismatch"
        if (
            fault.get("methods_unavailable") is not True
            or fault.get("library_closed") is not True
            or fault.get("name") != f"RecoveredOwnership{version}{kind}"
        ):
            return "Failed-library diagnostic boundary/parent state"
    return None


def diagnostic_failure(text, native_recovery=False):
    if "SCRIPT ERROR:" in text or "EGP_CPP_OWNERSHIP_FAILED" in text:
        return "Script/ownership failure"
    errors = [line.strip() for line in text.splitlines() if line.strip().startswith("ERROR:")]
    if not native_recovery:
        return "Unexpected engine error" if errors else None
    expected = {
        'ERROR: Condition "!FileAccess::exists(path)" is true. Returning: ERR_FILE_NOT_FOUND': 2,
        "ERROR: GDExtension dynamic library not found: 'res://ownership.gdextension'.": 2,
        "ERROR: Can't open GDExtension dynamic library: 'res://ownership.gdextension'.": 2,
    }
    for line, count in expected.items():
        if errors.count(line) != count:
            return "Missing expected failed-load diagnostic"
    invalid = [
        line
        for line in errors
        if re.fullmatch(r"ERROR: Can't open dynamic library: .*[/\\]ownership-invalid\.dll\. Error: .+\.", line)
    ]
    if len(invalid) != 2 or len(errors) != 8:
        return "Unexpected/missing native-loader error"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--sdk-library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--native-recovery", action="store_true", help="Inject missing and invalid DLLs before each compatible repair"
    )
    args = parser.parse_args()
    output = args.output.resolve() / str(time.time_ns())
    project = output / "project"
    project.mkdir(parents=True)
    engine = args.engine.resolve()
    inputs = [
        *FIXTURE.iterdir(),
        Path(__file__),
        ROOT / "modules/egp_net/cpp/egp_net.hpp",
        *sorted((ROOT / "modules/egp_net/gdscript").glob("*.gd")),
    ]
    receipt = {
        "passed": False,
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "engine_sha256": digest(engine),
        "sdk_library_sha256": digest(args.sdk_library),
        "source_sha256": {str(p): digest(p) for p in inputs if p.is_file()},
        "steps": [],
        "native_recovery": args.native_recovery,
        "scope": "Explicit Godot-thread handoff before two real compatible C++ DLL reloads; authenticated local server/client, retained Net/Box3D/world/body identities, exact callback counts, forged/copied capsule rejection and handler resubscription. Optional missing/invalid DLL repair within the fixed-clock catch-up budget. No automatic/in-flight reload, prolonged fault/re-admission, independent-process/exported reload, cross-language capsules, arbitrary ABI/platform/scale/soak claim.",
    }
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    if os.name == "nt":
        import ctypes

        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    def run(name, command, timeout):
        command = list(map(str, command))
        start = time.monotonic()
        log = output / (name + ".log")
        with log.open("w", encoding="utf-8") as stream:
            child = subprocess.Popen(
                command,
                cwd=extension if name in ("configure", "build") else project,
                env=dict(
                    os.environ,
                    XMAKE_CONFIGDIR=str(output / "xmake-config"),
                    XMAKE_GLOBALDIR=str(output / "xmake-global"),
                ),
                stdout=stream,
                stderr=subprocess.STDOUT,
                **options,
            )
            try:
                code = child.wait(timeout)
            except subprocess.TimeoutExpired:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
                else:
                    child.kill()
                child.wait(15)
                code = -1
        receipt["steps"].append({
            "name": name,
            "command": command,
            "cwd": str(project),
            "pid": child.pid,
            "exit_code": code,
            "elapsed_seconds": round(time.monotonic() - start, 3),
            "log": str(log),
            "log_sha256": digest(log),
        })
        save()
        if code:
            raise RuntimeError(name + " exited " + str(code))
        return log.read_text(encoding="utf-8", errors="replace")

    try:
        addon = project / "addons/egp_net"
        addon.mkdir(parents=True)
        for path in (ROOT / "modules/egp_net/gdscript").glob("*.gd"):
            shutil.copyfile(path, addon / path.name)
        extension = output / "extension"
        shutil.copytree(FIXTURE, extension)
        shutil.copyfile(ROOT / "modules/egp_net/cpp/egp_net.hpp", extension / "egp_net.hpp")
        shutil.copyfile(FIXTURE / "main.gd", project / "main.gd")
        (project / "project.godot").write_text(
            'config_version=5\n[application]\nconfig/name="CPP Ownership"\nrun/main_scene="res://main.tscn"\n[debug]\nhot_reload/enable_runtime=true\n',
            encoding="utf-8",
        )
        (project / "main.tscn").write_text(
            '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://main.gd" id="1"]\n[node name="Fixture" type="Node"]\nscript=ExtResource("1")\n',
            encoding="utf-8",
        )
        (project / ".godot").mkdir()
        (project / ".godot/extension_list.cfg").write_text("res://ownership.gdextension\n", encoding="utf-8")
        (project / "ownership.gdextension").write_text(
            '[configuration]\nentry_symbol="ownership_init"\ncompatibility_minimum="4.8"\nreloadable=true\n[libraries]\nwindows.debug.x86_64="res://bin/ownership1.dll"\n',
            encoding="utf-8",
        )
        xmake = os.environ.get("XMAKE") or shutil.which("xmake") or "xmake"
        run(
            "configure",
            [
                xmake,
                "f",
                "-y",
                "-P",
                extension,
                "-o",
                output / "build",
                "-m",
                "debug",
                f"--egp_cpp_sdk={args.sdk.resolve()}",
                f"--egp_cpp_library={args.sdk_library.resolve()}",
                f"--egp_net_include={extension}",
                f"--egp_fixture_bin={project / 'bin'}",
            ],
            120,
        )
        run("build", [xmake, "-P", extension, "-b", "-j", "3"], 300)
        receipt["libraries_sha256"] = {p.name: digest(p) for p in (project / "bin").glob("*.dll")}
        if args.native_recovery:
            (project / "bin/ownership-invalid.dll").write_bytes(b"Deliberately invalid isolated ownership fixture DLL")
        command = [engine, "--headless", "--path", project, "--max-fps", "60", "--disable-crash-handler"]
        if args.native_recovery:
            command += ["--", "--native-recovery"]
        text = run("runtime", command, 45)
        markers = re.findall(r"EGP_CPP_OWNERSHIP_PASSED (\{[^\n]+\})", text)
        if len(markers) != 1 or diagnostic_failure(text, args.native_recovery):
            raise RuntimeError("Invalid runtime success evidence; see runtime.log")
        receipt["proof"] = json.loads(markers[0])
        failure = evidence_failure(receipt["proof"], args.native_recovery)
        if failure:
            raise RuntimeError(failure)
        if (
            digest(engine) != receipt["engine_sha256"]
            or digest(args.sdk_library) != receipt["sdk_library_sha256"]
            or any(digest(p) != value for p, value in receipt["source_sha256"].items())
        ):
            raise RuntimeError("Executed inputs changed during qualification")
        receipt["passed"] = True
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        receipt["error"] = str(error)
    finally:
        save()
    print("PASS" if receipt["passed"] else "FAIL", output / "receipt.json", receipt.get("error", ""), flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
