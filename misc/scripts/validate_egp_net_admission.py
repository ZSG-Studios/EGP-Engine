#!/usr/bin/env python3
"""Test local token admission across listener clock failure or graceful restart.

Retained keys must admit an unused token issued in the restart second. Older
tokens must fail the transport's start-time gate. Generated keys reject both.
No key/token material is saved. A missed timestamp boundary fails the run.
"""
if __name__ == "__main__":
    raise SystemExit("This networking fixture is retired. Use validate_superpos.py with the current engine; legacy transport results do not qualify Superpos.")


import argparse
import hashlib
import itertools
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

from egp_engine_process import resolve_engine_process

ROOT = Path(__file__).resolve().parents[2]
MARKER = "EGP_ADMISSION_LIFECYCLE "
CLOCK_DIAGNOSTIC = "Fixed simulation exceeded its catch-up budget; resynchronization required."


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def evidence_failure(result, case):
    api, key, boundary, fault = case
    expected = key != "generated" and boundary == "same"
    created = result.get("token_created_utc")
    expires = result.get("token_expires_utc")
    before = result.get("restart_before_utc")
    after = result.get("restart_after_utc")
    if not all(type(value) is int and value > 0 for value in (created, expires, before, after)):
        return "Missing public token/restart timestamps"
    if before != after or (created != after if boundary == "same" else created >= before):
        return "Requested timestamp boundary was not observed"
    if expires != created + 120 or result.get("token_lifetime_seconds") != 120:
        return "Token lifetime does not match listener maximum"
    if (
        not result.get("passed")
        or (result.get("api"), result.get("key"), result.get("boundary"), result.get("fault")) != case
        or result.get("expected_connected") is not expected
        or result.get("peers_before_join") != 0
        or result.get("entities_before_join") != 0
        or result.get("gap_ms", 0) < (550 if boundary == "same" else 1100)
        or result.get("poll_error") != (1 if fault == "clock" else 0)
        or result.get("diagnostics") != ([CLOCK_DIAGNOSTIC] if fault == "clock" else [])
    ):
        return "Incomplete clock/authority/admission evidence"
    states = result.get("states", [])
    if "Connecting" not in states:
        return "Token connection was not attempted"
    if expected:
        if (
            result.get("final_state") != "Connected"
            or result.get("peers_before_close") != 1
            or "Connected" not in states
        ):
            return "Retained same-second token did not reach authority"
    elif (
        result.get("final_state") != "Disconnected"
        or result.get("peers_before_close") != 0
        or any(s in states for s in ("Connected", "Synchronizing"))
    ):
        return "Rejected token reached authority or did not disconnect"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument(
        "--editor", type=Path, help="Export with this editor when --engine is a matching Windows template"
    )
    parser.add_argument("--api", choices=("native", "gdscript", "both"), default="both")
    parser.add_argument(
        "--key",
        choices=("zero", "nonzero", "generated", "all"),
        default="all",
        help="Test-only retained patterns or freshly generated server key",
    )
    parser.add_argument("--boundary", choices=("same", "cross", "both"), default="both")
    parser.add_argument("--fault", choices=("clock", "graceful", "both"), default="clock")
    parser.add_argument("--output", type=Path, default=ROOT / ".build/egp-admission-lifecycle")
    args = parser.parse_args()
    engine, engine_process = resolve_engine_process(args.engine)
    editor, editor_process = resolve_engine_process(args.editor or args.engine)
    if not engine.is_file() or not editor.is_file():
        parser.error("Engine and editor must exist")
    output = args.output.resolve() / str(time.time_ns())
    project = output / "project"
    project.mkdir(parents=True)
    source = ROOT / "misc/egp/network_lab/admission_lifecycle.gd"
    shutil.copy2(source, project / source.name)
    helpers = project / "addons/egp_net"
    helpers.mkdir(parents=True)
    for path in (ROOT / "modules/egp_net/gdscript").glob("*.gd"):
        shutil.copy2(path, helpers / path.name)
    (project / "project.godot").write_text(
        'config_version=5\n[application]\nconfig/name="EGP Admission Lifecycle"\nrun/main_scene="res://main.tscn"\n[rendering]\nrenderer/rendering_method="forward_plus"\n',
        encoding="utf-8",
    )
    (project / "main.tscn").write_text(
        '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://admission_lifecycle.gd" id="1"]\n[node name="AdmissionLifecycle" type="Node"]\nscript = ExtResource("1")\n',
        encoding="utf-8",
    )
    receipt = {
        "passed": False,
        "engine": str(engine),
        "engine_process": engine_process,
        "editor_process": editor_process,
        "engine_sha256": digest(engine),
        "editor_sha256": digest(editor),
        "source_sha256": {
            str(path.relative_to(ROOT)): digest(path)
            for path in [
                source,
                Path(__file__),
                ROOT / "misc/scripts/egp_engine_process.py",
                *(ROOT / "modules/egp_net/gdscript").glob("*.gd"),
            ]
        },
        "steps": [],
        "scope": "Local native/GDScript admission timing and listener lifecycle only; fixed test keys stay in memory. No remote authentication/backend revocation or performance qualification.",
    }

    def run(label, command, timeout, case=None):
        logfile = output / (label + ".log")
        with logfile.open("w", encoding="utf-8") as log:
            child = subprocess.Popen(
                command, stdout=log, stderr=subprocess.STDOUT, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0)
            )
            try:
                code = child.wait(timeout)
            except subprocess.TimeoutExpired:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
                else:
                    child.kill()
                child.wait()
                code = -1
        text = logfile.read_text(encoding="utf-8", errors="replace")
        step = {
            "name": label,
            "command": command,
            "pid": child.pid,
            "exit_code": code,
            "log": str(logfile),
            "passed": code == 0 and "ERROR:" not in text,
        }
        if case:
            lines = [line[len(MARKER) :] for line in text.splitlines() if line.startswith(MARKER)]
            result = json.loads(lines[0]) if len(lines) == 1 else {}
            step["result"] = result
            step["evidence_failure"] = evidence_failure(result, case)
            if result.get("pid") != child.pid:
                step["evidence_failure"] = "Receipt process identity does not match launched process"
            step["passed"] = step["passed"] and step["evidence_failure"] is None
        receipt["steps"].append(step)
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        print(label, "PASS" if step["passed"] else "FAIL", flush=True)
        if not step["passed"]:
            raise RuntimeError(f"{label} failed; see {logfile}")

    try:
        run(
            "import", [str(editor), "--headless", "--editor", "--import", "--path", str(project), "--max-fps", "30"], 90
        )
        project_args = ["--path", str(project)]
        if args.editor:
            runtime = output / "runtime/EGP.AdmissionLifecycle.exe"
            runtime.parent.mkdir()
            (project / "export_presets.cfg").write_text(
                '[preset.0]\nname="Admission Lifecycle"\nplatform="Windows Desktop"\nrunnable=true\nexport_filter="all_resources"\ninclude_filter=""\nexclude_filter=""\n[preset.0.options]\n'
                + f'custom_template/debug="{engine.as_posix()}"\ncustom_template/release="{engine.as_posix()}"\nbinary_format/embed_pck=false\napplication/modify_resources=false\n',
                encoding="utf-8",
            )
            run(
                "export",
                [
                    str(editor),
                    "--headless",
                    "--path",
                    str(project),
                    "--export-release",
                    "Admission Lifecycle",
                    str(runtime),
                ],
                90,
            )
            if not runtime.is_file() or not runtime.with_suffix(".pck").is_file():
                raise RuntimeError("Export is missing its executable or project pack")
            if digest(runtime) != receipt["engine_sha256"]:
                raise RuntimeError("Exported executable does not match the requested template")
            receipt["runtime_sha256"] = {
                str(path.relative_to(runtime.parent)): digest(path)
                for path in runtime.parent.rglob("*")
                if path.is_file()
            }
            engine = runtime
            project_args = []
        apis = ("native", "gdscript") if args.api == "both" else (args.api,)
        keys = ("zero", "nonzero", "generated") if args.key == "all" else (args.key,)
        boundaries = ("same", "cross") if args.boundary == "both" else (args.boundary,)
        faults = ("clock", "graceful") if args.fault == "both" else (args.fault,)
        for case in itertools.product(apis, keys, boundaries, faults):
            for rel, expected in receipt["source_sha256"].items():
                if digest(ROOT / rel) != expected:
                    raise RuntimeError(f"Source changed during qualification: {rel}")
            api, key, boundary, fault = case
            run(
                "-".join(case),
                [
                    str(engine),
                    "--headless",
                    *project_args,
                    "--max-fps",
                    "60",
                    "--",
                    f"api={api}",
                    f"key={key}",
                    f"boundary={boundary}",
                    f"fault={fault}",
                ],
                15,
                case,
            )
        receipt["passed"] = True
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
        receipt["failure"] = str(error)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print("Receipt:", output / "receipt.json", flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
