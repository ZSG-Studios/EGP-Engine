#!/usr/bin/env python3
"""Check separate Godot processes with a test-only trusted token handoff."""

import argparse
import json
import os
import re
import subprocess
import tempfile
import time
from pathlib import Path

from egp_engine_process import resolve_engine_process

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--engine", type=Path, required=True)
parser.add_argument("--project", type=Path)
parser.add_argument("--client-language", choices=("csharp", "gdscript", "cpp"))
args = parser.parse_args()
engine, engine_process = resolve_engine_process(args.engine)
base = [str(engine), "--headless", "--max-fps", "60"]
if args.project:
    base += ["--path", str(args.project.resolve())]
base += ["--", "--fixture=processes"]
results = {}
children: list[subprocess.Popen[bytes]] = []
logs = []
output = ROOT / ".build/egp-net-processes" / str(time.time_ns())
output.mkdir(parents=True)
temporary = tempfile.TemporaryDirectory(prefix="egp-process-")
directory = Path(temporary.name)
try:
    admission = directory / "admission.bin"
    for role in ["server", "client"]:
        if role == "client":
            deadline = time.monotonic() + 10
            while not admission.is_file() or admission.stat().st_size != 2048:
                if children[0].poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("Server did not issue admission; see " + str(output))
                time.sleep(0.05)
        log = (output / (role + ".log")).open("w", encoding="utf-8")
        logs.append(log)
        language_args = ["--language=" + args.client_language] if role == "client" and args.client_language else []
        children.append(
            subprocess.Popen(
                base + ["--role=" + role, "--admission=" + str(admission)] + language_args,
                cwd=engine.parent,
                stdout=log,
                stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0) if os.name == "nt" else 0,
            )
        )
    for role, child in zip(["server", "client"], children):
        code = child.wait(timeout=15)
        logs[["server", "client"].index(role)].close()
        text = (output / (role + ".log")).read_text(encoding="utf-8", errors="replace")
        match = re.search(r"EGP_NETWORK_PROCESS (\{[^\n]+\})", text)
        if code or not match:
            raise RuntimeError(f"{role} failed ({code}); see {output}")
        results[role] = json.loads(match.group(1))
        if not results[role]["passed"]:
            raise RuntimeError(role + " reported failure; see " + str(output))
except (RuntimeError, subprocess.TimeoutExpired, OSError) as failure:
    results["error"] = str(failure)
finally:
    for child in children:
        if child.poll() is None:
            child.kill()
            child.wait(timeout=5)
    for log in logs:
        log.close()
    temporary.cleanup()
results["passed"] = "error" not in results and len(results) == 2
results["engine_process"] = engine_process
(output / "receipt.json").write_text(json.dumps(results, indent=2) + "\n", encoding="utf-8")
print("EGP_NETWORK_PROCESSES " + json.dumps(results))
raise SystemExit(0 if results["passed"] else 1)
