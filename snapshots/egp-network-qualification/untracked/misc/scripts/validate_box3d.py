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

ROOT = Path(__file__).resolve().parents[2]


def source_hash(path):
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", default=".build/box3d-validation")
    parser.add_argument("--config", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--generator")
    parser.add_argument("--parallel", type=int, default=4)
    parser.add_argument("--upstream-tests", action="store_true")
    parser.add_argument("--engine", help="Built EGP editor executable for the real native-class smoke test")
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
        started = time.monotonic()
        entry = {"name": name, "command": command, "timeout_seconds": timeout}
        receipt["steps"].append(entry)
        print(name, flush=True)
        try:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
            entry["exit_code"] = result.returncode
            (output / f"{name}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
            if result.returncode:
                print((result.stdout + result.stderr)[-6000:], file=sys.stderr)
                raise RuntimeError(f"{name} failed with exit code {result.returncode}")
            return result
        except subprocess.TimeoutExpired:
            entry["timed_out"] = True
            raise
        finally:
            entry["elapsed_seconds"] = round(time.monotonic() - started, 3)

    try:
        vendor = ROOT / "thirdparty/box3d"
        manifest = json.loads((vendor / "UPSTREAM.json").read_text(encoding="utf-8"))
        if manifest["commit"] != receipt["upstream_commit"]:
            raise RuntimeError("source revision and validation profile disagree")
        actual = {path.relative_to(vendor).as_posix()
                  for directory in ("include", "src", "test", "shared")
                  for path in (vendor / directory).rglob("*") if path.is_file()}
        actual.update(("CMakeLists.txt", "LICENSE"))
        if actual != set(manifest["files"]):
            raise RuntimeError("vendored source file set differs from the pin manifest")
        for relative, expected in manifest["files"].items():
            if source_hash(vendor / relative) != expected:
                raise RuntimeError(f"pinned source mismatch: {relative}")
        receipt["source_verified"] = True
        receipt["golden_trajectory_sha256"] = source_hash(ROOT / "tests/physics/box3d/golden_hashes.txt")
        configure = ["cmake", "-S", str(ROOT / "tests/physics/box3d"), "-B", str(output / "native"), "-DCMAKE_BUILD_TYPE=" + args.config]
        if args.generator:
            configure += ["-G", args.generator]
        run("native-configure", configure, 120)
        run("native-build", ["cmake", "--build", str(output / "native"), "--config", args.config, "--parallel", str(args.parallel)], 600)
        run("native-test", ["ctest", "--test-dir", str(output / "native"), "-C", args.config, "--output-on-failure"], 120)
        receipt["native_trajectory_passed"] = True
        if args.upstream_tests:
            configure = ["cmake", "-S", str(vendor), "-B", str(output / "upstream"), "-DCMAKE_BUILD_TYPE=" + args.config,
                         "-DBOX3D_SAMPLES=OFF", "-DBOX3D_UNIT_TESTS=ON", "-DBOX3D_AVX2=OFF", "-DBOX3D_PROFILE=OFF"]
            if args.generator:
                configure += ["-G", args.generator]
            if platform.system() == "Windows":
                configure += ["-DCMAKE_C_FLAGS=/fp:precise"]
            else:
                configure += ["-DCMAKE_C_FLAGS=-fno-fast-math -ffp-contract=off"]
            run("upstream-configure", configure, 120)
            run("upstream-build", ["cmake", "--build", str(output / "upstream"), "--config", args.config, "--parallel", str(args.parallel)], 600)
            directory = output / "upstream/bin"
            executable = directory / args.config / "test.exe" if platform.system() == "Windows" else directory / "test"
            if not executable.exists():
                executable = directory / args.config / "test"
            run("upstream-test", [str(executable)], 120)
            receipt["upstream_tests_passed"] = True
        if args.engine:
            engine = (ROOT / args.engine).resolve()
            result = run("godot-native-smoke", [str(engine), "--headless", "--path", str(ROOT / "tests/physics/box3d/godot"), "--script", "res://smoke.gd"], 60)
            if "BOX3D_GODOT_PASS" not in result.stdout:
                raise RuntimeError("Godot exited without the Box3D smoke success marker")
            receipt["godot_runtime_passed"] = True
        print("PASS: pinned native Box3D trajectory and local replay checks", flush=True)
        return 0
    except (RuntimeError, subprocess.TimeoutExpired, OSError) as error:
        receipt["failure"] = str(error)
        print(str(error), file=sys.stderr)
        return 1
    finally:
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    sys.exit(main())
