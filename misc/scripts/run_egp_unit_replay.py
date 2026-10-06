"""Replay committed tests with an explicitly identified, previously built editor."""

import argparse
import hashlib
import json
import re
import subprocess
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--expected-source", required=True)
    parser.add_argument("--expected-sha256", required=True)
    parser.add_argument("--output", type=Path, default=Path(".build/unit-replay"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    engine = args.engine.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    receipt = {
        "passed": False,
        "input_source": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
        "engine_source": args.expected_source,
        "engine_sha256": hashlib.sha256(engine.read_bytes()).hexdigest(),
        "scope": "Previously built editor replaying current committed unit and GDScript test inputs; this does not build or qualify a new engine binary.",
        "steps": [],
    }

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    save()
    if receipt["engine_sha256"] != args.expected_sha256:
        raise RuntimeError("Editor hash does not match the selected artifact")
    profiles = [
        ("version", ["--version"]),
        ("gdscript", ["--headless", "--test", "--no-colors", "--test-case=Script compilation and runtime"]),
        ("full-unit-suite", ["--headless", "--test", "--no-colors"]),
    ]
    for name, flags in profiles:
        command = [str(engine), *flags]
        log = output / (name + ".log")
        started = time.monotonic()
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run(command, cwd=root, stdout=stream, stderr=subprocess.STDOUT, timeout=300)
        text = log.read_text(encoding="utf-8", errors="replace")
        receipt["steps"].append({
            "name": name,
            "command": command,
            "exit_code": result.returncode,
            "seconds": round(time.monotonic() - started, 3),
            "log_sha256": hashlib.sha256(log.read_bytes()).hexdigest(),
        })
        save()
        if result.returncode != 0:
            raise RuntimeError(f"{name} failed; see {log}")
        if name == "version":
            if args.expected_source[:9] not in text:
                raise RuntimeError("Editor source identity does not match")
        elif "[doctest]" not in text or not re.search(r"\b0 failed\b", text):
            raise RuntimeError(f"Missing successful test summary for {name}")
        print(f"PASS: {name}", flush=True)
    receipt["passed"] = True
    save()


if __name__ == "__main__":
    main()
