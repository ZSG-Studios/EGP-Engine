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

from egp_engine_process import resolve_engine_process

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def clock_recovery_failure(result):
    """Reject incomplete server recovery and explicit live-client resynchronization."""
    diagnostic = "Fixed simulation exceeded its catch-up budget; resynchronization required."
    for language, body in (("csharp", 10000), ("cpp", 20000)):
        proof = result.get("clock_recovery", {}).get(language, {})
        if proof.get("passed") is not True or proof.get("same_session") is not True or proof.get("body_id") != body:
            return f"Missing {language} retained-session recovery"
        states = proof.get("client_states", [])
        if states and states[0] == "Stopped":
            states = states[1:]
        if (
            states != ["Connecting", "Synchronizing", "Connected", "Stopped"] * 4
            or proof.get("client_latency_ms") != 20
            or proof.get("client_jitter_ms") != 5
        ):
            return f"Missing {language} repeated client admission/reset history"
        expected_low_states = []
        for epoch in range(1, 5):
            expected_low_states += ["Connecting", "Synchronizing", "Connected", "Stopped"]
            if epoch < 4:
                expected_low_states += ["Disconnected", "Stopped"]
        if proof.get("low_client_states") != expected_low_states:
            return f"Missing {language} connected low-level disconnect/rejoin history"
        for kind in ("cycles", "low_cycles"):
            records = proof.get(kind, [])
            diagnostics = proof.get("diagnostics" if kind == "cycles" else "low_diagnostics")
            if len(records) != 3 or diagnostics != [diagnostic] * 3:
                return f"Missing {language} {kind} faults/diagnostics"
            previous = None
            previous_peer = None
            for number, record in enumerate(records, 1):
                old, new = record.get("old_entity"), record.get("new_entity")
                if (
                    record.get("cycle") != number
                    or not isinstance(old, int)
                    or not isinstance(new, int)
                    or old <= 0
                    or new <= 0
                    or old == new
                    or (previous is not None and old != previous)
                    or record.get("poll_error") != 1
                    or record.get("gap_ms", 0) < 550
                ):
                    return f"Invalid {language} {kind} handle/fault history"
                previous = new
                if kind == "cycles":
                    tick, advanced = record.get("checkpoint_tick", 0), record.get("final_network_tick", 0)
                    state_hash = record.get("checkpoint_hash", "")
                    if (
                        tick <= 0
                        or advanced < 8
                        or record.get("final_physics_tick") != tick + advanced
                        or not isinstance(state_hash, str)
                        or len(state_hash) != 16
                        or any(c not in "0123456789abcdef" for c in state_hash)
                    ):
                        return f"Invalid {language} restored physics clock/hash"
                    if (
                        record.get("client_id") != (777 if language == "csharp" else 888)
                        or record.get("client_live_polls", 0) <= 0
                        or any(
                            record.get(flag) is not True
                            for flag in (
                                "client_same_session",
                                "fresh_token",
                                "client_reset_cleared",
                                "client_retired_absent",
                                "interest_roundtrip",
                            )
                        )
                        or not tick < record.get("client_physics_tick", 0) <= record.get("final_physics_tick", 0)
                        or record.get("owner_input_count") != number
                        or record.get("invalid_input_count") != 0
                    ):
                        return f"Invalid {language} restored client baseline/ownership/interest"
                else:
                    old_peer, new_peer = record.get("old_peer", 0), record.get("new_peer", 0)
                    if (
                        not isinstance(old_peer, int)
                        or not isinstance(new_peer, int)
                        or old_peer <= 0
                        or new_peer <= old_peer
                        or (previous_peer is not None and old_peer != previous_peer)
                        or record.get("client_id") != (556 if language == "csharp" else 667)
                        or record.get("client_live_polls", 0) <= 0
                        or record.get("final_network_tick", 0) < 8
                        or record.get("opaque_state_hex") != f"{number:02x}00ff2a"
                        or any(
                            record.get(field) != number
                            for field in ("server_apps", "client_apps", "server_packets", "client_packets")
                        )
                        or any(
                            record.get(flag) is not True
                            for flag in (
                                "client_same_session",
                                "fresh_token",
                                "client_disconnect_cleared",
                                "retired_peer_rejected",
                                "interest_roundtrip",
                            )
                        )
                    ):
                        return f"Invalid {language} connected low-level recovery/opaque data history"
                    previous_peer = new_peer
    return None


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
    parser.add_argument(
        "--distcc",
        action="store_true",
        help="Connect native extension builds using private configured xmake distcc hosts",
    )
    args = parser.parse_args()
    engine, engine_process = resolve_engine_process(args.engine)
    sdk, packages = args.sdk.resolve(), args.packages.resolve()
    output = (args.output or ROOT / ".build/egp-net-languages" / str(time.time_ns())).resolve()
    output.mkdir(parents=True, exist_ok=True)
    private_config = None
    if args.distcc:
        private_root = Path(os.environ["LOCALAPPDATA"]) / ".xmake" if os.name == "nt" else Path.home() / ".xmake"
        private_config = private_root / "egp-private-configs" / hashlib.sha256(str(output).encode()).hexdigest()[:20]
        private_config.mkdir(parents=True, exist_ok=True)
        if os.name == "nt":
            account = f"{os.environ['USERDOMAIN']}\\{os.environ['USERNAME']}"
            secured = subprocess.run(
                [
                    "icacls",
                    str(private_config),
                    "/inheritance:r",
                    "/grant:r",
                    f"{account}:(OI)(CI)F",
                    "*S-1-5-18:(OI)(CI)F",
                ],
                capture_output=True,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                timeout=30,
            )
            if secured.returncode:
                raise RuntimeError("Unable to restrict private distributed-build configuration permissions")
        else:
            private_config.chmod(0o700)
    project = output / "project"
    shutil.copytree(
        ROOT / "modules/egp_net/samples/trilingual",
        project,
        ignore=shutil.ignore_patterns(".godot", "bin", "obj", "*.previous"),
        dirs_exist_ok=True,
    )
    shutil.copy2(ROOT / "modules/egp_net/samples/gdscript/processes.gd", project / "ProcessPeer.gd")
    shutil.copy2(ROOT / "misc/egp/network_lab/process_clock_server.gd", project / "ClockServer.gd")
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
        "engine_process": engine_process,
        "engine_sha256": digest(engine),
        "checks": checks,
        "source_sha256": {
            str(p.relative_to(ROOT)): digest(p)
            for folder in ("csharp", "cpp", "gdscript")
            for p in (ROOT / "modules/egp_net" / folder).rglob("*")
            if p.is_file()
        },
    }
    receipt["source_sha256"].update({
        rel: digest(ROOT / rel)
        for rel in (
            "modules/egp_net/samples/trilingual/InteropFixture.cs",
            "modules/egp_net/samples/trilingual/extension/probe.cpp",
            "misc/scripts/validate_egp_net_languages.py",
            "misc/scripts/egp_engine_process.py",
            "misc/scripts/test_egp_net_language_clock.py",
            "misc/egp/network_lab/process_clock_server.gd",
            "misc/scripts/validate_egp_net_clock_process.py",
            "misc/scripts/test_egp_net_clock_process.py",
        )
    })

    def run(label, command, timeout, marker=None, cwd=None):
        start = time.monotonic()
        logfile = output / (label + ".log")
        private_native = private_config is not None and label.startswith("cpp-")
        raw_logfile = private_config / (label + ".log") if private_native else logfile
        with raw_logfile.open("w") as log:
            process = subprocess.Popen(
                [str(x) for x in command],
                cwd=cwd or project,
                env=dict(
                    env,
                    XMAKE_CONFIGDIR=str(
                        (private_config or output) / ("xmake-release-config" if "release" in label else "xmake-config")
                    ),
                    XMAKE_GLOBALDIR=env.get("XMAKE_GLOBALDIR") or str(output / "xmake-global"),
                ),
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
        text = raw_logfile.read_text(errors="replace")
        passed = (
            code == 0
            and (marker is None or marker in text)
            and not any(failure in text for failure in ("SCRIPT ERROR:", "ERROR:", "EGP_TRILINGUAL_FAILED"))
        )
        if private_native:
            # Connection diagnostics may contain private service identity. Keep
            # only the result; credentials remain in the caller's global config.
            receipt.setdefault("distcc", {})[label] = {
                "connected": code == 0 if "distcc-connect" in label else None,
                "distributed_candidate_entries": len(re.findall(r"compiling\.distc", text)),
                "local_fallback_entries": text.count("fallback to the local compiler"),
                "private_diagnostics_retained": True,
                "scope": "Candidate scheduling does not independently prove successful remote execution",
            }
            text = "Private distributed-build diagnostics suppressed.\n"
            logfile.write_text(text)
        checks.append({
            "name": label,
            "passed": passed,
            "exit_code": code,
            "elapsed_seconds": round(time.monotonic() - start, 3),
            "log": str(logfile),
            "pid": process.pid,
        })
        if marker == "EGP_TRILINGUAL_PASSED":
            match = re.search(r"EGP_TRILINGUAL_PASSED (\{[^\n]+\})", text)
            if match:
                receipt[label] = json.loads(match.group(1))
                failure = clock_recovery_failure(receipt[label])
                if failure:
                    checks[-1]["passed"] = passed = False
                    checks[-1]["evidence_failure"] = failure
        elif marker == "EGP_CLOCK_PROCESSES":
            match = re.search(r"EGP_CLOCK_PROCESSES (\{[^\n]+\})", text)
            if match:
                receipt[label] = json.loads(match.group(1))
                if receipt[label].get("passed") is not True:
                    checks[-1]["passed"] = passed = False
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
        print(label, "PASS" if passed else "FAIL", flush=True)
        if not passed:
            print("\n".join(text.splitlines()[-25:]))
            raise RuntimeError(label)

    try:
        xmake = os.environ.get("XMAKE") or shutil.which("xmake") or "xmake"
        run(
            "install",
            [
                xmake,
                "lua",
                ROOT / "misc/scripts/install_egp_net_helpers.lua",
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
            xmake,
            "f",
            "-y",
            "-P",
            project / "extension",
            "-o",
            output / "cpp-build",
            "-m",
            "debug",
            f"--egp_cpp_sdk={sdk}",
        ]
        if args.sdk_library:
            command.append(f"--egp_cpp_library={args.sdk_library.resolve()}")
        run("cpp-configure", command, 120, cwd=project / "extension")
        if args.distcc:
            run(
                "cpp-distcc-connect",
                [xmake, "service", "-P", project / "extension", "--connect", "--distcc"],
                30,
                cwd=project / "extension",
            )
        run(
            "cpp-build",
            [xmake, "-P", project / "extension", "-b", *(["-v"] if args.distcc else []), "-j", "4", "extension"],
            600,
            cwd=project / "extension",
        )
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
        for language in ("csharp", "cpp"):
            run(
                "clock-processes-" + language,
                [
                    sys.executable,
                    ROOT / "misc/scripts/validate_egp_net_clock_process.py",
                    "--engine",
                    engine,
                    "--project",
                    project,
                    "--client-language",
                    language,
                ],
                50,
                "EGP_CLOCK_PROCESSES",
            )
        if args.release_template:
            command = [
                xmake,
                "f",
                "-y",
                "-P",
                project / "extension",
                "-o",
                output / "cpp-build-release",
                "-m",
                "release",
                f"--egp_cpp_sdk={sdk}",
            ]
            if args.release_sdk_library:
                command.append(f"--egp_cpp_library={args.release_sdk_library.resolve()}")
            run("cpp-configure-release", command, 120, cwd=project / "extension")
            if args.distcc:
                run(
                    "cpp-distcc-connect-release",
                    [xmake, "service", "-P", project / "extension", "--connect", "--distcc"],
                    30,
                    cwd=project / "extension",
                )
            run(
                "cpp-build-release",
                [xmake, "-P", project / "extension", "-b", *(["-v"] if args.distcc else []), "-j", "4", "extension"],
                600,
                cwd=project / "extension",
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
            for language in ("csharp", "cpp"):
                run(
                    "export-clock-processes-" + language + suffix,
                    [
                        sys.executable,
                        ROOT / "misc/scripts/validate_egp_net_clock_process.py",
                        "--engine",
                        game,
                        "--client-language",
                        language,
                    ],
                    50,
                    "EGP_CLOCK_PROCESSES",
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
