#!/usr/bin/env python3
"""Qualify EGP's sole native Box2D/Box3D backends with bounded headless scenes."""

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def validate_result(exit_code, log, fixture_name, completion_marker="RESULT: PASS"):
    expected_errors = []
    if fixture_name == "invalid_parameters.gd":
        expected_errors = ["Box2D: Shape cast parameters must not be null."] * 2 + [
            "Box2D: Rectangle is degenerate or smaller than the configured physics tolerance."
        ] * 4
    errors = re.findall(r"^ERROR: (.*)$", log, re.M)
    passed = (
        exit_code == 0
        and completion_marker in log
        and sorted(errors) == sorted(expected_errors)
        and not any(
            marker in log
            for marker in (
                "RESULT: FAIL",
                "SCRIPT ERROR",
                "BOX2D ASSERTION",
                "CrashHandlerException",
                "RID allocations of type",
            )
        )
    )
    return {"passed": passed, "expected_errors": expected_errors, "observed_errors": errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--output", default=".build/box2d-scene-validation")
    args = parser.parse_args()
    engine = (ROOT / args.engine).resolve()
    output = (ROOT / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = ROOT / "tests/physics/box2d/scene"
    receipt = {
        "schema": 1,
        "engine": str(engine),
        "tests": [],
        "all_passed": False,
        "full_physicsserver2d_parity": False,
        "scene_network_rollback_qualified": False,
        "cross_platform_qualified": False,
    }
    try:
        with engine.open("rb") as stream:
            receipt["engine_sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
        fixtures = sorted(source.glob("*_test.gd"), key=lambda p: (p.name != "backend_activation_test.gd", p.name))
        required = {
            "backend_activation_test.gd",
            "native_determinism_test.gd",
            "native_lifetime_test.gd",
            "queries_motion_test.gd",
            "sensor_lifetime_test.gd",
            "world_anchor_joint_test.gd",
            "character_motion_test.gd",
            "native_capabilities_test.gd",
            "canvas_cast_test.gd",
        }
        if required - {p.name for p in fixtures}:
            raise RuntimeError("Required Box2D fixture missing")
        if not (source / "invalid_parameters.gd").is_file():
            raise RuntimeError("Required Box2D invalid-parameter fixture missing")
        runs = [(fixture, 1, False) for fixture in fixtures] + [
            (source / "invalid_parameters.gd", 1, False),
            (source / "native_determinism_test.gd", 4, False),
            (source / "backend_activation_test.gd", 1, True),
        ]
        for fixture, workers, legacy in runs:
            suffix = f"workers-{workers}" + ("-legacy" if legacy else "")
            project = output / ("project-" + suffix)
            project.mkdir(exist_ok=True)
            project_text = (
                (source / "project.godot")
                .read_text()
                .replace("box_2d/worker_count=1", f"box_2d/worker_count={workers}")
            )
            if legacy:
                project_text = project_text.replace(
                    "[physics]", '[physics]\n2d/physics_engine="GodotPhysics2D"\n3d/physics_engine="Jolt Physics"'
                )
            (project / "project.godot").write_text(project_text)
            shutil.copyfile(fixture, project / fixture.name)
            entry = {
                "script": fixture.name,
                "workers": workers,
                "legacy_backend_migration": legacy,
                "timeout_seconds": 60,
                "passed": False,
                "script_sha256_lf": hashlib.sha256(fixture.read_bytes().replace(b"\r\n", b"\n")).hexdigest(),
            }
            receipt["tests"].append(entry)
            started = time.monotonic()
            try:
                result = subprocess.run(
                    [
                        str(engine),
                        "--headless",
                        "--max-fps",
                        "60",
                        "--path",
                        str(project),
                        "--script",
                        "res://" + fixture.name,
                    ],
                    cwd=ROOT,
                    capture_output=True,
                    text=True,
                    timeout=60,
                )
                log = result.stdout + result.stderr
                entry["exit_code"] = result.returncode
                entry.update(validate_result(result.returncode, log, fixture.name))
                trace = re.search(r"^TRACE_SHA256: ([0-9a-f]{64})$", log, re.M)
                if trace:
                    entry["trace_sha256"] = trace.group(1)
            except subprocess.TimeoutExpired as error:
                entry["timed_out"] = True
                log = (
                    "".join(
                        x.decode(errors="replace") if isinstance(x, bytes) else x or ""
                        for x in (error.stdout, error.stderr)
                    )
                    + "\nWATCHDOG TIMEOUT\n"
                )
            entry["elapsed_seconds"] = round(time.monotonic() - started, 3)
            (output / f"{fixture.stem}-{suffix}.log").write_text(log)
            print(("PASS " if entry["passed"] else "FAIL ") + f"{fixture.name} {suffix}", flush=True)
            if fixture.name == "backend_activation_test.gd" and not entry["passed"]:
                break
        receipt["all_passed"] = len(receipt["tests"]) == len(runs) and all(x["passed"] for x in receipt["tests"])
        trajectories = [x.get("trace_sha256") for x in receipt["tests"] if x["script"] == "native_determinism_test.gd"]
        receipt["one_and_four_worker_trace_equal"] = (
            len(trajectories) == 2 and trajectories[0] is not None and trajectories[0] == trajectories[1]
        )
        receipt["all_passed"] = receipt["all_passed"] and receipt["one_and_four_worker_trace_equal"]
        print(
            "PASS exact one/four-worker trajectory"
            if receipt["one_and_four_worker_trace_equal"]
            else "FAIL one/four-worker trajectory",
            flush=True,
        )
        return 0 if receipt["all_passed"] else 1
    except (OSError, RuntimeError) as error:
        receipt["failure"] = str(error)
        print(error, file=sys.stderr)
        return 1
    finally:
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    sys.exit(main())
