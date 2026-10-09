#!/usr/bin/env python3
"""Run EGP's native Box3D scene backend regressions with bounded processes."""
import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--output", default=".build/box3d-scene-validation")
    parser.add_argument("--include", action="append", help="Run a named fixture only; may be repeated")
    args = parser.parse_args()
    engine = (ROOT / args.engine).resolve()
    project = ROOT / "tests/physics/box3d/scene"
    output = (ROOT / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipt = {"schema": 1, "engine": str(engine), "engine_sha256": None, "tests": [],
               "all_passed": False, "scene_network_rollback_qualified": False,
               "full_physicsserver3d_parity": False, "cross_platform_qualified": False,
               "complete_suite": not args.include, "selected_scripts": args.include}
    try:
        with engine.open("rb") as stream:
            receipt["engine_sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
        scripts = sorted(project.glob("*_test.gd"), key=lambda p: (p.name != "backend_activation_test.gd", p.name))
        required = {"backend_activation_test.gd", "native_determinism_test.gd",
                    "native_indices_exclusions_test.gd", "native_lifetime_test.gd",
                    "world_anchor_joint_test.gd"}
        if args.include:
            required = set(args.include)
            scripts = [script for script in scripts if script.name in required]
        missing = required - {script.name for script in scripts}
        if missing:
            receipt["failure"] = "Missing required fixtures: " + ", ".join(sorted(missing))
            print(receipt["failure"], file=sys.stderr)
            return 1
        for script in scripts:
            started = time.monotonic()
            entry = {"script": script.name, "timeout_seconds": 60, "passed": False}
            entry["script_sha256_lf"] = hashlib.sha256(script.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
            receipt["tests"].append(entry)
            command = [str(engine), "--headless", "--max-fps", "60", "--path", str(project), "--script", "res://" + script.name]
            try:
                result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=60)
                log = result.stdout + result.stderr
                entry["exit_code"] = result.returncode
                entry["passed"] = result.returncode == 0 and "RESULT: PASS" in log and "RESULT: FAIL" not in log and "SCRIPT ERROR" not in log and "ERROR:" not in log and "RID allocations of type" not in log
            except subprocess.TimeoutExpired as error:
                entry["timed_out"] = True
                log = "".join(value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value or "" for value in (error.stdout, error.stderr)) + "\nWATCHDOG TIMEOUT\n"
            entry["elapsed_seconds"] = round(time.monotonic() - started, 3)
            (output / (script.stem + ".log")).write_text(log, encoding="utf-8")
            print(("PASS " if entry["passed"] else "FAIL ") + script.name, flush=True)
            if script.name == "backend_activation_test.gd" and not entry["passed"]:
                break
        receipt["all_passed"] = len(receipt["tests"]) == len(scripts) and all(test["passed"] for test in receipt["tests"])
        return 0 if receipt["all_passed"] else 1
    except OSError as error:
        receipt["failure"] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
