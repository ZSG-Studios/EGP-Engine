#!/usr/bin/env python3
"""Supervise cached bindings held across another GDExtension's failed reloads."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "misc/egp/cached_bindings"
CASES = ("call-instance", "call-static", "ptr-instance", "ptr-static", "validated-instance", "validated-static")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--sdk-library", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", choices=CASES, help="Run one isolated call path")
    args = parser.parse_args()
    output = args.output.resolve() / str(time.time_ns())
    project = output / "project"
    project.mkdir(parents=True)
    engine = args.engine.resolve()
    receipt = {
        "passed": False,
        "engine_sha256": digest(engine),
        "source_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "fixture_sha256": {str(p.relative_to(ROOT)): digest(p) for p in [*FIXTURE.iterdir(), Path(__file__)] if p.is_file()},
        "sdk_library_sha256": digest(args.sdk_library.resolve()),
        "steps": [],
        "scope": "Windows Debug SDK, independent observer library, raw call/ptrcall and typed GDScript validated instance/static calls. Missing/invalid DLL, compatible repair, retired signature and removed-class cache refresh. No arbitrary ABI/platform/soak claim.",
    }
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    if os.name == "nt":
        import ctypes
        ctypes.windll.kernel32.SetErrorMode(0x0001 | 0x0002 | 0x8000)

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    def run(name, command, timeout=120):
        command = list(map(str, command))
        start = time.monotonic()
        with (output / (name + ".log")).open("w", encoding="utf-8") as log:
            child = subprocess.Popen(command, cwd=project, stdout=log, stderr=subprocess.STDOUT, **options)
            try:
                code = child.wait(timeout)
            except subprocess.TimeoutExpired:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
                else:
                    child.kill()
                child.wait(15)
                code = -1
        step = {"name": name, "command": command, "pid": child.pid, "exit_code": code, "elapsed_seconds": round(time.monotonic() - start, 3)}
        receipt["steps"].append(step)
        save()
        return step, (output / (name + ".log")).read_text(encoding="utf-8", errors="replace")

    def descriptor(library, reloadable):
        return f'[configuration]\nentry_symbol="fixture_init"\ncompatibility_minimum="4.8"\nreloadable={str(reloadable).lower()}\n[libraries]\nwindows.debug.x86_64="res://bin/{library}.dll"\n'

    try:
        shutil.copyfile(FIXTURE / "main.gd", project / "main.gd")
        (project / "project.godot").write_text('config_version=5\n[application]\nconfig/name="CachedBindingFixture"\nrun/main_scene="res://main.tscn"\n[debug]\nhot_reload/enable_runtime=true\n', encoding="utf-8")
        (project / "main.tscn").write_text('[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://main.gd" id="1"]\n[node name="Fixture" type="Node"]\nscript=ExtResource("1")\n', encoding="utf-8")
        (project / ".godot").mkdir()
        (project / ".godot/extension_list.cfg").write_text('res://victim.gdextension\nres://observer.gdextension\n', encoding="utf-8")
        (project / "observer.gdextension").write_text(descriptor("observer", False), encoding="utf-8")
        command = ["cmake", "-S", FIXTURE, "-B", output / "build", f"-DEGP_CPP_SDK={args.sdk.resolve()}", f"-DEGP_CPP_LIBRARY={args.sdk_library.resolve()}", f"-DEGP_FIXTURE_BIN={project / 'bin'}"]
        for name, command in [("configure", command), ("build", ["cmake", "--build", output / "build", "--config", "Debug", "--parallel", "4"])]:
            step, _ = run(name, command, 300)
            if step["exit_code"]:
                raise RuntimeError(name + " failed")
        receipt["libraries_sha256"] = {p.name: digest(p) for p in (project / "bin").glob("*.dll")}
        (project / "bin/invalid.dll").write_bytes(b"Deliberately invalid isolated fixture DLL")
        for case in (args.case,) if args.case else CASES:
            (project / "victim.gdextension").write_text(descriptor("victim1", True), encoding="utf-8")
            step, text = run(case, [engine, "--headless", "--path", project, "--max-fps", "60", "--disable-crash-handler", "--", case], 30)
            stage = project / "stage.json"
            if stage.exists():
                checkpoint = json.loads(stage.read_text(encoding="utf-8"))
                if checkpoint["pid"] == step["pid"]:
                    step["last_stage"] = checkpoint
            marker = re.search(r"EGP_CACHED_BINDING_PASSED (\{[^\n]+\})", text)
            step["passed"] = step["exit_code"] == 0 and marker is not None and "SCRIPT ERROR:" not in text and "EGP_CACHED_BINDING_FAILED" not in text
            if marker:
                step["result"] = json.loads(marker.group(1))
            save()
            print(case, "PASS" if step["passed"] else "FAIL", flush=True)
            if not step["passed"]:
                raise RuntimeError(case + " failed; see " + str(output / (case + ".log")))
        receipt["passed"] = True
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as error:
        receipt["error"] = str(error)
    finally:
        save()
    print("PASS" if receipt["passed"] else "FAIL", output / "receipt.json", receipt.get("error", ""), flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
