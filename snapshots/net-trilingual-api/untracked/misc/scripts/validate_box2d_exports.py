#!/usr/bin/env python3
"""Export and qualify native Box2D capabilities in relocated Debug/Release games."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

from validate_box2d_scene import validate_result

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--editor", type=Path, required=True)
    parser.add_argument("--debug-template", type=Path, required=True)
    parser.add_argument("--release-template", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    editor = args.editor.resolve()
    receipt = {
        "passed": False,
        "editor": str(editor),
        "editor_sha256": digest(editor),
        "steps": [],
        "cross_platform_qualified": False,
    }

    def run(label, command, cwd, timeout, fixture=None):
        started = time.monotonic()
        entry = {"name": label, "command": [str(x) for x in command], "timeout_seconds": timeout, "passed": False}
        receipt["steps"].append(entry)
        log_path = output / (label + ".log")
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.Popen(
                entry["command"],
                cwd=cwd,
                stdout=log,
                stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            try:
                entry["exit_code"] = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
                else:
                    process.kill()
                process.wait(timeout=15)
                raise RuntimeError(label + " watchdog expired")
        text = log_path.read_text(encoding="utf-8")
        entry["elapsed_seconds"] = round(time.monotonic() - started, 3)
        entry["passed"] = entry["exit_code"] == 0 and "ERROR:" not in text and "SCRIPT ERROR" not in text
        if fixture:
            marker = re.search(
                r'"(RESULT: PASS[^\"]*)"', (ROOT / "tests/physics/box2d/scene" / fixture).read_text(encoding="utf-8")
            )
            if marker is None:
                raise RuntimeError("Fixture has no specific completion marker: " + fixture)
            entry["completion_marker"] = marker[1]
            entry.update(validate_result(entry["exit_code"], text, fixture, marker[1]))
        print(label + (" PASS" if entry["passed"] else " FAIL"), flush=True)
        if not entry["passed"]:
            raise RuntimeError(label + " failed; see " + str(log_path))

    try:
        source = ROOT / "tests/physics/box2d/scene"
        fixtures = [
            "backend_activation_test.gd",
            "canvas_cast_test.gd",
            "native_capabilities_test.gd",
            "invalid_parameters.gd",
            "convex_input_test.gd",
            "invalid_convex_input.gd",
        ]
        project = output / "project"
        project.mkdir(exist_ok=True)
        project_text = (
            (source / "project.godot")
            .read_text(encoding="utf-8")
            .replace(
                "[application]",
                '[application]\nrun/main_loop_type="Box2DExportAcceptance"\nrun/main_scene="res://main.tscn"',
            )
        )
        (project / "project.godot").write_text(project_text, encoding="utf-8")
        (project / "main.tscn").write_text(
            '[gd_scene format=3]\n\n[node name="Fixture" type="Node"]\n', encoding="utf-8"
        )
        receipt["fixtures_sha256"] = {}
        combined = """class_name Box2DExportAcceptance
extends SceneTree

func _initialize() -> void:
	var selected := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--fixture="):
			selected = argument.trim_prefix("--fixture=").trim_suffix(".gd")
	if has_method("initialize_" + selected):
		call("initialize_" + selected)
	else:
		print("RESULT: FAIL - unknown exported fixture")
		quit(1)

"""
        for name in fixtures:
            text = (source / name).read_text(encoding="utf-8")
            (project / name).write_text(text, encoding="utf-8")
            # Merge unchanged fixture bodies into one MainLoop, namespacing their
            # functions/variables; release templates disable CLI script overrides.
            stem = Path(name).stem
            renamed = {
                "_initialize": "initialize_" + stem,
                "run": "run_" + stem,
                "require": "require_" + stem,
                "failures": "failures_" + stem,
            }
            combined += (
                re.sub(
                    r"\b(_initialize|run|require|failures)\b",
                    lambda m: renamed[m[1]],
                    text.replace("extends SceneTree\n", "", 1),
                )
                + "\n"
            )
            receipt["fixtures_sha256"][name] = {
                "source": digest(source / name),
                "packaged_input": digest(project / name),
            }
        (project / "MainLoop.gd").write_text(combined, encoding="utf-8")
        receipt["combined_main_loop_sha256"] = digest(project / "MainLoop.gd")
        run(
            "cold-import",
            [editor, "--headless", "--editor", "--path", project, "--import", "--max-fps", "30"],
            project,
            120,
        )
        for configuration, candidate in [("debug", args.debug_template), ("release", args.release_template)]:
            template = candidate.resolve()
            (project / "export_presets.cfg").write_text(
                f'''[preset.0]
name="Box2D Acceptance"
platform="Windows Desktop"
runnable=true
export_filter="all_resources"
include_filter="*.gd"
exclude_filter=""

[preset.0.options]
custom_template/{configuration}="{template.as_posix()}"
binary_format/architecture="x86_64"
binary_format/embed_pck=false
application/modify_resources=false
debug/export_console_wrapper=0
''',
                encoding="utf-8",
            )
            staging = output / ("staging-" + configuration)
            staging.mkdir(exist_ok=True)
            game = staging / "EGP.Box2DAcceptance.exe"
            run(
                "export-" + configuration,
                [
                    editor,
                    "--headless",
                    "--path",
                    project,
                    "--max-fps",
                    "30",
                    "--export-" + configuration,
                    "Box2D Acceptance",
                    game,
                ],
                project,
                300,
            )
            relocated = output / ("relocated-" + configuration)
            shutil.copytree(staging, relocated, dirs_exist_ok=True)
            game = relocated / game.name
            if not game.with_suffix(".pck").is_file():
                raise RuntimeError("Export did not produce an adjacent PCK")
            receipt[configuration] = {
                "template_sha256": digest(template),
                "bundle_sha256": {
                    str(p.relative_to(relocated)): digest(p) for p in sorted(relocated.rglob("*")) if p.is_file()
                },
            }
            for fixture in fixtures:
                command = [game, "--headless", "--max-fps", "60", "--", "--fixture=" + fixture]
                run(configuration + "-" + Path(fixture).stem, command, relocated, 60, fixture)
        receipt["passed"] = True
    except (OSError, RuntimeError) as error:
        receipt["failure"] = str(error)
        print(error, file=sys.stderr)
    finally:
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
