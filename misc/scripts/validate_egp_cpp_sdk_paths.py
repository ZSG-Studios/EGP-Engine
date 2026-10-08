"""Qualify Windows x64 external SDK paths with full native cold/warm/dependency builds."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=ROOT / ".build/xmake-sdk-object-paths")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error(
            "This regression qualifies Windows x64 compiler paths; use the platform-independent SDK/UI validators elsewhere"
        )
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    engine = args.engine.resolve()
    xmake = os.environ.get("XMAKE") or shutil.which("xmake")
    if not xmake:
        parser.error("Set XMAKE or install the pinned native xmake tool")
    output = args.output.resolve() / str(time.time_ns())
    output.mkdir(parents=True)
    project = output / "project/fixtures/games/networked/players/extensions/deep/source/project"
    project.mkdir(parents=True)
    sdk = output / "external-engine-sdk-generated-bindings-package"
    native = output / "native"
    environment = dict(os.environ, XMAKE_CONFIGDIR=str(output / "config"), XMAKE_GLOBALDIR=str(output / "global"))
    receipt = {
        "passed": False,
        "engine": str(engine),
        "engine_sha256": hashlib.sha256(engine.read_bytes()).hexdigest(),
        "commands": [],
    }

    def run(label, command, cwd=project):
        logfile = output / (label + ".log")
        with logfile.open("w", encoding="utf-8") as log:
            process = subprocess.run(
                command,
                cwd=cwd,
                env=environment,
                stdout=log,
                stderr=subprocess.STDOUT,
                timeout=900,
                **({"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}),
            )
        receipt["commands"].append({"stage": label, "command": command, "exit_code": process.returncode})
        text = logfile.read_text(encoding="utf-8", errors="replace")
        if process.returncode or "ERROR:" in text or "leaked" in text:
            raise RuntimeError(label + " failed: " + str(logfile))
        return text

    try:
        api_folder = output / "api"
        api_folder.mkdir()
        run("api", [str(engine), "--headless", "--dump-extension-api"], api_folder)
        api = api_folder / "extension_api.json"
        receipt["api_sha256"] = hashlib.sha256(api.read_bytes()).hexdigest()
        run(
            "sdk",
            [xmake, "lua", str(ROOT / "misc/scripts/extract_egp_cpp_sdk.lua"), "--output", str(sdk), "--api", str(api)],
            ROOT,
        )
        metadata = json.loads((sdk / "sdk.json").read_text())
        assert metadata["api_sha256"] == receipt["api_sha256"]
        assert (sdk / "tools/generated_objects.lua").read_bytes() == (
            ROOT / "build/xmake/generated_objects.lua"
        ).read_bytes()
        assert str(metadata["bits"]) == "64", "This regression requires the matching Windows x64 editor SDK"
        receipt["sdk_metadata"] = metadata
        platform = "windows"
        replacements = {
            "@NAME@": "path_fixture",
            "@CLASS@": "EGP_path_fixture_Node",
            "@PLATFORM@": platform,
            "@ARCH@": "x86_64",
            "@SUFFIX@": ".dll",
        }
        for source, destination in [("xmake.lua.in", "xmake.lua"), ("extension.cpp.in", "src/extension.cpp")]:
            text = (sdk / "templates" / source).read_text()
            for key, value in replacements.items():
                text = text.replace(key, value)
            target = project / destination
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text)
        with (project / "xmake.lua").open("a") as file:
            file.write(
                '\ntarget("godot-cpp")\n    after_build(function(target)\n        import("core.base.json").savefile("objects.json", {objects=target:objectfiles(), mapping=target:data("egp.generated_objects.mapping")})\n    end)\ntarget_end()\n'
            )
        run(
            "configure",
            [
                xmake,
                "f",
                "-y",
                "-P",
                str(project),
                "-o",
                str(native),
                "-p",
                platform,
                "-a",
                "x64",
                "-m",
                "debug",
                "--egp_cpp_sdk=" + str(sdk),
                "--egp_cpp_cache=",
            ],
        )
        build = [xmake, "-P", str(project), "-b", "-j", str(args.jobs), "extension"]
        run("cold", build)
        graph = json.loads((project / "objects.json").read_text())
        objects = [Path(name) if Path(name).is_absolute() else (project / name).resolve() for name in graph["objects"]]
        assert len(objects) > 1000, "The full SDK must compile, not a reduced stand-in"
        mapping = graph["mapping"]
        ordinary = next((pair for pair in mapping.items() if "editor_plugin_registration.cpp" in pair[0]), None)
        generated = next(
            (pair for pair in mapping.items() if "visual_shader_node_particle_sphere_emitter.cpp" in pair[0]), None
        )
        assert ordinary and generated, "Both ordinary external and generated long SDK paths must compact"

        def absolute(name):
            return Path(name).resolve() if Path(name).is_absolute() else (project / name).resolve()

        receipt["ordinary_path_lengths"] = {
            "original": len(str(absolute(ordinary[0]))),
            "compact": len(str(absolute(ordinary[1]))),
        }
        receipt["generated_path_lengths"] = {
            "original": len(str(absolute(generated[0]))),
            "compact": len(str(absolute(generated[1]))),
        }
        snapshots = {name: (name.stat().st_mtime_ns, hashlib.sha256(name.read_bytes()).hexdigest()) for name in objects}
        run("warm", build)
        assert all(
            (name.stat().st_mtime_ns, hashlib.sha256(name.read_bytes()).hexdigest()) == state
            for name, state in snapshots.items()
        ), "Warm SDK build changed objects"
        source = sdk / "src/classes/editor_plugin_registration.cpp"
        original = source.read_bytes()
        changed_object = absolute(ordinary[1])
        old_mtime = changed_object.stat().st_mtime_ns
        time.sleep(1.1)
        source.write_bytes(original + b"\n// Isolated canonical source dependency regression.\n")
        try:
            run("source-dependency", build)
            assert changed_object.stat().st_mtime_ns > old_mtime, (
                "Original SDK source mutation did not rebuild compact object"
            )
            assert all(
                name == changed_object or name.stat().st_mtime_ns == state[0] for name, state in snapshots.items()
            ), "Unrelated SDK objects rebuilt after one source changed"
        finally:
            source.write_bytes(original)
        library = next((project / "bin").glob("*.dll"))
        descriptor = (sdk / "templates/extension.gdextension.in").read_text()
        for key, value in replacements.items():
            descriptor = descriptor.replace(key, value)
        descriptor = descriptor.replace("res://extensions/path_fixture/", "res://")
        (project / "path_fixture.gdextension").write_text(descriptor)
        (project / "project.godot").write_text('config_version=5\n[application]\nconfig/name="SDKPathQualification"\n')
        (project / "verify.gd").write_text(
            'extends SceneTree\nfunc _initialize():\n\tvar node = ClassDB.instantiate("EGP_path_fixture_Node")\n\tassert(node.get_message() == "Hello from path_fixture!")\n\tnode.free()\n\tprint("EGP_CPP_EXTERNAL_SDK_RUNTIME_PASS")\n\tquit()\n'
        )
        run("import", [str(engine), "--headless", "--editor", "--path", str(project), "--import"])
        runtime = run("runtime", [str(engine), "--headless", "--path", str(project), "--script", "res://verify.gd"])
        assert "EGP_CPP_EXTERNAL_SDK_RUNTIME_PASS" in runtime
        receipt.update(
            passed=True,
            full_sdk_objects=len(objects),
            warm_objects_unchanged=True,
            canonical_source_dependency=True,
            library=str(library),
            library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),
        )
        assert hashlib.sha256(engine.read_bytes()).hexdigest() == receipt["engine_sha256"], (
            "Engine changed during qualification"
        )
    except (
        OSError,
        ValueError,
        KeyError,
        StopIteration,
        AssertionError,
        RuntimeError,
        subprocess.TimeoutExpired,
    ) as error:
        receipt["error"] = str(error)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(("PASS" if receipt["passed"] else "FAIL") + ": " + str(output / "receipt.json"))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
