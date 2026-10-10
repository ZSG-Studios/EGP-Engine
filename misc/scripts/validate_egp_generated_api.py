"""Compile direct C# and C++ networking APIs against the actual generated bindings."""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROBES = ROOT / "misc/egp/generated_api"


def extract_sdk(output, engine, api_sha256, sdk_header=None):
    """Unpack a native-build SDK proven present in the selected final editor."""
    binary = (
        engine.with_name(engine.name.removesuffix(".console.exe") + ".exe")
        if engine.name.endswith(".console.exe")
        else engine
    )
    compiled_bytes = binary.read_bytes()
    binary_hash = hashlib.sha256(compiled_bytes).hexdigest()
    if sdk_header:
        candidates = [sdk_header.resolve()]
    else:
        # CI runners keep persistent native graphs outside the checkout (EGP_BUILD_CACHE).
        roots = [ROOT / ".build"]
        if os.environ.get("EGP_BUILD_CACHE"):
            roots.append(Path(os.environ["EGP_BUILD_CACHE"]))
        candidates = [
            stamp.parent.parent / "generated/editor/settings/gdextension/native_extension_sdk.gen.h"
            for root in roots
            if root.is_dir()
            for stamp in root.glob("**/engine-api/sdk-stamp.json")
        ]
    for candidate in candidates:
        # Accept native graph output only, never historical source-tree headers.
        if not candidate.is_file() or len(candidate.parents) < 5 or candidate.parents[3].name != "generated":
            continue
        stamp_path = candidate.parents[4] / "engine-api/sdk-stamp.json"
        if not stamp_path.is_file():
            continue
        stamp = json.loads(stamp_path.read_text())
        header_bytes = candidate.read_bytes()
        if stamp.get("api_hash") != api_sha256 or stamp.get("header_hash") != hashlib.sha256(header_bytes).hexdigest():
            continue
        header = header_bytes.decode("utf-8")
        hash_match = re.search(r'egp_cpp_sdk_hash = "([0-9a-f]{64})"', header)
        payload_match = re.search(r"egp_cpp_sdk_data\[\] = \{([^}]*)\}", header)
        if not hash_match or not payload_match:
            raise RuntimeError("Invalid native-build SDK header: " + str(candidate))
        expected = hash_match.group(1)
        payload_text = payload_match.group(1)
        payload = bytes(int(value) for value in re.findall(r"\d+", payload_text))
        if not payload or payload not in compiled_bytes:
            continue
        archive = zlib.decompress(payload)
        if hashlib.sha256(archive).hexdigest() != expected:
            raise RuntimeError("Compiled SDK archive hash mismatch")
        output.mkdir(parents=True, exist_ok=True)
        offset = 0
        files = {}
        while offset < len(archive):
            if offset + 8 > len(archive):
                raise RuntimeError("Truncated SDK archive entry")
            name_size, data_size = struct.unpack_from("<II", archive, offset)
            offset += 8
            if offset + name_size + data_size > len(archive):
                raise RuntimeError("Truncated SDK archive payload")
            name = archive[offset : offset + name_size].decode("utf-8")
            offset += name_size
            target = (output / name).resolve()
            if not target.is_relative_to(output.resolve()) or name in files:
                raise RuntimeError("Invalid or duplicate SDK archive entry")
            files[name] = archive[offset : offset + data_size]
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(files[name])
            offset += data_size
        if "sdk.json" not in files:
            raise RuntimeError("Compiled SDK metadata is missing")
        metadata = json.loads(files["sdk.json"])
        fingerprint = b"".join(
            name.encode() + hashlib.sha256(files[name]).digest() for name in sorted(files) if name != "sdk.json"
        )
        if metadata.get("api_sha256") != api_sha256 or metadata.get("build_system") != "xmake":
            raise RuntimeError("Compiled SDK does not match the final editor's API/native build contract")
        if metadata.get("source_sha256") != hashlib.sha256(fingerprint).hexdigest():
            raise RuntimeError("Compiled SDK source fingerprint mismatch")
        (output / ".complete").write_text(expected)
        return {
            "sdk_archive_sha256": expected,
            "sdk_header": str(candidate),
            "sdk_header_sha256": hashlib.sha256(header_bytes).hexdigest(),
            "sdk_stamp": str(stamp_path),
            "compiled_binary": str(binary),
            "compiled_binary_sha256": binary_hash,
        }
    raise RuntimeError("No native-build SDK header matches both the final editor payload and freshly captured API")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--engine", type=Path, help="Final editor used to capture and qualify its exact API/embedded SDK"
    )
    parser.add_argument(
        "--sdk-header", type=Path, help="Explicit native graph generated SDK header (requires matching SDK stamp)"
    )
    parser.add_argument("--cpp", action="store_true", help="Compile against the current bundled editor SDK")
    parser.add_argument("--sdk", type=Path, help="Use an already extracted SDK")
    parser.add_argument(
        "--managed-assembly", type=Path, help="Direct GodotSharp assembly reference; avoids stale NuGet packages"
    )
    parser.add_argument("--output", type=Path, default=ROOT / ".build/generated-api")
    args = parser.parse_args()
    if not (args.cpp or args.sdk or args.managed_assembly):
        parser.error("Select --cpp, --sdk, and/or --managed-assembly")
    if args.cpp and not args.sdk and not args.engine:
        parser.error("Automatic bundled SDK qualification requires --engine pointing to the final editor")
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
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
                cwd=PROBES / "cpp" if label.startswith("cpp-") else ROOT,
                env=dict(
                    os.environ,
                    XMAKE_CONFIGDIR=str(output / "xmake-config"),
                    XMAKE_GLOBALDIR=os.environ.get("XMAKE_GLOBALDIR", str(output / "xmake-global")),
                ),
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=300,
                **({"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}),
            )
        receipt["commands"].append({"name": label, "command": command, "exit_code": result.returncode})
        if result.returncode:
            raise RuntimeError(label + " failed: " + str(path))

    try:
        api_sha256 = None
        if args.engine:
            engine = args.engine.resolve()
            receipt["engine"] = str(engine)
            receipt["engine_sha256"] = hashlib.sha256(engine.read_bytes()).hexdigest()
            api_output = output / "engine-api"
            api_output.mkdir(exist_ok=True)
            with (api_output / "capture.log").open("w", encoding="utf-8") as log:
                capture = subprocess.run(
                    [str(engine), "--headless", "--dump-extension-api"],
                    cwd=api_output,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    timeout=120,
                    **({"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}),
                )
            text = (api_output / "capture.log").read_text(encoding="utf-8", errors="replace")
            if capture.returncode or "ERROR:" in text:
                raise RuntimeError("Final editor API capture failed: " + str(api_output / "capture.log"))
            api_sha256 = hashlib.sha256((api_output / "extension_api.json").read_bytes()).hexdigest()
            receipt["api_sha256"] = api_sha256
            receipt["api_path"] = str(api_output / "extension_api.json")
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
                receipt.update(extract_sdk(sdk, engine, api_sha256, args.sdk_header))
            receipt["sdk_metadata"] = json.loads((sdk / "sdk.json").read_text())
            if api_sha256 and receipt["sdk_metadata"]["api_sha256"] != api_sha256:
                raise RuntimeError("SDK metadata does not match the selected final editor API")
            for name in (
                "superpos_session",
                "superpos_world",
                "superpos_schema",
                "superpos_field",
                "superpos_u_int64",
                "superpos_simulation_provider",
            ):
                if not (sdk / "gen/include/godot_cpp/classes" / (name + ".hpp")).is_file():
                    raise RuntimeError("Matching SDK header missing: " + name)
            xmake = os.environ.get("XMAKE") or shutil.which("xmake") or "xmake"
            run(
                "cpp-configure",
                [
                    xmake,
                    "f",
                    "-y",
                    "-P",
                    str(PROBES / "cpp"),
                    "--builddir=" + str(output / "native"),
                    "--mode=debug",
                    *(["--toolchain=msvc"] if os.name == "nt" else []),
                    *(["--vs=" + os.environ["XMAKE_VS"]] if os.name == "nt" and os.environ.get("XMAKE_VS") else []),
                    "--egp_cpp_sdk=" + str(sdk),
                ],
            )
            run("cpp-build", [xmake, "-P", str(PROBES / "cpp"), "-b", "-j", "2", "api-probe"])
        if args.engine and hashlib.sha256(engine.read_bytes()).hexdigest() != receipt["engine_sha256"]:
            raise RuntimeError("Editor changed during typed API qualification")
        if (
            receipt.get("compiled_binary")
            and hashlib.sha256(Path(receipt["compiled_binary"]).read_bytes()).hexdigest()
            != receipt["compiled_binary_sha256"]
        ):
            raise RuntimeError("Compiled editor changed during typed API qualification")
        receipt["passed"] = True
    except (OSError, ValueError, RuntimeError, zlib.error, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(("PASS" if receipt["passed"] else "FAIL") + ": " + str(output / "receipt.json"))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
