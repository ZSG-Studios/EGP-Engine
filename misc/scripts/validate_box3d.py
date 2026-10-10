#!/usr/bin/env python3
"""Verify pinned Box3D sources and deterministic native replay; emit exact evidence."""

import argparse
import hashlib
import json
import platform
import subprocess
import sys
import time
from pathlib import Path

import egp_xmake
from egp_vendor_manifest import load_upstream_manifest, verify_excluded_files

ROOT = Path(__file__).resolve().parents[2]


def source_hash(path):
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default=".build/box3d-validation")
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--upstream-tests", action="store_true")
    parser.add_argument("--engine", help="Built EGP editor executable for the real native-class smoke test")
    parser.add_argument("--source-only", action="store_true")
    egp_xmake.add_options(parser)
    args = parser.parse_args()
    if not 1 <= args.parallel <= 32:
        parser.error("--parallel must be between 1 and 32")
    output = (ROOT / args.build_dir / args.config).resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipt = {
        "schema": 1,
        "upstream_commit": "e77352cd606dc1a34209094076199549a52ea0a1",
        "platform": platform.platform(),
        "architecture": platform.machine(),
        "configuration": args.config,
        "profile": "float32/simd4/precise/no-fma/60Hz/4substeps",
        "source_verified": False,
        "native_trajectory_passed": False,
        "upstream_tests_passed": None,
        "godot_runtime_passed": None,
        "cross_platform_qualified": False,
        "steps": [],
    }

    def run(name, command, timeout):
        cwd, environment = egp_xmake.execution(command, output, ROOT)
        started = time.monotonic()
        entry = {"name": name, "command": command, "timeout_seconds": timeout}
        receipt["steps"].append(entry)
        print(name, flush=True)
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                env=environment,
                capture_output=True,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=timeout,
            )
            entry["exit_code"] = result.returncode
            (output / f"{name}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            if result.returncode:
                print((result.stdout + result.stderr)[-6000:], file=sys.stderr)
                raise RuntimeError(f"{name} failed with exit code {result.returncode}")
            return result
        except subprocess.TimeoutExpired as error:
            entry["timed_out"] = True
            partial = []
            for data in (error.stdout, error.stderr):
                if data:
                    partial.append(data.decode("utf-8", errors="replace") if isinstance(data, bytes) else data)
            (output / f"{name}.log").write_text("".join(partial) + "\nWATCHDOG TIMEOUT\n", encoding="utf-8")
            raise
        finally:
            entry["elapsed_seconds"] = round(time.monotonic() - started, 3)

    try:
        vendor = ROOT / "thirdparty/box3d"
        manifest = load_upstream_manifest(vendor)
        if manifest["commit"] != receipt["upstream_commit"]:
            raise RuntimeError("source revision and validation profile disagree")
        actual = {
            path.relative_to(vendor).as_posix()
            for directory in ("include", "src", "test", "shared")
            for path in (vendor / directory).rglob("*")
            if path.is_file()
        }
        actual.update(path.name for path in (vendor / "LICENSE",) if path.is_file())
        verify_excluded_files(vendor, manifest)
        patched = {}
        for patch in manifest.get("egp_patches", []):
            if source_hash(ROOT / patch["path"]) != patch["sha256"]:
                raise RuntimeError("EGP patch manifest mismatch: " + patch["name"])
            for relative, hashes in patch["files"].items():
                if hashes["upstream_sha256"] != manifest["files"].get(relative) or relative in patched:
                    raise RuntimeError("Invalid EGP patch baseline: " + relative)
                patched[relative] = hashes["patched_sha256"]
        receipt["egp_patches"] = manifest.get("egp_patches", [])
        expected_files = {**manifest["files"], **patched}
        if actual != set(expected_files):
            raise RuntimeError("vendored source file set differs from the pin and EGP patch manifest")
        for relative, expected in expected_files.items():
            if source_hash(vendor / relative) != expected:
                raise RuntimeError(f"pinned source mismatch: {relative}")
        receipt["source_verified"] = True
        receipt["golden_trajectory_sha256"] = source_hash(ROOT / "tests/physics/box3d/golden_hashes.txt")
        if args.source_only:
            print("PASS: pinned Box3D source hashes and explicit upstream exclusions")
            return 0
        tool = egp_xmake.executable()
        project = ROOT / "tests/physics/box3d"
        build = output / "native"
        receipt["sanitizer"] = args.sanitizer
        receipt["toolchain"] = args.toolchain
        receipt["build_backend"] = "xmake"
        receipt["xmake_version"] = run("xmake-version", [tool, "--version"], 30).stdout
        run(
            "native-configure",
            egp_xmake.configure(tool, project, build, args.config, args.sanitizer, args.toolchain),
            120,
        )
        run("native-build", egp_xmake.build(tool, project, args.parallel), 600)
        run("native-test", egp_xmake.test(tool, project, "egp_box3d_determinism/*", "egp_box3d_joints/*"), 180)
        receipt["native_executable_sha256"] = {
            name: hashlib.sha256(egp_xmake.binary(build, name).read_bytes()).hexdigest()
            for name in ("egp_box3d_determinism", "egp_box3d_joints")
        }
        receipt["native_trajectory_passed"] = True
        if args.upstream_tests:
            run("upstream-build", egp_xmake.build(tool, project, args.parallel, "egp_box3d_upstream"), 600)
            run("upstream-test", egp_xmake.test(tool, project, "egp_box3d_upstream/*"), 120)
            receipt["upstream_executable_sha256"] = hashlib.sha256(
                egp_xmake.binary(build, "egp_box3d_upstream").read_bytes()
            ).hexdigest()
            receipt["upstream_tests_passed"] = True
        if args.engine:
            engine = (ROOT / args.engine).resolve()
            result = run(
                "godot-native-smoke",
                [
                    str(engine),
                    "--headless",
                    "--path",
                    str(ROOT / "tests/physics/box3d/godot"),
                    "--script",
                    "res://smoke.gd",
                ],
                60,
            )
            if "BOX3D_GODOT_PASS" not in result.stdout:
                raise RuntimeError("Godot exited without the Box3D smoke success marker")
            receipt["godot_runtime_passed"] = True
        print("PASS: pinned native Box3D trajectory and local replay checks", flush=True)
        return 0
    except (RuntimeError, ValueError, subprocess.TimeoutExpired, OSError) as error:
        receipt["failure"] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
