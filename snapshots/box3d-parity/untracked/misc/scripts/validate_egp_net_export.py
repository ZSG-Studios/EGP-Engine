#!/usr/bin/env python3
"""Export and run EGP's pure GDScript networking fixtures on Windows."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--engine", type=Path, required=True)
parser.add_argument("--template", type=Path, required=True)
args = parser.parse_args()
engine, template = args.engine.resolve(), args.template.resolve()
if sys.platform != "win32" or not engine.is_file() or not template.is_file():
    parser.error("Supply an existing Windows EGP editor and matching native debug template")
output = ROOT / ".build/egp-net-export" / str(time.time_ns())
project = output / "project"
shutil.copytree(ROOT / "modules/egp_net/samples/gdscript", project,
                ignore=shutil.ignore_patterns(".godot", "export_presets.cfg"))
staging = output / "staging"
staging.mkdir()
game = staging / "EGP.NetworkSmoke.exe"
(project / "export_presets.cfg").write_text(f'''[preset.0]
name="Windows Native Network"
platform="Windows Desktop"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter=""

[preset.0.options]
custom_template/debug="{template.as_posix()}"
custom_template/release=""
binary_format/architecture="x86_64"
binary_format/embed_pck=false
application/modify_resources=false
debug/export_console_wrapper=0
''', encoding="utf-8")
receipt = {"passed": False, "editor_sha256": hashlib.sha256(engine.read_bytes()).hexdigest(),
           "template_sha256": hashlib.sha256(template.read_bytes()).hexdigest(), "source_hashes": {}}
for source in sorted((ROOT / "modules/egp_net").rglob("*")):
    if source.is_file() and ".godot" not in source.parts:
        receipt["source_hashes"][source.relative_to(ROOT).as_posix()] = hashlib.sha256(source.read_bytes()).hexdigest()


def run(name, command, timeout, cwd=ROOT):
    print("START " + name, flush=True)
    log_path = output / (name + ".log")
    with log_path.open("w", encoding="utf-8") as log:
        result = subprocess.run([str(item) for item in command], cwd=cwd, stdout=log,
                                stderr=subprocess.STDOUT, timeout=timeout)
    if result.returncode:
        raise RuntimeError(f"{name} failed ({result.returncode}); see {log_path}")
    return log_path.read_text(encoding="utf-8", errors="replace")


try:
    run("import", [engine, "--headless", "--import", "--path", project, "--max-fps", "30"], 120)
    run("export", [engine, "--headless", "--path", project, "--max-fps", "30", "--export-debug", "Windows Native Network", game], 180)
    # Run from a relocated directory with no source project or import cache.
    relocated = output / "relocated"
    shutil.copytree(staging, relocated)
    game = relocated / game.name
    if not game.with_suffix(".pck").is_file():
        raise RuntimeError("Export did not produce the expected standalone PCK")
    receipt["bundle_sha256"] = {source.name: hashlib.sha256(source.read_bytes()).hexdigest()
                                for source in sorted(relocated.iterdir()) if source.is_file()}
    receipt["game_sha256"] = hashlib.sha256(game.read_bytes()).hexdigest()
    receipt["game_path"] = str(game)
    for name, scene, marker, timeout, fps in [
        ("aio", "Smoke.tscn", "EGP_GDSCRIPT_AIO", 45, 60),
        ("physics", "Physics.tscn", "EGP_NETWORK_PHYSICS", 30, 60),
        ("token", "Token.tscn", "EGP_NETWORK_TOKEN", 20, 60),
        ("prediction", "Prediction.tscn", "EGP_NETWORK_PREDICTION", 30, 30),
        ("lifecycle", "Lifecycle.tscn", "EGP_NETWORK_LIFECYCLE", 20, 60),
    ]:
        text = run(name, [game, "--headless", "--max-fps", str(fps), "--", "--fixture=" + name], timeout, cwd=relocated)
        match = re.search(re.escape(marker) + r" (\{[^\n]+\})", text)
        if not match:
            raise RuntimeError(name + " did not provide its success marker")
        receipt[name] = json.loads(match.group(1))
        if not receipt[name]["passed"]:
            raise RuntimeError(name + " reported failure")
    text = run("processes", [sys.executable, ROOT / "misc/scripts/validate_egp_net_process.py", "--engine", game], 25)
    match = re.search(r"EGP_NETWORK_PROCESSES (\{[^\n]+\})", text)
    if not match:
        raise RuntimeError("Separate exported processes did not provide their success marker")
    receipt["processes"] = json.loads(match.group(1))
    if not receipt["processes"]["passed"]:
        raise RuntimeError("Separate exported server/client fixture failed")
    receipt["passed"] = True
except (RuntimeError, subprocess.TimeoutExpired, OSError) as failure:
    receipt["error"] = str(failure)
finally:
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
print(json.dumps({key: value for key, value in receipt.items() if key != "source_hashes"}, indent=2))
print("Receipt: " + str(output / "receipt.json"))
sys.exit(0 if receipt["passed"] else 1)
