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


def strip_documentation(value):
    """Remove only dump documentation fields, retaining every ABI/signature field."""
    if isinstance(value, dict):
        return {
            key: strip_documentation(item)
            for key, item in value.items()
            if key not in ("description", "brief_description")
        }
    if isinstance(value, list):
        return [strip_documentation(item) for item in value]
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / ".build/egp-api-captures")
    parser.add_argument("--include-docs", action="store_true", help="Also capture and pair the compiled editor help")
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
        docs_api = None
        if args.include_docs:
            docs_output = output / "compiled-docs"
            docs_output.mkdir()
            (docs_output / "project.godot").write_bytes((output / "project.godot").read_bytes())
            docs_api = docs_output / "extension_api.json"
            commands.append((
                "compiled-docs",
                [str(engine), "--headless", "--path", str(docs_output), "--dump-extension-api-with-docs"],
            ))
        for name, command in commands:
            directory = docs_api.parent if name == "compiled-docs" else output
            result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=120)
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
        if docs_api:
            plain = json.loads(api.read_text(encoding="utf-8"))
            compiled = json.loads(docs_api.read_text(encoding="utf-8"))
            if strip_documentation(compiled) != plain:
                raise RuntimeError("Compiled documentation API differs from the paired extension API")
            receipt.update(compiled_docs=str(docs_api), compiled_docs_sha256=digest(docs_api))
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
