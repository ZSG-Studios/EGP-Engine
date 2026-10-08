"""Exercise the C++ editor panel through an EditorPlugin and orderly editor shutdown."""

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / ".build/egp-cpp-ui")
    parser.add_argument(
        "--capture-ui",
        action="store_true",
        help="Render the disposable editor hidden and capture embedded panel screenshots; no input injection",
    )
    parser.add_argument("--cache-recovery-only", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args.cache_recovery_only and not (os.name == "nt" or sys.platform.startswith("linux")):
        parser.error("Cache recovery requires verified Windows/Linux editor-state isolation")
    engine = args.engine.resolve()
    output = args.output.resolve() / str(time.time_ns())
    output.mkdir(parents=True)
    project = output / "project"
    addon = project / "addons/cpp_smoke"
    addon.mkdir(parents=True)
    (project / "project.godot").write_text(
        'config_version=5\n[application]\nconfig/name="CppUiCheck"\n[editor_plugins]\n'
        'enabled=PackedStringArray("res://addons/cpp_smoke/plugin.cfg")\n',
        encoding="utf-8",
    )
    (addon / "plugin.cfg").write_text(
        '[plugin]\nname="CppSmoke"\ndescription="Editor regression fixture"\nauthor="EGP"\n'
        'version="1"\nscript="editor_smoke.gd"\n',
        encoding="utf-8",
    )
    source = (ROOT / "misc/scripts/egp_cpp_editor_smoke.gd").read_text(encoding="utf-8")
    source = source.replace("extends SceneTree", "@tool\nextends EditorPlugin").replace(
        "func _initialize()", "func _enter_tree()"
    )
    source = source.replace("await process_frame", "await get_tree().process_frame").replace(
        "root.find_children", "get_tree().root.find_children"
    )
    source = source.replace("quit(1)", "get_tree().quit(1)")
    assert "\tquit(0)" in source
    source = source.replace(
        "\tquit(0)",
        "\tvar filesystem = EditorInterface.get_resource_filesystem()\n"
        "\tfilesystem.scan()\n\tawait filesystem.filesystem_changed\n"
        "\tvar editor_node = EditorInterface.get_base_control().get_parent()\n"
        "\teditor_node.notification.call_deferred(Node.NOTIFICATION_WM_CLOSE_REQUEST)",
    )
    # Close EditorNode after this callback returns. Recursing through the root
    # unloads this plugin during traversal and creates a false teardown failure.
    (addon / "editor_smoke.gd").write_text(source, encoding="utf-8")
    receipt = {
        "passed": False,
        "engine": str(engine),
        "engine_sha256": hashlib.sha256(engine.read_bytes()).hexdigest(),
        "fixture_sha256": hashlib.sha256(source.encode()).hexdigest(),
        "project": str(project),
        "log": str(output / "editor.log"),
        "scope": "Headless editor controls, build/diagnostics/reload and clean shutdown; no interactive graphics or running-game reload claim.",
    }
    with (output / "editor.log").open("w", encoding="utf-8") as log:
        command = [str(engine), "--editor", "--max-fps", "30", "--path", str(project)]
        if not args.capture_ui:
            command.insert(1, "--headless")
        else:
            command.extend(["--rendering-method", "forward_plus"])
        launch_options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        environment = os.environ.copy()
        environment["EGP_CPP_UI_CACHE_ONLY"] = "1" if args.cache_recovery_only else "0"
        # Recovery tests may damage only this disposable editor's SDK extraction.
        # macOS system paths ignore XDG variables; its cache remains untouched.
        isolated_cache = args.cache_recovery_only and (os.name == "nt" or sys.platform.startswith("linux"))
        receipt["cache_recovery_required"] = isolated_cache
        if isolated_cache:
            cache_root = output / "editor-state"
            cache_root.mkdir()
            environment["EGP_CPP_UI_CACHE_ROOT"] = str(cache_root)
            if os.name == "nt":
                environment["LOCALAPPDATA"] = str(cache_root / "local")
                environment["APPDATA"] = str(cache_root / "roaming")
            else:
                environment["XDG_CACHE_HOME"] = str(cache_root / "cache")
                environment["XDG_CONFIG_HOME"] = str(cache_root / "config")
                environment["XDG_DATA_HOME"] = str(cache_root / "data")
        else:
            environment.pop("EGP_CPP_UI_CACHE_ROOT", None)
        if args.capture_ui:
            capture_dir = output / "screenshots"
            capture_dir.mkdir()
            environment["EGP_CPP_UI_CAPTURE"] = str(capture_dir)
            if os.name == "nt":
                startup_info = subprocess.STARTUPINFO()
                startup_info.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup_info.wShowWindow = subprocess.SW_HIDE
                launch_options["startupinfo"] = startup_info
        else:
            environment.pop("EGP_CPP_UI_CAPTURE", None)
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, env=environment, **launch_options)
        try:
            receipt["exit_code"] = process.wait(timeout=900)
        except subprocess.TimeoutExpired:
            receipt["timed_out"] = True
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
            else:
                process.kill()
            receipt["exit_code"] = process.wait(timeout=10)
    text = (output / "editor.log").read_text(encoding="utf-8", errors="replace")
    receipt["passed"] = (
        receipt["exit_code"] == 0
        and not receipt.get("timed_out")
        and (
            "EGP_CPP_CACHE_ONLY_PASSED" in text
            if args.cache_recovery_only
            else "EGP_CPP_EDITOR_SMOKE_PASSED" in text and "EGP_CPP_DESCRIPTOR_FAILURE_PASSED" in text
        )
        and (not receipt["cache_recovery_required"] or "EGP_CPP_CACHE_RECOVERY_PASSED" in text)
        and all(marker not in text for marker in ("ERROR:", "leaked", "Scan thread aborted"))
    )
    if args.cache_recovery_only:
        receipt["scope"] = (
            "Isolated SDK extraction recovery for invalid identity and missing required files; no compiler or reload."
        )
    if args.capture_ui:
        receipt["screenshots"] = [str(path) for path in sorted((output / "screenshots").glob("*.png"))]
        receipt["passed"] = receipt["passed"] and len(receipt["screenshots"]) == 3
        receipt["scope"] = (
            "Hidden renderer editor control/build/diagnostic/reload fixture with panel screenshots; no physical input or running-game reload claim."
        )
    if receipt["passed"] and not args.cache_recovery_only and (os.name == "nt" or sys.platform.startswith("linux")):
        # Run cache damage separately: the regular UI reuses the existing compiled
        # SDK cache, while this isolated editor exercises extraction without builds.
        cache_output = output / "cache-recovery"
        cache_result = main(["--engine", str(engine), "--output", str(cache_output), "--cache-recovery-only"])
        receipt["cache_recovery_receipts"] = [str(path) for path in cache_output.glob("*/receipt.json")]
        receipt["passed"] = cache_result == 0
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(f"{'PASS' if receipt['passed'] else 'FAIL'}: {output / 'receipt.json'}")
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
