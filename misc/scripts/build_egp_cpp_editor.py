"""Build an editor and embed godot-cpp bindings for its actual (including fork) API."""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("scons_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    flags = args.scons_args
    if flags and flags[0] == "--":
        flags = flags[1:]
    if any(flag.startswith(("target=", "egp_cpp_api=")) for flag in flags):
        parser.error("This command sets target=editor and egp_cpp_api automatically.")
    root = Path(__file__).resolve().parents[2]
    api_dir = root / ".build/egp-cpp-api"
    api_dir.mkdir(parents=True, exist_ok=True)
    api = api_dir / "extension_api.json"
    command = [sys.executable, "-m", "SCons", *flags, "target=editor"]
    subprocess.run(command + ([f"egp_cpp_api={api}"] if api.exists() else []), cwd=root, check=True)
    env = json.loads((root / ".scons_env.json").read_text(encoding="utf-8"))
    suffix = env["PROGSUFFIX"]
    editor = root / "bin" / ("godot" + suffix)
    if not editor.is_file():
        editor = root / "bin" / ("godot" + env.get("PROGSUFFIX_WRAP", suffix))
    # The GUI executable supports redirected headless output directly. Avoid
    # console-wrapper lifetime issues when its child exits during API export.
    subprocess.run(
        [str(editor), "--headless", "--dump-extension-api"],
        cwd=api_dir,
        check=True,
        timeout=120,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    subprocess.run(command + [f"egp_cpp_api={api}"], cwd=root, check=True)
    header = json.loads(api.read_text(encoding="utf-8"))["header"]
    print(f"EGP_CPP_EDITOR_READY: {editor}; embedded API {header['version_major']}.{header['version_minor']}")


if __name__ == "__main__":
    main()
