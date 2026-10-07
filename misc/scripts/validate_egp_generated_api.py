"""Compile direct C# and C++ networking APIs against the actual generated bindings."""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import time
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROBES = ROOT / "misc/egp/generated_api"


def extract_sdk(output):
    """Verify and unpack the exact archive compiled into the validation editor."""
    header = (ROOT / "editor/settings/gdextension/native_extension_sdk.gen.h").read_text()
    expected = re.search(r'egp_cpp_sdk_hash = "([0-9a-f]+)"', header).group(1)
    payload = header.split("egp_cpp_sdk_data[] = {", 1)[1].split("}", 1)[0]
    archive = zlib.decompress(bytes(int(value) for value in re.findall(r"\d+", payload)))
    if hashlib.sha256(archive).hexdigest() != expected:
        raise RuntimeError("Bundled SDK archive hash mismatch")
    output.mkdir(parents=True)
    offset = 0
    while offset < len(archive):
        name_size, data_size = struct.unpack_from("<II", archive, offset)
        offset += 8
        name = archive[offset : offset + name_size].decode("utf-8")
        offset += name_size
        target = (output / name).resolve()
        if not target.is_relative_to(output.resolve()) or offset + data_size > len(archive):
            raise RuntimeError("Invalid bundled SDK archive entry")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(archive[offset : offset + data_size])
        offset += data_size
    (output / ".complete").write_text(expected + "\n")
    return expected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpp", action="store_true", help="Compile against the current bundled editor SDK")
    parser.add_argument("--sdk", type=Path, help="Use an already extracted SDK")
    parser.add_argument(
        "--managed-assembly", type=Path, help="Direct GodotSharp assembly reference; avoids stale NuGet packages"
    )
    parser.add_argument("--output", type=Path, default=ROOT / ".build/generated-api")
    args = parser.parse_args()
    if not (args.cpp or args.sdk or args.managed_assembly):
        parser.error("Select --cpp, --sdk, and/or --managed-assembly")
    output = args.output.resolve() / str(time.time_ns())
    output.mkdir(parents=True)
    receipt = {
        "passed": False,
        "scope": "Direct typed generated API compilation; runtime is qualified separately.",
        "commands": [],
    }

    def run(label, command):
        path = output / (label + ".log")
        with path.open("w", encoding="utf-8") as log:
            result = subprocess.run(
                command,
                cwd=ROOT,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=300,
                **({"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}),
            )
        receipt["commands"].append({"name": label, "command": command, "exit_code": result.returncode})
        if result.returncode:
            raise RuntimeError(label + " failed: " + str(path))

    try:
        if args.managed_assembly:
            assembly = args.managed_assembly.resolve()
            receipt["assembly_sha256"] = hashlib.sha256(assembly.read_bytes()).hexdigest()
            run(
                "csharp",
                [
                    "dotnet",
                    "build",
                    str(PROBES / "csharp/ApiProbe.csproj"),
                    "--nologo",
                    "-p:GodotSharpAssembly=" + str(assembly),
                    "-p:BaseIntermediateOutputPath=" + str(output / "obj") + os.sep,
                    "-o",
                    str(output / "managed"),
                ],
            )
        if args.cpp or args.sdk:
            sdk = args.sdk.resolve() if args.sdk else output / "sdk"
            if not args.sdk:
                receipt["sdk_archive_sha256"] = extract_sdk(sdk)
            receipt["sdk_metadata"] = json.loads((sdk / "sdk.json").read_text())
            for name in (
                "superposition",
                "superposition_config",
                "superposition_property",
                "egp_net_snapshot_interpolator",
            ):
                if not (sdk / "gen/include/godot_cpp/classes" / (name + ".hpp")).is_file():
                    raise RuntimeError("Matching SDK header missing: " + name)
            cmake = shutil.which("cmake") or "cmake"
            command = [cmake, "-S", str(PROBES / "cpp"), "-B", str(output / "native"), "-DEGP_CPP_SDK=" + str(sdk)]
            if os.name == "nt":
                command.extend(["-A", "x64"])
            run("cpp-configure", command)
            run("cpp-build", [cmake, "--build", str(output / "native"), "--config", "Debug", "--parallel", "2"])
        receipt["passed"] = True
    except (OSError, ValueError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(("PASS" if receipt["passed"] else "FAIL") + ": " + str(output / "receipt.json"))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
