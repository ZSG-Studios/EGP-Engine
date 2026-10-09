#!/usr/bin/env python3
"""Compile and run the mixed C#/GDScript/C++ networking sample in a fresh project."""

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

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, required=True)
    parser.add_argument("--sdk-library", type=Path)
    parser.add_argument(
        "--release-sdk-library", type=Path, help="Matching Release SDK archive; never reuse a Debug archive"
    )
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--template", type=Path, help="Also export and verify a relocated Windows Mono debug game")
    parser.add_argument(
        "--release-template",
        type=Path,
        help="Also build the Release extension and verify a relocated Mono release game",
    )
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    engine, sdk, packages = args.engine.resolve(), args.sdk.resolve(), args.packages.resolve()
    output = (args.output or ROOT / ".build/egp-net-languages" / str(time.time_ns())).resolve()
    output.mkdir(parents=True, exist_ok=True)
    project = output / "project"
    shutil.copytree(
        ROOT / "modules/egp_net/samples/trilingual",
        project,
        ignore=shutil.ignore_patterns(".godot", "bin", "obj", "*.previous"),
        dirs_exist_ok=True,
    )
    shutil.copy2(ROOT / "modules/egp_net/samples/gdscript/processes.gd", project / "ProcessPeer.gd")
    env = os.environ.copy()
    # Godot packages all have the same development version: never reuse a stale cache.
    env["NUGET_PACKAGES"] = str(output / "nuget-packages")
    config = project / "NuGet.Config"
    from xml.sax.saxutils import escape

    config.write_text(
        f'<configuration><packageSources><clear/><add key="egp" value="{escape(str(packages))}"/><add key="nuget" value="https://api.nuget.org/v3/index.json"/></packageSources></configuration>\n'
    )
    checks = []
    receipt = {
        "engine": str(engine),
        "engine_sha256": digest(engine),
        "checks": checks,
        "source_sha256": {
            str(p.relative_to(ROOT)): digest(p)
            for folder in ("csharp", "cpp", "gdscript")
            for p in (ROOT / "modules/egp_net" / folder).rglob("*")
            if p.is_file()
        },
    }

    def run(label, command, timeout, marker=None, cwd=None):
        start = time.monotonic()
        logfile = output / (label + ".log")
        with logfile.open("w") as log:
            process = subprocess.Popen(
                [str(x) for x in command],
                cwd=cwd or project,
                env=env,
                stdout=log,
                stderr=subprocess.STDOUT,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            try:
                code = process.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(process.pid), "/T", "/F"], capture_output=True)
                else:
                    process.kill()
                process.wait()
                code = -1
        text = logfile.read_text(errors="replace")
        passed = (
            code == 0
            and (marker is None or marker in text)
            and not any(failure in text for failure in ("SCRIPT ERROR:", "ERROR:", "EGP_TRILINGUAL_FAILED"))
        )
        checks.append({
            "name": label,
            "passed": passed,
            "exit_code": code,
            "elapsed_seconds": round(time.monotonic() - start, 3),
            "log": str(logfile),
        })
        if marker == "EGP_TRILINGUAL_PASSED":
            match = re.search(r"EGP_TRILINGUAL_PASSED (\{[^\n]+\})", text)
            if match:
                receipt[label] = json.loads(match.group(1))
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(label, "PASS" if passed else "FAIL", flush=True)
        if not passed:
            print("\n".join(text.splitlines()[-25:]))
            raise RuntimeError(label)

    try:
        run(
            "install",
            [
                sys.executable,
                ROOT / "misc/scripts/install_egp_net_helpers.py",
                "--project",
                project,
                "--languages",
                "gdscript",
                "csharp",
                "cpp",
            ],
            30,
        )
        command = [
            "cmake",
            "-S",
            project / "extension",
            "-B",
            output / "cpp-build",
            f"-DEGP_CPP_SDK={sdk}",
            "-DCMAKE_BUILD_TYPE=Debug",
        ]
        if args.sdk_library:
            command.append(f"-DEGP_CPP_LIBRARY={args.sdk_library.resolve()}")
        run("cpp-configure", command, 120)
        run("cpp-build", ["cmake", "--build", output / "cpp-build", "--config", "Debug", "--parallel", "4"], 600)
        run("csharp-build", ["dotnet", "build", project / "NetInterop.csproj", "--nologo", "-v", "minimal"], 300)
        run("cold-import", [engine, "--headless", "--editor", "--path", project, "--import", "--max-fps", "30"], 120)
        run("interop", [engine, "--headless", "--path", project, "--max-fps", "60"], 40, "EGP_TRILINGUAL_PASSED")
        for language in ("csharp", "gdscript", "cpp"):
            run(
                "processes-" + language,
                [
                    sys.executable,
                    ROOT / "misc/scripts/validate_egp_net_process.py",
                    "--engine",
                    engine,
                    "--project",
                    project,
                    "--client-language",
                    language,
                ],
                40,
                "EGP_NETWORK_PROCESSES",
            )
        if args.release_template:
            command = [
                "cmake",
                "-S",
                project / "extension",
                "-B",
                output / "cpp-build-release",
                f"-DEGP_CPP_SDK={sdk}",
                "-DCMAKE_BUILD_TYPE=Release",
            ]
            if args.release_sdk_library:
                command.append(f"-DEGP_CPP_LIBRARY={args.release_sdk_library.resolve()}")
            run("cpp-configure-release", command, 120)
            run(
                "cpp-build-release",
                ["cmake", "--build", output / "cpp-build-release", "--config", "Release", "--parallel", "4"],
                600,
            )
            receipt["release_extension_sha256"] = digest(project / "extension/bin/netinterop.release.dll")
        for configuration, candidate in [("debug", args.template), ("release", args.release_template)]:
            if candidate is None:
                continue
            template = candidate.resolve()
            if sys.platform != "win32" or not template.is_file():
                raise RuntimeError(f"Supply a Windows Mono {configuration} template")
            suffix = "" if configuration == "debug" else "-release"
            staging = output / ("staging" + suffix)
            staging.mkdir(exist_ok=True)
            game = staging / "EGP.NetInterop.exe"
            (project / "export_presets.cfg").write_text(f'''[preset.0]
name="Windows Mono Network"
platform="Windows Desktop"
runnable=true
export_filter="all_resources"
include_filter=""
exclude_filter="*.previous"

[preset.0.options]
custom_template/debug="{args.template.resolve().as_posix() if args.template else ""}"
custom_template/release="{args.release_template.resolve().as_posix() if args.release_template else ""}"
binary_format/architecture="x86_64"
binary_format/embed_pck=false
application/modify_resources=false
debug/export_console_wrapper=0
''')
            run(
                "export" + suffix,
                [
                    engine,
                    "--headless",
                    "--path",
                    project,
                    "--max-fps",
                    "30",
                    "--export-" + configuration,
                    "Windows Mono Network",
                    game,
                ],
                300,
            )
            relocated = output / ("relocated" + suffix)
            shutil.copytree(staging, relocated, dirs_exist_ok=True)
            game = relocated / game.name
            if not game.with_suffix(".pck").is_file():
                raise RuntimeError("No exported PCK")
            run(
                "export-interop" + suffix,
                [game, "--headless", "--max-fps", "60"],
                40,
                "EGP_TRILINGUAL_PASSED",
                cwd=relocated,
            )
            for language in ("csharp", "gdscript", "cpp"):
                run(
                    "export-processes-" + language + suffix,
                    [
                        sys.executable,
                        ROOT / "misc/scripts/validate_egp_net_process.py",
                        "--engine",
                        game,
                        "--client-language",
                        language,
                    ],
                    40,
                    "EGP_NETWORK_PROCESSES",
                    cwd=relocated,
                )
            receipt[configuration + "_export"] = {
                "template_sha256": digest(template),
                "export_path": str(game),
                "bundle_sha256": {
                    str(p.relative_to(relocated)): digest(p) for p in sorted(relocated.rglob("*")) if p.is_file()
                },
            }
            if configuration == "debug":
                receipt["template_sha256"] = digest(template)
                receipt["export_path"] = str(game)
                receipt["bundle_sha256"] = receipt["debug_export"]["bundle_sha256"]
        receipt["passed"] = True
        receipt["extension_sha256"] = digest(project / "extension/bin/netinterop.dll")
        receipt["assembly_sha256"] = digest(project / ".godot/mono/temp/bin/Debug/NetInterop.dll")
    except (RuntimeError, OSError) as error:
        receipt["passed"] = False
        receipt["error"] = str(error)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(output / "receipt.json")
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
