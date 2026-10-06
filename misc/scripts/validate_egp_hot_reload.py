#!/usr/bin/env python3
"""Exercise live C#/C++ objects through the editor's real debugger and build panel."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
PROBE = """using Godot;
using System.Runtime.Loader;
public partial class ReloadProbe : Node, ISerializationListener {
    [Export] public int Counter { get; set; } = 1;
    [Export] public Vector3 PositionValue { get; set; }
    [Export] public Node? Reference { get; set; }
    [Export] public int ReadyCount { get; set; }
    [Export] public int BeforeCount { get; set; }
    [Export] public int AfterCount { get; set; }
    [Signal] public delegate void PulseEventHandler(int value);
    public override void _Ready() { ReadyCount++; }
    public int Version() => VERSION;
    public bool Collectible() => AssemblyLoadContext.GetLoadContext(GetType().Assembly)!.IsCollectible;
    public void Fire() => EmitSignal(SignalName.Pulse, 1);
    public void OnBeforeSerialize() { BeforeCount++; }
    public void OnAfterDeserialize() { AfterCount++; }
}
"""
RECEIVER = """using Godot;
public partial class ReloadReceiver : Node {
    [Export] public int Hits { get; set; }
    public void Listen(ReloadProbe probe) { probe.Pulse += Receive; }
    private void Receive(int value) { Hits += value; }
}
"""


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-disabled", action="store_true", help="Record the pre-fix opt-in rejection")
    parser.add_argument("--disable-runtime", action="store_true", help="Verify the default non-collectible player")
    args = parser.parse_args()
    output = args.output.resolve() / str(time.time_ns())
    project = output / "project"
    addon = project / "addons/reload_fixture"
    addon.mkdir(parents=True)
    engine = args.engine.resolve()
    env = os.environ.copy()
    env["NUGET_PACKAGES"] = str(output / "nuget-packages")
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    receipt = {"passed": False, "engine": str(engine), "engine_sha256": digest(engine), "steps": [], "samples": []}
    receipt["scope"] = (
        "Headless editor and separate running game; live objects, properties, callables, signals, compile recovery. No exported-template or arbitrary-ABI-change claim."
    )
    process = None
    stream = None
    request_id = 0

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    def require(value, message):
        if not value:
            raise RuntimeError(message)

    def run(name, command, expected_success=True, timeout=300):
        command = [str(x) for x in command]
        start = time.monotonic()
        with (output / (name + ".log")).open("w", encoding="utf-8") as log:
            child = subprocess.Popen(command, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, **options)
            try:
                code = child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                terminate(child)
                raise RuntimeError(name + " watchdog expired")
        passed = (code == 0) == expected_success
        receipt["steps"].append({
            "name": name,
            "command": command,
            "exit_code": code,
            "passed": passed,
            "elapsed_seconds": round(time.monotonic() - start, 3),
        })
        save()
        require(passed, name + " failed")
        print(name, "PASS", flush=True)

    def terminate(child):
        if child.poll() is None:
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
            else:
                child.kill()
            child.wait(timeout=15)

    def wait_json(path, predicate, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            require(process.poll() is None, "Editor exited unexpectedly; see editor.log")
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                if predicate(data):
                    return data
            except (OSError, ValueError):
                pass
            time.sleep(0.05)
        raise RuntimeError("Watchdog expired waiting for " + path.name)

    def command(action, timeout=30):
        nonlocal request_id
        request_id += 1
        # Replace atomically so the editor never parses a partial JSON document.
        temporary = project / "command.tmp"
        temporary.write_text(json.dumps({"id": request_id, "action": action}), encoding="utf-8")
        temporary.replace(project / "command.json")
        response = wait_json(project / "response.json", lambda x: x["id"] == request_id, timeout)
        require(response["passed"], action + " command failed")
        return response

    def sample():
        command("sample")
        state = wait_json(project / "sample.json", lambda x: x["request"] == request_id, 15)
        receipt["samples"].append(state)
        save()
        return state

    def verify(state, version, previous):
        require(not state["editor_hint"], "Fixture is an editor tool rather than a running game")
        require(state["collectible"], "Running-game project assembly is not collectible")
        for key in ("cpp_counter", "cs_counter", "ready_count"):
            require(state[key] == {"cpp_counter": 91, "cs_counter": 87, "ready_count": 1}[key], key + " changed")
        require(all(state[key] for key in ("vector_ok", "reference_ok", "parents_ok")), "Live state or parent lost")
        require(
            state["cpp_version"] == str(version) and state["cpp_callable"] == str(version),
            "C++ method/cached callable retained old code",
        )
        require(
            state["cs_version"] == version and state["cs_callable"] == version,
            "C# method/cached callable retained old code",
        )
        require(
            state["cpp_hits"] == state["cs_hits"] == state["receiver_hits"],
            "Signal/delegate subscription lost or duplicated",
        )
        if previous:
            require(
                all(state[key] == previous[key] for key in ("cpp_id", "cs_id", "receiver_id")),
                "Live native identity changed",
            )
            require(state["cpp_hits"] == previous["cpp_hits"] + 1, "Signal callback count changed")
            require(
                state["before_count"] >= previous["before_count"] and state["after_count"] >= previous["after_count"],
                "Serialization callback state lost",
            )

    try:
        (project / "project.godot").write_text(
            'config_version=5\n[application]\nconfig/name="ReloadFixture"\nrun/main_scene="res://main.tscn"\n[dotnet]\nproject/assembly_name="ReloadFixture"\n[debug]\nhot_reload/enable_runtime='
            + ("false" if args.disable_runtime else "true")
            + '\n[editor]\nrun/main_run_args="--headless --max-fps 60"\n[editor_plugins]\nenabled=PackedStringArray("res://addons/reload_fixture/plugin.cfg")\n',
            encoding="utf-8",
        )
        (project / "main.tscn").write_text(
            '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://main.gd" id="1"]\n[node name="Fixture" type="Node"]\nscript=ExtResource("1")\n',
            encoding="utf-8",
        )
        shutil.copyfile(ROOT / "misc/scripts/egp_hot_reload_game.gd", project / "main.gd")
        shutil.copyfile(ROOT / "misc/scripts/egp_hot_reload_editor.gd", addon / "plugin.gd")
        (addon / "plugin.cfg").write_text(
            '[plugin]\nname="ReloadFixture"\ndescription="Isolated reload fixture"\nauthor="EGP"\nversion="1"\nscript="plugin.gd"\n',
            encoding="utf-8",
        )
        (project / "ReloadFixture.csproj").write_text(
            '<Project Sdk="Godot.NET.Sdk/4.8.0-dev"><PropertyGroup><TargetFramework>net10.0</TargetFramework><EnableDynamicLoading>true</EnableDynamicLoading><Nullable>enable</Nullable></PropertyGroup></Project>\n',
            encoding="utf-8",
        )
        (project / "NuGet.Config").write_text(
            f'<configuration><packageSources><clear/><add key="egp" value="{escape(str(args.packages.resolve()))}"/><add key="nuget" value="https://api.nuget.org/v3/index.json"/></packageSources></configuration>\n',
            encoding="utf-8",
        )
        (project / "ReloadProbe.cs").write_text(PROBE.replace("VERSION", "1"), encoding="utf-8")
        (project / "ReloadReceiver.cs").write_text(RECEIVER, encoding="utf-8")
        run("managed-initial", ["dotnet", "build", "--nologo", "-v", "minimal"])
        editor_command = [str(engine), "--headless", "--editor", "--path", str(project), "--max-fps", "30"]
        receipt["editor_command"] = editor_command
        stream = (output / "editor.log").open("w", encoding="utf-8")
        process = subprocess.Popen(editor_command, env=env, stdout=stream, stderr=subprocess.STDOUT, **options)
        command("prepare", 120)
        source_path = project / "extensions/reload/src/extension.cpp"
        source = source_path.read_text(encoding="utf-8")
        source = source.replace(
            'ClassDB::bind_method(D_METHOD("get_message"), &EGP_reload_Node::get_message);',
            'ClassDB::bind_method(D_METHOD("get_message"), &EGP_reload_Node::get_message);\n\t\tClassDB::bind_method(D_METHOD("set_counter", "value"), &EGP_reload_Node::set_counter);\n\t\tClassDB::bind_method(D_METHOD("get_counter"), &EGP_reload_Node::get_counter);\n\t\tADD_PROPERTY(PropertyInfo(Variant::INT, "counter"), "set_counter", "get_counter");\n\t\tADD_SIGNAL(MethodInfo("pulse", PropertyInfo(Variant::INT, "value")));',
        )
        source = source.replace(
            "public:\n",
            "public:\n\tint counter = 1;\n\tvoid set_counter(int value) { counter = value; }\n\tint get_counter() const { return counter; }\n",
        )
        require("Hello from reload!" in source, "Unexpected scaffold template")
        source = source.replace("Hello from reload!", "VERSION")
        source_path.write_text(source.replace("VERSION", "1"), encoding="utf-8")
        require(command("build", 900)["build_result"] == 0, "Initial C++ panel build failed")
        command("play", 120)
        # Wait for the remote debugger to attach before sending a sample.
        time.sleep(3)
        initial = sample()
        if args.expect_disabled or args.disable_runtime:
            require(not initial["editor_hint"] and not initial["collectible"], "Expected non-collectible baseline")
            receipt["disabled_baseline"] = True
        else:
            verify(initial, 1, None)
            previous = initial
            descriptor = project / "extensions/reload/reload.gdextension"
            for version in (2, 3):
                before = digest(descriptor)
                if version == 2:
                    source_path.write_text(
                        source.replace("VERSION", "2") + "\n#error EGP deliberate runtime diagnostic\n",
                        encoding="utf-8",
                    )
                    require(command("build", 900)["build_result"] != 0, "Invalid C++ unexpectedly compiled")
                    require(digest(descriptor) == before, "Failed C++ build published a descriptor")
                    failed = sample()
                    verify(failed, 1, previous)
                    previous = failed
                    (project / "ReloadProbe.cs").write_text(PROBE.replace("VERSION", "invalid!"), encoding="utf-8")
                    run("managed-invalid", ["dotnet", "build", "--nologo", "-v", "minimal"], expected_success=False)
                    failed = sample()
                    verify(failed, 1, previous)
                    previous = failed
                (project / "ReloadProbe.cs").write_text(PROBE.replace("VERSION", str(version)), encoding="utf-8")
                run("managed-version-" + str(version), ["dotnet", "build", "--nologo", "-v", "minimal"])
                source_path.write_text(source.replace("VERSION", str(version)), encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "C++ recovery build failed")
                # C++ publication broadcasts reload to each running debugger session.
                time.sleep(2)
                state = sample()
                verify(state, version, previous)
                require(
                    state["before_count"] > previous["before_count"] and state["after_count"] > previous["after_count"],
                    "C# lifecycle hooks did not run",
                )
                previous = state
            receipt["reloads"] = 2
        command("close")
        require(process.wait(timeout=60) == 0, "Editor/game teardown failed")
        require(digest(engine) == receipt["engine_sha256"], "Input engine changed during validation")
        receipt["passed"] = True
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    finally:
        if process is not None:
            terminate(process)
        if stream is not None:
            stream.close()
        receipt["fixture_sha256"] = {
            str(p.relative_to(ROOT)): digest(p)
            for p in (
                ROOT / "misc/scripts/egp_hot_reload_game.gd",
                ROOT / "misc/scripts/egp_hot_reload_editor.gd",
                Path(__file__).resolve(),
            )
        }
        save()
    print("PASS" if receipt["passed"] else "FAIL", output / "receipt.json", receipt.get("error", ""), flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
