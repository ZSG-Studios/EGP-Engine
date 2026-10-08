"""Qualify native scene spawning and allowlisted RPC with authenticated OS processes."""

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MARKER = "EGP_SPAWNER_RPC_RESULT="


def run_scenario(engine, project, output, creationflags, pack=None):
    output.mkdir()
    coordination = output / "coordination"
    coordination.mkdir()
    processes = []
    logs = []
    try:
        for role in ("server", "1", "2"):
            command = [str(engine), "--headless", "--max-fps", "120"]
            if pack is not None and role != "server":
                runtime = output / ("relocated-client-" + role)
                runtime.mkdir()
                command += ["--path", str(runtime), "--main-pack", str(pack)]
            else:
                command += ["--path", str(project)]
            command += [
                "--script",
                "res://spawner_rpc_fixture.gd",
                "--",
                "role=" + role,
                "directory=" + str(coordination),
            ]
            if pack is not None and role != "server":
                command.append("compiled=1")
            log = (output / (role + ".log")).open("w", encoding="utf-8")
            logs.append(log)
            processes.append(
                subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, creationflags=creationflags)
            )
        deadline = time.monotonic() + 65
        while any(process.poll() is None for process in processes) and time.monotonic() < deadline:
            if any(process.poll() not in (None, 0) for process in processes):
                raise RuntimeError("A fixture process failed; see " + str(output))
            time.sleep(0.1)
        if any(process.poll() is None for process in processes):
            raise RuntimeError("Authenticated three-process watchdog expired; see " + str(output))
        for log in logs:
            log.close()
        results = {}
        for role, process in zip(("server", "1", "2"), processes):
            text = (output / (role + ".log")).read_text(encoding="utf-8", errors="replace")
            markers = [line.split(MARKER, 1)[1] for line in text.splitlines() if MARKER in line]
            if process.returncode or len(markers) != 1 or re.search(r"(?:SCRIPT ERROR|ERROR|WARNING):", text):
                raise RuntimeError("Unclean fixture process: " + str(output / (role + ".log")))
            result = json.loads(markers[0])
            if not result["passed"] or result["checks"] < (14 if role == "server" else 8):
                raise RuntimeError("Incomplete fixture assertions: " + str(output / (role + ".log")))
            if pack is not None and role != "server" and not result["compiled_assets"]:
                raise RuntimeError("Exported client did not prove compiled assets")
            results[role] = result
        return {"passed": True, "results": results, "exit_codes": [process.returncode for process in processes]}
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        for log in logs:
            if not log.closed:
                log.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    engine = args.engine.resolve()
    output = args.output.resolve() / str(time.time_ns())
    output.mkdir(parents=True)
    project = output / "project"
    project.mkdir()
    coordination = output / "coordination"
    coordination.mkdir()
    sources = ("spawner_rpc_fixture.gd", "spawner_rpc_actor.gd", "spawner_rpc_actor.tscn", "spawner_world_fixture.gd")
    for name in sources:
        shutil.copy2(ROOT / "modules/egp_net/tests" / name, project / name)
    helper = project / "addons/egp_net/egp_net.gd"
    helper.parent.mkdir(parents=True)
    shutil.copy2(ROOT / "modules/egp_net/gdscript/egp_net.gd", helper)
    (project / "project.godot").write_text(
        'config_version=5\n[application]\nconfig/name="Superposition Spawner RPC Qualification"\n'
        '[rendering]\nrenderer/rendering_method="gl_compatibility"\n',
        encoding="utf-8",
    )
    creationflags = subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0
    processes = []
    logs = []
    receipt = {"passed": False, "engine": str(engine), "engine_sha256": hashlib.sha256(engine.read_bytes()).hexdigest()}
    receipt["legacy_helper_sha256"] = hashlib.sha256(helper.read_bytes()).hexdigest()
    receipt["source_sha256"] = {
        name: hashlib.sha256((ROOT / "modules/egp_net/tests" / name).read_bytes()).hexdigest() for name in sources
    }
    try:
        with (output / "import.log").open("w", encoding="utf-8") as log:
            imported = subprocess.run(
                [str(engine), "--headless", "--path", str(project), "--editor", "--import", "--quit"],
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=90,
                creationflags=creationflags,
            )
        if imported.returncode:
            raise RuntimeError("Fixture import failed; see import.log")
        receipt["source_scenario"] = run_scenario(engine, project, output / "source-scenario", creationflags)
        pack = output / "compiled-scenes.pck"
        (project / "export_presets.cfg").write_text(
            '[preset.0]\nname="CompiledPack"\nplatform="Windows Desktop"\nrunnable=true\n'
            'export_filter="all_resources"\ninclude_filter=""\nexclude_filter=""\nscript_export_mode=2\n[preset.0.options]\n'
            f'custom_template/debug="{engine.as_posix()}"\ncustom_template/release="{engine.as_posix()}"\n',
            encoding="utf-8",
        )
        with (output / "export-pack.log").open("w", encoding="utf-8") as log:
            exported = subprocess.run(
                [str(engine), "--headless", "--path", str(project), "--export-pack", "CompiledPack", str(pack)],
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=90,
                creationflags=creationflags,
            )
        export_log = (output / "export-pack.log").read_text(encoding="utf-8", errors="replace")
        if exported.returncode or not pack.is_file() or re.search(r"(?:SCRIPT ERROR|ERROR|WARNING):", export_log):
            raise RuntimeError("Compiled scene/script PCK export failed; see export-pack.log")
        receipt["pack_sha256"] = hashlib.sha256(pack.read_bytes()).hexdigest()
        receipt["compiled_export_scenario"] = run_scenario(
            engine, project, output / "compiled-export-scenario", creationflags, pack
        )
        with (output / "world.log").open("w", encoding="utf-8") as log:
            world_process = subprocess.run(
                [
                    str(engine),
                    "--headless",
                    "--max-fps",
                    "120",
                    "--path",
                    str(project),
                    "--script",
                    "res://spawner_world_fixture.gd",
                ],
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=45,
                creationflags=creationflags,
            )
        world_text = (output / "world.log").read_text(encoding="utf-8", errors="replace")
        world_markers = [
            line.split("EGP_SPAWNER_WORLD_RESULT=", 1)[1]
            for line in world_text.splitlines()
            if "EGP_SPAWNER_WORLD_RESULT=" in line
        ]
        if (
            world_process.returncode
            or len(world_markers) != 1
            or re.search(r"(?:SCRIPT ERROR|ERROR|WARNING):", world_text)
        ):
            raise RuntimeError("Unclean World automatic-provider fixture")
        world_result = json.loads(world_markers[0])
        if not world_result["passed"] or world_result["checks"] < 20:
            raise RuntimeError("Incomplete World automatic-provider assertions")
        receipt["world"] = world_result
        receipt["world_exit_code"] = world_process.returncode
        receipt.update(
            passed=True,
            results=receipt["source_scenario"]["results"],
            scope="Authenticated source-project server and independent source/compiled-PCK clients; late join, mismatched scene IDs, nested properties, RPC permission/schema/type rejection, legacy helper coexistence, interest, despawn, reconnect. World auto-provider restart and queued-call isolation. Export uses real compressed .gdc and binary scenes; matching native editor runs relocated PCK. No exported-template, rendering or performance claim.",
        )
    except Exception as error:
        receipt["error"] = str(error)
    finally:
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
        for log in logs:
            if not log.closed:
                log.close()
        receipt["exit_codes"] = receipt.get("source_scenario", {}).get("exit_codes", []) + receipt.get(
            "compiled_export_scenario", {}
        ).get("exit_codes", [])
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(str(output / "receipt.json"))
    if not receipt["passed"]:
        raise SystemExit(receipt.get("error", "Qualification failed"))
    print("Native authenticated spawner/RPC qualification: PASS")


if __name__ == "__main__":
    main()
