#!/usr/bin/env python3
"""Verify pinned Box2D sources and run its unchanged native test suite."""

import argparse
import hashlib
import json
import subprocess
import sys
import time
from pathlib import Path

import egp_xmake
from egp_vendor_manifest import verify_excluded_files

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--configuration", choices=("Debug", "Release"), default="Release")
    parser.add_argument("--output", default=".build/box2d-validation")
    parser.add_argument("--source-only", action="store_true")
    egp_xmake.add_options(parser)
    args = parser.parse_args()
    vendor = ROOT / "thirdparty/box2d"
    output = (ROOT / args.output / args.configuration).resolve()
    output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((vendor / "UPSTREAM.json").read_text())
    receipt = {
        "schema": 1,
        "upstream_commit": manifest["commit"],
        "source_verified": False,
        "native_upstream_passed": False,
        "cross_platform_qualified": False,
        "configuration": args.configuration,
        "commands": [],
    }

    def run(command, label, timeout):
        cwd, environment = egp_xmake.execution(command, output, ROOT)
        started = time.monotonic()
        entry = {"command": command, "timeout_seconds": timeout}
        receipt["commands"].append(entry)
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
            log = result.stdout + result.stderr
            entry["exit_code"] = result.returncode
        except subprocess.TimeoutExpired as error:
            entry["timed_out"] = True
            log = "".join(
                x.decode(errors="replace") if isinstance(x, bytes) else x or "" for x in (error.stdout, error.stderr)
            )
            raise RuntimeError(label + " exceeded its watchdog")
        finally:
            entry["elapsed_seconds"] = round(time.monotonic() - started, 3)
            (output / (label + ".log")).write_text(log, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(label + " failed; see " + str(output / (label + ".log")))
        return log

    try:
        verify_excluded_files(vendor, manifest)
        actual = {path.relative_to(vendor).as_posix() for path in vendor.rglob("*") if path.is_file()}
        # EGP-authored sources that compile against Box2D internals live beside the vendor
        # tree; they are pinned separately so the upstream pin stays the upstream commit.
        additions = manifest.get("egp_additions", {}).get("sha256_lf", {})
        if set(additions) & set(manifest["sha256_lf"]):
            raise RuntimeError("EGP additions overlap the upstream Box2D pin")
        if actual != set(manifest["sha256_lf"]) | set(additions) | {"UPSTREAM.json"}:
            raise RuntimeError("Vendored Box2D file set differs from the active source pin")
        # Explicit EGP patches over upstream files: the patch itself is pinned, its baseline
        # must be the upstream pin, and the patched file must match the recorded result.
        patched = {}
        for entry in manifest.get("egp_patches", []):
            patch_path = ROOT / entry["path"]
            if not patch_path.is_file() or hashlib.sha256(patch_path.read_bytes().replace(b"\r\n", b"\n")).hexdigest() != entry["sha256"]:
                raise RuntimeError("EGP patch manifest mismatch: " + entry["name"])
            for relative, hashes in entry["files"].items():
                if hashes["upstream_sha256"] != manifest["sha256_lf"].get(relative) or relative in patched:
                    raise RuntimeError("Invalid EGP patch baseline: " + relative)
                patched[relative] = hashes["patched_sha256"]
        receipt["egp_patches"] = manifest.get("egp_patches", [])
        mismatches = []
        for relative, expected in {**manifest["sha256_lf"], **patched, **additions}.items():
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
        tool = egp_xmake.executable()
        project = ROOT / "tests/physics/box2d"
        receipt["sanitizer"] = args.sanitizer
        receipt["toolchain"] = args.toolchain
        receipt["build_backend"] = "xmake"
        receipt["xmake_version"] = run([tool, "--version"], "xmake-version", 30)
        run(
            egp_xmake.configure(tool, project, build, args.configuration, args.sanitizer, args.toolchain),
            "configure",
            120,
        )
        run(egp_xmake.build(tool, project, 8, "egp_box2d_upstream"), "build", 1200)
        run(egp_xmake.test(tool, project, "egp_box2d_upstream/*"), "xmake-test", 120)
        executable = egp_xmake.binary(build, "egp_box2d_upstream")
        log = run([str(executable)], "tests", 120)
        if "All Box2D tests passed!" not in log:
            raise RuntimeError("Native test suite exited without its success marker")
        receipt["native_upstream_passed"] = True
        receipt["executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        print("PASS: pinned Box2D native suite " + args.configuration, flush=True)
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        receipt["failure"] = str(error)
        print(error, file=sys.stderr)
        return 1
    finally:
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")


if __name__ == "__main__":
    sys.exit(main())
