#!/usr/bin/env python3
"""Verify pinned Box2D sources and run its unchanged native test suite."""
import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--output", default=".build/box2d-validation")
    parser.add_argument("--source-only", action="store_true")
    args = parser.parse_args()
    vendor = ROOT / "thirdparty/box2d"
    output = (ROOT / args.output / args.configuration).resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((vendor / "UPSTREAM.json").read_text())
    receipt = {"schema": 1, "upstream_commit": manifest["commit"], "source_verified": False,
               "native_upstream_passed": False, "cross_platform_qualified": False,
               "configuration": args.configuration, "commands": []}

    def run(command, label, timeout):
        started = time.monotonic()
        entry = {"command": command, "timeout_seconds": timeout}
        receipt["commands"].append(entry)
        try:
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, timeout=timeout)
            log = result.stdout + result.stderr
            entry["exit_code"] = result.returncode
        except subprocess.TimeoutExpired as error:
            entry["timed_out"] = True
            log = "".join(x.decode(errors="replace") if isinstance(x, bytes) else x or "" for x in (error.stdout, error.stderr))
            raise RuntimeError(label + " exceeded its watchdog")
        finally:
            entry["elapsed_seconds"] = round(time.monotonic() - started, 3)
            (output / (label + ".log")).write_text(log)
        if result.returncode:
            raise RuntimeError(label + " failed; see " + str(output / (label + ".log")))
        return log

    try:
        mismatches = []
        for relative, expected in manifest["sha256_lf"].items():
            path = vendor / relative
            if not path.is_file() or hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest() != expected:
                mismatches.append(relative)
        receipt["source_mismatches"] = mismatches
        if mismatches:
            raise RuntimeError("Pinned Box2D source mismatch: " + ", ".join(mismatches))
        receipt["source_verified"] = True
        if args.source_only:
            print("PASS: pinned Box2D source hashes")
            return 0
        build = output / "native"
        configure = ["cmake", "-S", str(vendor), "-B", str(build),
                     "-DBOX2D_SAMPLES=OFF", "-DBOX2D_UNIT_TESTS=ON", "-DBOX2D_AVX2=OFF",
                     "-DBOX2D_DISABLE_SIMD=OFF", "-DBOX2D_DOUBLE_PRECISION=OFF", "-DBUILD_SHARED_LIBS=OFF",
                     "-DCMAKE_BUILD_TYPE=" + args.configuration]
        if os.name == "nt":
            configure += ["-A", "x64"]
        run(configure, "configure", 120)
        run(["cmake", "--build", str(build), "--config", args.configuration, "--parallel", "8"], "build", 1200)
        executable = build / "bin" / args.configuration / "test.exe" if os.name == "nt" else build / "bin/test"
        log = run([str(executable)], "tests", 120)
        if "All Box2D tests passed!" not in log:
            raise RuntimeError("Native test suite exited without its success marker")
        receipt["native_upstream_passed"] = True
        receipt["executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        print("PASS: pinned Box2D native suite " + args.configuration, flush=True)
        return 0
    except (OSError, RuntimeError) as error:
        receipt["failure"] = str(error)
        print(error, file=sys.stderr)
        return 1
    finally:
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    sys.exit(main())
