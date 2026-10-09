#!/usr/bin/env python3
"""Qualify the preserved old-network-only (Yojimbo egp_net) profile.

Opt-in legacy migration profile from the Superpos plan, step 8. Use an engine
built with module_egp_net_enabled=y module_superpos_enabled=n. Results never
qualify Superpos; validate_superpos.py qualifies the default build.
"""


import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import egp_xmake
from egp_vendor_manifest import normalization_pins, pinned_digest_match, verify_excluded_files

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--configuration", choices=("Debug", "Release"), default="Debug")
parser.add_argument("--engine", type=Path)
parser.add_argument(
    "--runtime-only",
    action="store_true",
    help="Qualify a matching built editor without rebuilding previously qualified native tests",
)
parser.add_argument("--output", type=Path, help="Keep this qualification separate from earlier binary receipts")
parser.add_argument(
    "--verify-vendor-only",
    action="store_true",
    help="Verify pinned vendor bytes without building or running networking",
)
egp_xmake.add_options(parser)
args = parser.parse_args()
if args.runtime_only and (not args.engine or args.verify_vendor_only):
    parser.error("--runtime-only requires --engine and cannot use --verify-vendor-only")
if args.verify_vendor_only and args.engine:
    parser.error("--verify-vendor-only cannot qualify an engine")
output = (args.output or ROOT / ".build/egp-net-validation" / args.configuration).resolve()
output.mkdir(parents=True, exist_ok=True)
build = output / "native"
receipt = {"passed": False, "configuration": args.configuration, "source_hashes": {}}
for path in sorted((ROOT / "modules/egp_net").rglob("*")):
    if path.is_file() and ".godot" not in path.parts:
        receipt["source_hashes"][path.relative_to(ROOT).as_posix()] = hashlib.sha256(path.read_bytes()).hexdigest()


def run(name, command, timeout=180):
    cwd, environment = egp_xmake.execution(command, output, ROOT)
    environment["EGP_TEST_PYTHON"] = sys.executable
    with (output / (name + ".log")).open("w", encoding="utf-8") as log:
        result = subprocess.run(
            [str(item) for item in command],
            cwd=cwd,
            stdout=log,
            stderr=subprocess.STDOUT,
            timeout=timeout,
            env=environment,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0,
        )
    if result.returncode:
        raise RuntimeError(f"{name} failed ({result.returncode}); see {output / (name + '.log')}")
    return (output / (name + ".log")).read_text(encoding="utf-8", errors="replace")


try:
    vendor = ROOT / "thirdparty/yojimbo"
    manifest = json.loads((vendor / "EGP-UPSTREAM.json").read_text(encoding="utf-8"))
    verify_excluded_files(vendor, manifest)
    actual = {path.relative_to(vendor).as_posix() for path in vendor.rglob("*") if path.is_file()}
    if actual != set(manifest["files"]) | {"EGP-UPSTREAM.json", "EGP-PROVENANCE.md"}:
        raise RuntimeError("Vendored Yojimbo file set differs from the active source pin")
    normalized = normalization_pins(manifest)
    receipt["vendor_file_identity"] = {}
    for relative, expected in manifest["files"].items():
        source = (vendor / relative).resolve()
        if not source.is_relative_to(vendor.resolve()) or not source.is_file():
            raise RuntimeError("Vendored source hash mismatch: " + relative)
        identity = pinned_digest_match(source.read_bytes(), expected, normalized_lf_expected=normalized.get(relative))
        if identity is None:
            raise RuntimeError("Vendored source hash mismatch: " + relative)
        receipt["vendor_file_identity"][relative] = identity
    receipt["upstream_commit"] = manifest["commit"]
    receipt["vendor_verified"] = True
    if args.verify_vendor_only:
        receipt.update(
            passed=True,
            scope="Pinned vendor source identity only; no native build, networking test or engine runtime qualification",
        )
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
        print("Pinned vendor source identity: PASS")
        sys.exit(0)
    receipt["runtime_only"] = args.runtime_only
    if not args.runtime_only:
        tool = egp_xmake.executable()
        project = ROOT / "modules/egp_net/tests"
        receipt["sanitizer"] = args.sanitizer
        receipt["toolchain"] = args.toolchain
        receipt["build_backend"] = "xmake"
        receipt["xmake_version"] = run("xmake-version", [tool, "--version"])
        run("configure", egp_xmake.configure(tool, project, build, args.configuration, args.sanitizer, args.toolchain))
        run("build", egp_xmake.build(tool, project, 6), timeout=300)
        tests = run("xmake-test", egp_xmake.test(tool, project), timeout=600)
        tests = re.sub(r"\x1b\[[0-?]*[ -/]*[@-~]", "", tests)
        summary = re.search(r"100% tests passed, 0 test\(s\) failed out of (\d+)", tests)
        if not summary or int(summary.group(1)) != 17:
            raise RuntimeError("xmake did not qualify all 17 native networking cases")
        receipt["native_test_cases"] = int(summary.group(1))
        executable = egp_xmake.binary(build, "egp_net_checks")
        receipt["native_executable_sha256"] = hashlib.sha256(executable.read_bytes()).hexdigest()
        text = run("native", [executable], timeout=40)
        match = re.search(r"EGP_NATIVE_NETWORK_CHECKS=(\d+)", text)
        if not match:
            raise RuntimeError("Native executable did not provide its success marker")
        receipt["native_checks"] = int(match.group(1))
    if args.engine:
        engine = args.engine.resolve()
        receipt["engine_sha256"] = hashlib.sha256(engine.read_bytes()).hexdigest()
        project = ROOT / "modules/egp_net/samples/gdscript"
        run("import", [engine, "--headless", "--import", "--path", project, "--max-fps", "30"], timeout=120)
        report = output / "gdscript.json"
        text = run(
            "gdscript",
            [engine, "--headless", "--path", project, "--max-fps", "60", "--", "--report=" + str(report)],
            timeout=45,
        )
        if "EGP_GDSCRIPT_AIO " not in text:
            raise RuntimeError("GDScript game did not provide its success marker")
        receipt["gdscript"] = json.loads(report.read_text())
        if not receipt["gdscript"]["passed"]:
            raise RuntimeError("GDScript AIO fixture failed")
        text = run(
            "cache", [engine, "--headless", "--path", project, "res://Cache.tscn", "--max-fps", "60"], timeout=20
        )
        match = re.search(r"EGP_NETWORK_CACHE (\{[^\n]+\})", text)
        if not match:
            raise RuntimeError("Entity cache fixture did not provide its success marker")
        receipt["cache"] = json.loads(match.group(1))
        if not receipt["cache"]["passed"] or receipt["cache"]["updates"] != 1024:
            raise RuntimeError("Immediate single-entity cache refresh fixture failed")
        text = run(
            "physics", [engine, "--headless", "--path", project, "res://Physics.tscn", "--max-fps", "60"], timeout=30
        )
        match = re.search(r"EGP_NETWORK_PHYSICS (\{[^\n]+\})", text)
        if not match:
            raise RuntimeError("Networked Box3D fixture did not provide its success marker")
        receipt["physics"] = json.loads(match.group(1))
        if not receipt["physics"]["passed"]:
            raise RuntimeError("Networked Box3D fixture failed")
        text = run(
            "token", [engine, "--headless", "--path", project, "res://Token.tscn", "--max-fps", "60"], timeout=20
        )
        match = re.search(r"EGP_NETWORK_TOKEN (\{[^\n]+\})", text)
        if not match:
            raise RuntimeError("Secure GDScript fixture did not provide its success marker")
        receipt["token"] = json.loads(match.group(1))
        if not receipt["token"]["passed"]:
            raise RuntimeError("Secure GDScript fixture failed")
        text = run(
            "prediction",
            [engine, "--headless", "--path", project, "res://Prediction.tscn", "--max-fps", "30"],
            timeout=30,
        )
        match = re.search(r"EGP_NETWORK_PREDICTION (\{[^\n]+\})", text)
        if not match:
            raise RuntimeError("Prediction fixture did not provide its success marker")
        receipt["prediction"] = json.loads(match.group(1))
        if not receipt["prediction"]["passed"]:
            raise RuntimeError("Prediction correction/replay fixture failed")
        text = run(
            "deterministic-replay",
            [engine, "--headless", "--path", project, "res://DeterministicReplay.tscn", "--max-fps", "60"],
            timeout=20,
        )
        match = re.search(r"EGP_NETWORK_DETERMINISTIC_REPLAY (\{[^\n]+\})", text)
        if not match:
            raise RuntimeError("Deterministic canonical-input replay fixture did not provide its success marker")
        receipt["deterministic_replay"] = json.loads(match.group(1))
        if not receipt["deterministic_replay"]["passed"]:
            raise RuntimeError("Deterministic canonical-input replay fixture failed")
        text = run(
            "snapshot-interpolation",
            [engine, "--headless", "--path", project, "res://SnapshotInterpolation.tscn", "--max-fps", "60"],
            timeout=20,
        )
        if "SNAPSHOT_INTERPOLATION_PASS" not in text or "ERROR:" in text:
            raise RuntimeError("Native snapshot interpolation fixture failed")
        receipt["snapshot_interpolation"] = {"passed": True}
        text = run(
            "superposition",
            [engine, "--headless", "--path", project, "res://Superposition.tscn", "--max-fps", "60"],
            timeout=30,
        )
        match = re.search(r"EGP_SUPERPOSITION (\{[^\n]+\})", text)
        if not match or "ERROR:" in text:
            raise RuntimeError("Superposition property replication fixture failed")
        receipt["superposition"] = json.loads(match.group(1))
        if not receipt["superposition"]["passed"]:
            raise RuntimeError("Superposition property replication fixture failed")
        text = run(
            "superposition-world",
            [engine, "--headless", "--path", project, "res://SuperpositionWorld.tscn", "--max-fps", "60"],
            timeout=20,
        )
        match = re.search(r"EGP_SUPERPOSITION_WORLD (\{[^\n]+\})", text)
        if not match or "ERROR:" in text:
            raise RuntimeError("Superposition World Inspector/session lifecycle fixture failed")
        receipt["superposition_world"] = json.loads(match.group(1))
        if not receipt["superposition_world"]["passed"]:
            raise RuntimeError("Superposition World fixture failed")
        text = run(
            "superposition-spawner-rpc",
            [
                sys.executable,
                ROOT / "misc/scripts/validate_superposition_spawner_rpc.py",
                "--engine",
                engine,
                "--output",
                output / "spawner-rpc",
            ],
            timeout=90,
        )
        spawned_receipt = Path(
            next(line.strip() for line in text.splitlines() if line.strip().endswith("receipt.json"))
        )
        if not spawned_receipt.resolve().is_relative_to((output / "spawner-rpc").resolve()):
            raise RuntimeError("Spawner/RPC qualification returned a receipt outside its output directory")
        receipt["superposition_spawner_rpc"] = json.loads(spawned_receipt.read_text())
        if not receipt["superposition_spawner_rpc"]["passed"]:
            raise RuntimeError("Superposition authenticated spawn/RPC lifecycle fixture failed")
        text = run(
            "superposition-delta",
            [engine, "--headless", "--path", project, "res://superposition_delta.tscn", "--max-fps", "60"],
            timeout=30,
        )
        if "SUPERPOSITION_DELTA_CHECKS_PASS" not in text or "ERROR:" in text:
            raise RuntimeError("Superposition acknowledged delta and typed property fixture failed")
        receipt["superposition_delta"] = {"passed": True}
        text = run(
            "superposition-callbacks",
            [engine, "--headless", "--path", project, "res://SuperpositionCallbacks.tscn", "--max-fps", "60"],
            timeout=30,
        )
        if "SUPERPOSITION_CALLBACK_CHECKS_PASS" not in text or "ERROR:" in text:
            raise RuntimeError("Superposition property callback lifetime fixture failed")
        receipt["superposition_callbacks"] = {"passed": True}
        for label, scene, marker in (
            ("superposition-prediction", "SuperpositionPrediction.tscn", "EGP_SUPERPOSITION_PREDICTION_PASS"),
            ("box3d-prediction", "Box3DPrediction.tscn", "EGP_BOX3D_PREDICTION_PASS"),
            ("box3d-prediction-transport", "Box3DPredictionTransport.tscn", "EGP_BOX3D_PREDICTION_TRANSPORT_PASS"),
        ):
            text = run(
                label, [engine, "--headless", "--path", project, "res://" + scene, "--max-fps", "60"], timeout=45
            )
            if marker not in text or "ERROR:" in text:
                raise RuntimeError(label + " native replay/transport fixture failed")
            receipt[label.replace("-", "_")] = {"passed": True}
        text = run(
            "lifecycle",
            [engine, "--headless", "--path", project, "res://Lifecycle.tscn", "--max-fps", "60"],
            timeout=20,
        )
        match = re.search(r"EGP_NETWORK_LIFECYCLE (\{[^\n]+\})", text)
        if not match:
            raise RuntimeError("Lifecycle fixture did not provide its success marker")
        receipt["lifecycle"] = json.loads(match.group(1))
        if not receipt["lifecycle"]["passed"]:
            raise RuntimeError("Native facade lifecycle fixture failed")
        text = run(
            "processes",
            [
                sys.executable,
                ROOT / "misc/scripts/validate_egp_net_process.py",
                "--engine",
                engine,
                "--project",
                project,
            ],
            timeout=25,
        )
        match = re.search(r"EGP_NETWORK_PROCESSES (\{[^\n]+\})", text)
        if not match:
            raise RuntimeError("Separate Godot processes did not provide their success marker")
        receipt["processes"] = json.loads(match.group(1))
        if not receipt["processes"]["passed"]:
            raise RuntimeError("Separate Godot server/client fixture failed")
    receipt["passed"] = True
except (RuntimeError, ValueError, subprocess.TimeoutExpired, OSError) as failure:
    receipt["error"] = str(failure)
finally:
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(json.dumps({key: value for key, value in receipt.items() if key != "source_hashes"}, indent=2))
sys.exit(0 if receipt["passed"] else 1)
