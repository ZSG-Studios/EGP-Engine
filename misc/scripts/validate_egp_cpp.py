"""Validate built-in C++ CLI and real Debug/Release exports on the host platform."""

import argparse
import json
import platform
import subprocess
import time
import zipfile
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--editor", required=True)
    parser.add_argument("--debug-template", required=True)
    parser.add_argument("--release-template")
    parser.add_argument("--debug-only", action="store_true")
    parser.add_argument("--output", default=".build/cpp-qualification")
    args = parser.parse_args()
    if not args.debug_only and not args.release_template:
        parser.error("--release-template is required unless --debug-only is selected")
    root = Path(__file__).resolve().parents[2]
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    project = output / ("project-" + str(time.time_ns()))
    project.mkdir()
    editor = str(Path(args.editor).resolve())
    host = platform.system()
    export_platform = {"Windows": "Windows Desktop", "Linux": "Linux", "Darwin": "macOS"}[host]
    arch = "arm64" if platform.machine().lower() in ("arm64", "aarch64") else "x86_64"
    results = []

    def run(name, command, timeout=1800, expected=0, marker=None):
        started = time.monotonic()
        log = output / (name + ".log")
        with log.open("w", encoding="utf-8") as stream:
            process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                if host == "Windows":
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
                else:
                    process.kill()
                raise RuntimeError(f"{name} timed out; see {log}")
        text = log.read_text(encoding="utf-8", errors="replace")
        results.append({"check": name, "exit_code": code, "seconds": round(time.monotonic() - started, 2)})
        (output / "results.json").write_text(json.dumps(results, indent=2), encoding="utf-8")
        if code != expected or (marker and marker not in text) or (expected == 0 and "ERROR:" in text):
            raise RuntimeError(f"{name} failed ({code}, expected {expected}); see {log}\n{text[-5000:]}")
        print(f"PASS: {name}", flush=True)

    (project / "project.godot").write_text(
        'config_version=5\n[application]\nconfig/name="CppSmoke"\nrun/main_scene="res://main.tscn"\n'
        "[rendering]\ntextures/vram_compression/import_etc2_astc=true\n",
        encoding="utf-8",
    )
    (project / "main.gd").write_text(
        """extends Node
func _ready():
    var instance = ClassDB.instantiate("EGP_smoke_Node")
    if instance == null:
        get_tree().quit(1)
        return
    var message = instance.get_message()
    instance.free()
    if message != "Hello from smoke!":
        get_tree().quit(1)
        return
    print("EGP_CPP_GAME_PASSED")
    get_tree().quit(0)
""",
        encoding="utf-8",
    )
    (project / "main.tscn").write_text(
        '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://main.gd" id="1"]\n[node name="CppSmoke" type="Node"]\nscript = ExtResource("1")\n',
        encoding="utf-8",
    )
    cli = [editor, "--headless", "--editor", "--max-fps", "30", "--path", str(project), "--"]
    run(
        "cli-check-create",
        cli + ["--cpp-check", "--cpp-create=smoke"],
        marker="EGP_CPP_CLI_PASSED",
    )
    # Build in a separate editor session, matching the interactive Create/Build
    # workflow and ensuring discovery is not rescued by the initial project scan.
    run("cli-build-debug", cli + ["--cpp-build=smoke:debug"], marker="EGP_CPP_CLI_PASSED")
    run(
        "native-game-after-first-build",
        [editor, "--headless", "--max-fps", "30", "--path", str(project)],
        timeout=60,
        marker="EGP_CPP_GAME_PASSED",
    )
    run("cli-build-release", cli + ["--cpp-build=smoke:release"], marker="EGP_CPP_CLI_PASSED")
    run("cli-invalid-option", cli + ["--cpp-unknown"], expected=1)
    run("cli-invalid-build-config", cli + ["--cpp-build=smoke:invalid"], expected=1)
    run("cli-missing-xmake", cli + ["--cpp-xmake=/egp/does/not/exist/xmake", "--cpp-check"], expected=1)
    run("cli-duplicate-create", cli + ["--cpp-create=smoke"], expected=1)
    run(
        "native-game",
        [editor, "--headless", "--max-fps", "30", "--path", str(project)],
        timeout=60,
        marker="EGP_CPP_GAME_PASSED",
    )

    templates = [Path(args.debug_template).resolve(), Path(args.release_template or args.debug_template).resolve()]
    if host == "Darwin":
        archive = output / "macos-templates.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
            source = root / "misc/dist/macos_template.app"
            for path in source.rglob("*"):
                if path.is_file():
                    package.write(path, "macos_template.app/" + path.relative_to(source).as_posix())
            for config, template in zip(("debug", "release"), templates):
                package.write(template, f"macos_template.app/Contents/MacOS/godot_macos_{config}.{arch}")
        templates = [archive, archive]
    options = f"custom_template/debug={json.dumps(templates[0].as_posix())}\ncustom_template/release={json.dumps(templates[1].as_posix())}\n"
    options += f'binary_format/architecture="{arch}"\napplication/modify_resources=false\napplication/bundle_identifier="com.egp.cppsmoke"\ncodesign/codesign=0\n'
    (project / "export_presets.cfg").write_text(
        f'[preset.0]\nname="Host"\nplatform="{export_platform}"\nrunnable=true\nexport_filter="all_resources"\ninclude_filter=""\nexclude_filter=""\n[preset.0.options]\n{options}',
        encoding="utf-8",
    )
    for config in ("debug",) if args.debug_only else ("debug", "release"):
        folder = output / config
        folder.mkdir(exist_ok=True)
        game = folder / ("CppSmoke.exe" if host == "Windows" else "CppSmoke.app" if host == "Darwin" else "CppSmoke")
        run(
            "export-" + config,
            [editor, "--headless", "--path", str(project), "--export-" + config, "Host", str(game)],
            timeout=180,
        )
        if host == "Darwin":
            game = game / "Contents/MacOS/CppSmoke"
        if host == "Windows":
            # Console wrapper is part of the Windows template build and makes stdout observable.
            console = Path(str(game).replace(".exe", ".console.exe"))
            wrapper = Path(
                str(Path(args.debug_template if config == "debug" else args.release_template).resolve()).replace(
                    ".exe", ".console.exe"
                )
            )
            import shutil

            shutil.copy2(wrapper, console)
            game = console
        else:
            game.chmod(game.stat().st_mode | 0o111)
        run(
            "exported-game-" + config,
            [str(game), "--headless", "--max-fps", "30"],
            timeout=60,
            marker="EGP_CPP_GAME_PASSED",
        )
    print("EGP_CPP_QUALIFICATION_PASSED", flush=True)


if __name__ == "__main__":
    main()
