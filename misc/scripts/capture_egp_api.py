"""Capture extension API and ClassDB metadata from one unchanged editor binary."""

import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / ".build/egp-api-captures")
    args = parser.parse_args()
    engine = args.engine.resolve()
    output = args.output.resolve() / str(time.time_ns())
    output.mkdir(parents=True)
    receipt = {"passed": False, "engine": str(engine), "steps": []}
    try:
        receipt["engine_sha256"] = digest(engine)
        expected_engine = engine
        if engine.name.endswith(".console.exe"):
            expected_engine = engine.with_name(engine.name.removesuffix(".console.exe") + ".exe")
        expected_hash = digest(expected_engine)
        (output / "project.godot").write_text('config_version=5\n[application]\nconfig/name="EGP API capture"\n')
        api = output / "extension_api.json"
        reflection = output / "classdb.json"
        commands = [
            ("extension-api", [str(engine), "--headless", "--path", str(output), "--dump-extension-api"]),
            (
                "classdb",
                [
                    str(engine),
                    "--headless",
                    "--path",
                    str(output),
                    "--script",
                    str(ROOT / "misc/scripts/dump_egp_classdb.gd"),
                    "--",
                    "--api=" + str(api),
                    "--output=" + str(reflection),
                ],
            ),
        ]
        for name, command in commands:
            result = subprocess.run(command, cwd=output, capture_output=True, text=True, timeout=120)
            log = result.stdout + result.stderr
            (output / (name + ".log")).write_text(log, encoding="utf-8")
            receipt["steps"].append({"name": name, "command": command, "exit_code": result.returncode})
            if result.returncode or "ERROR:" in log or "SCRIPT ERROR" in log:
                raise RuntimeError(name + " failed; see " + str(output / (name + ".log")))
        actual = json.loads(reflection.read_text(encoding="utf-8"))
        if digest(engine) != receipt["engine_sha256"]:
            raise RuntimeError("Editor binary changed during capture")
        actual_engine = Path(actual["engine"])
        if actual_engine.resolve() != expected_engine.resolve() or actual["engine_sha256"] != expected_hash:
            raise RuntimeError("ClassDB capture did not use the expected unchanged editor")
        if digest(actual_engine) != actual["engine_sha256"]:
            raise RuntimeError("Actual editor binary changed during capture")
        if digest(api) != actual["extension_api_sha256"]:
            raise RuntimeError("Extension API changed during capture")
        receipt.update(
            passed=True,
            extension_api=str(api),
            classdb=str(reflection),
            extension_api_sha256=digest(api),
            classdb_sha256=digest(reflection),
            actual_engine=str(actual_engine),
            actual_engine_sha256=actual["engine_sha256"],
        )
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(("PASS" if receipt["passed"] else "FAIL") + ": " + str(output / "receipt.json"))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
