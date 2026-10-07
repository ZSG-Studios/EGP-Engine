"""Build an editor and embed godot-cpp bindings for its actual (including fork) API."""

import argparse
import hashlib
import importlib.util
import json
import mmap
import platform
import re
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path


def host_flags(flags, system=None, machine=None):
    """Only macOS's same-platform, 64-bit universal slices share a host API."""
    options = dict(flag.split("=", 1) for flag in flags if "=" in flag and not flag.startswith("-"))
    system = system or platform.system()
    machine = machine or platform.machine()
    aliases = {"AMD64": "x86_64", "aarch64": "arm64"}
    host_arch = aliases.get(machine, machine)
    arch = options.get("arch", "auto")
    target_platform = options.get(
        "platform", {"Windows": "windows", "Linux": "linuxbsd", "Darwin": "macos"}.get(system)
    )
    if target_platform != {"Windows": "windows", "Linux": "linuxbsd", "Darwin": "macos"}.get(system):
        raise ValueError("The editor API must be dumped on its target operating system.")
    if arch in ("auto", host_arch):
        return flags, False
    if system != "Darwin" or {arch, host_arch} != {"x86_64", "arm64"}:
        raise ValueError("Cross-architecture API export is supported only for 64-bit macOS universal slices.")
    return [flag for flag in flags if not flag.startswith("arch=")] + [f"arch={host_arch}"], True


def built_editor(root):
    env = json.loads((root / ".scons_env.json").read_text(encoding="utf-8"))
    editor = root / "bin" / ("godot" + env["PROGSUFFIX"])
    if not editor.is_file():
        editor = root / "bin" / ("godot" + env.get("PROGSUFFIX_WRAP", env["PROGSUFFIX"]))
    if not editor.is_file():
        raise RuntimeError(f"Built editor not found: {editor}")
    return editor, env


def dump_api(editor, directory):
    api = directory / "extension_api.json"
    # Never accept an old dump when an editor exits without producing a new one.
    api.unlink(missing_ok=True)
    subprocess.run(
        [str(editor), "--headless", "--dump-extension-api"],
        cwd=directory,
        check=True,
        timeout=120,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
    )
    if not api.is_file():
        raise RuntimeError("Editor exited without dumping its extension API.")
    json.loads(api.read_text(encoding="utf-8"))["header"]
    return api


def validate_sdk(root, api, env, editor=None):
    """Check the actual embedded payload, including all generated binding bytes."""
    folder = root / "editor/settings/gdextension"
    text = (folder / "native_extension_sdk.gen.h").read_text(encoding="utf-8")
    data = re.search(r"egp_cpp_sdk_data\[\]\s*=\s*\{([^}]+)\}", text)
    digest = re.search(r'egp_cpp_sdk_hash\s*=\s*"([0-9a-f]{64})"', text)
    size = re.search(r"egp_cpp_sdk_size\s*=\s*(\d+)", text)
    if not all((data, digest, size)):
        raise RuntimeError("Invalid generated SDK header.")
    compressed = bytes(int(value) for value in data[1].split(",") if value.strip())
    archive = zlib.decompress(compressed)
    if len(archive) != int(size[1]) or hashlib.sha256(archive).hexdigest() != digest[1]:
        raise RuntimeError("Embedded SDK size/hash mismatch.")
    spec = importlib.util.spec_from_file_location("egp_sdk_validation", folder / "native_extension_sdk.py")
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    bits = "32" if env["arch"] in ("x86_32", "arm32", "rv32", "wasm32") else "64"
    expected = sdk.make_archive(root / "thirdparty/godot-cpp", folder / "cpp_sdk", bits, env["precision"], api)
    if archive != expected:
        raise RuntimeError("Embedded SDK does not match this editor's API and binding sources.")
    if editor is not None:
        with editor.open("rb") as executable, mmap.mmap(executable.fileno(), 0, access=mmap.ACCESS_READ) as binary:
            if binary.find(compressed) < 0:
                raise RuntimeError("Final editor executable does not contain the validated SDK payload.")
    return {
        "api_sha256": hashlib.sha256(api.read_bytes()).hexdigest(),
        "sdk_sha256": digest[1],
        "bits": bits,
        "precision": env["precision"],
    }


def build_editor(root, flags):
    initial_flags, cross = host_flags(flags)
    api_dir = root / ".build/egp-cpp-api"
    api_dir.mkdir(parents=True, exist_ok=True)
    api = api_dir / "extension_api.json"
    command = [sys.executable, "-m", "SCons"]
    # Reuse prior bindings for the bootstrap build rather than replacing them
    # with the stock SDK. The executable still exports a fresh authoritative API.
    bootstrap = [f"egp_cpp_api={api}"] if api.is_file() else []
    subprocess.run(command + initial_flags + ["target=editor"] + bootstrap, cwd=root, check=True)
    api_editor, _ = built_editor(root)
    dump_api(api_editor, api_dir)
    subprocess.run(command + flags + ["target=editor", f"egp_cpp_api={api}"], cwd=root, check=True)
    editor, env = built_editor(root)
    if not cross:
        with tempfile.TemporaryDirectory() as temp:
            final_api = dump_api(editor, Path(temp))
            if final_api.read_bytes() != api.read_bytes():
                raise RuntimeError("Final editor API changed during SDK embedding.")
    receipt = validate_sdk(root, api, env, editor)
    receipt.update({"editor": str(editor), "api_editor": str(api_editor), "host_api_for_universal_slice": cross})
    (api_dir / "sdk-receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    return editor, json.loads(api.read_text(encoding="utf-8"))["header"]


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
    editor, header = build_editor(root, flags)
    print(f"EGP_CPP_EDITOR_READY: {editor}; embedded API {header['version_major']}.{header['version_minor']}")


if __name__ == "__main__":
    main()
