#!/usr/bin/env python3
"""Exercise live C#/C++ objects through the editor's real debugger and build panel."""

import argparse
import hashlib
import json
import os
import re
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
    public void HoldRoot(string path) {
        var thread = new System.Threading.Thread(() => {
            System.IO.File.WriteAllText(path + ".started", "started");
            while (!System.IO.File.Exists(path)) System.Threading.Thread.Sleep(10);
            System.IO.File.WriteAllText(path + ".finished", "finished");
        });
        thread.IsBackground = true;
        thread.Start();
    }
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
    parser.add_argument(
        "--feature-override", action="store_true", help="Enable runtime reload through the editor feature override"
    )
    parser.add_argument(
        "--assembly-recovery", action="store_true", help="Also reject and recover a corrupted managed assembly"
    )
    parser.add_argument(
        "--unload-recovery",
        action="store_true",
        help="Also recover after a live application thread prevents assembly unload",
    )
    parser.add_argument(
        "--native-recovery", action="store_true", help="Also recover a missing and invalid native library"
    )
    parser.add_argument(
        "--native-abi-recovery",
        action="store_true",
        help="Exercise changed method signatures and rejected base-class repair",
    )
    args = parser.parse_args()
    if args.disable_runtime and args.feature_override:
        parser.error("--disable-runtime and --feature-override are mutually exclusive")
    if args.native_abi_recovery and (args.disable_runtime or args.expect_disabled):
        parser.error("--native-abi-recovery requires runtime reload")
    probe_source = PROBE
    output = args.output.resolve() / str(time.time_ns())
    project = output / "project"
    addon = project / "addons/reload_fixture"
    addon.mkdir(parents=True)
    engine = args.engine.resolve()
    env = os.environ.copy()
    env["NUGET_PACKAGES"] = str(output / "nuget-packages")
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    receipt = {"passed": False, "engine": str(engine), "engine_sha256": digest(engine), "steps": [], "samples": []}
    fixture_paths = [
        ROOT / "misc/scripts" / name
        for name in (
            "egp_hot_reload_game.gd",
            "egp_hot_reload_editor.gd",
            "validate_egp_hot_reload.py",
        )
    ]
    receipt["source_commit"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    receipt["fixture_sha256"] = {str(p.relative_to(ROOT)): digest(p) for p in fixture_paths}
    runtime_files = [engine.parent / "GodotSharp/Api/Debug" / name for name in ("GodotSharp.dll", "GodotPlugins.dll")]
    receipt["managed_runtime_sha256"] = {str(path): digest(path) for path in runtime_files}
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
            "pid": child.pid,
            "log": str(output / (name + ".log")),
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
        deadline = time.monotonic() + 2
        while True:
            try:
                temporary.replace(project / "command.json")
                break
            except PermissionError:
                # Windows readers may briefly hold a file without delete sharing.
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
        response = wait_json(project / "response.json", lambda x: x["id"] == request_id, timeout)
        require(response["passed"], action + " command failed")
        return response

    def sample(action="sample"):
        command(action)
        state = wait_json(project / "sample.json", lambda x: x["request"] == request_id, 15)
        receipt["samples"].append(state)
        save()
        return state

    def verify(state, version, previous, cs_version=None):
        cs_version = version if cs_version is None else cs_version
        require(not state.get("native_unavailable"), "Compatible native repair did not restore the live class")
        require(not state["editor_hint"], "Fixture is an editor tool rather than a running game")
        require(int(state["pid"]) != process.pid, "Game was not launched as a separate process")
        require(state["collectible"], "Running-game project assembly is not collectible")
        for key in ("cpp_counter", "cs_counter", "ready_count"):
            require(state[key] == {"cpp_counter": 91, "cs_counter": 87, "ready_count": 1}[key], key + " changed")
        require(all(state[key] for key in ("vector_ok", "reference_ok", "parents_ok")), "Live state or parent lost")
        require(
            state["cpp_version"] == str(version) and state["cpp_callable"] == str(version),
            "C++ method/cached callable retained old code",
        )
        require(
            state["cs_version"] == cs_version and state["cs_callable"] == cs_version,
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
            + (
                "false\nhot_reload/enable_runtime.editor=true"
                if args.feature_override
                else "false"
                if args.disable_runtime
                else "true"
            )
            + "\n[editor]\nrun/main_run_args="
            + json.dumps(
                f'--headless --max-fps 60 --ignore-error-breaks --log-file "{(output / "game.log").as_posix()}"'
            )
            + '\n[editor_plugins]\nenabled=PackedStringArray("res://addons/reload_fixture/plugin.cfg")\n',
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
        (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", "1"), encoding="utf-8")
        (project / "ReloadReceiver.cs").write_text(RECEIVER, encoding="utf-8")
        run("managed-initial", ["dotnet", "build", "--nologo", "-v", "minimal"])
        editor_command = [str(engine), "--headless", "--editor", "--path", str(project), "--max-fps", "30"]
        receipt["editor_command"] = editor_command
        stream = (output / "editor.log").open("w", encoding="utf-8")
        process = subprocess.Popen(editor_command, env=env, stdout=stream, stderr=subprocess.STDOUT, **options)
        receipt["editor_pid"] = process.pid
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
        if args.native_abi_recovery:
            source = (
                source
                .replace("classes/node.hpp", "classes/node2d.hpp")
                .replace(
                    "class EGP_reload_Node : public Node {", "class EGP_reload_Node : public EGP_reload_Ancestor {"
                )
                .replace("GDCLASS(EGP_reload_Node, Node)", "GDCLASS(EGP_reload_Node, EGP_reload_Ancestor)")
                .replace(
                    "class EGP_reload_Node :",
                    "class EGP_reload_Ancestor : public Node {\n"
                    "    GDCLASS(EGP_reload_Ancestor, Node);\n"
                    "protected:\n    static void _bind_methods() {}\n};\n\n"
                    "class EGP_reload_Parent : public Node2D {\n"
                    "    GDCLASS(EGP_reload_Parent, Node2D);\n"
                    "protected:\n    static void _bind_methods() {}\n};\n\nclass EGP_reload_Node :",
                )
                .replace(
                    "GDREGISTER_CLASS(EGP_reload_Node);",
                    "GDREGISTER_CLASS(EGP_reload_Ancestor);\n        GDREGISTER_CLASS(EGP_reload_Parent);\n        GDREGISTER_CLASS(EGP_reload_Node);",
                )
            )
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
                    (project / "ReloadProbe.cs").write_text(
                        probe_source.replace("VERSION", "invalid!"), encoding="utf-8"
                    )
                    run("managed-invalid", ["dotnet", "build", "--nologo", "-v", "minimal"], expected_success=False)
                    failed = sample()
                    verify(failed, 1, previous)
                    previous = failed
                (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", str(version)), encoding="utf-8")
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
            # Exercise the same debugger command after a C#-only rebuild.
            (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", "4"), encoding="utf-8")
            run("managed-version-4", ["dotnet", "build", "--nologo", "-v", "minimal"])
            command("reload")
            time.sleep(2)
            state = sample()
            verify(state, 3, previous, cs_version=4)
            require(state["after_count"] > previous["after_count"], "C#-only reload did not deserialize")
            previous = state
            command("reload")
            time.sleep(1)
            state = sample()
            verify(state, 3, previous, cs_version=4)
            require(state["after_count"] == previous["after_count"], "No-change command reloaded the assembly again")
            previous = state
            if args.assembly_recovery:
                assembly = project / ".godot/mono/temp/bin/Debug/ReloadFixture.dll"
                backup = assembly.read_bytes()
                time.sleep(1.1)
                assembly.write_bytes(b"EGP deliberately invalid managed assembly")
                command("reload")
                time.sleep(2)
                fallback = sample()
                require(
                    fallback.get("placeholder")
                    and fallback["cs_id"] == previous["cs_id"]
                    and fallback["cs_counter"] == 87,
                    "Failed load lost placeholder identity or state",
                )
                command("drop")
                fallback = sample()
                require(
                    fallback.get("placeholder") and not fallback["extra_alive"], "Deleted placeholder remained alive"
                )
                time.sleep(1.1)
                assembly.write_bytes(backup)
                command("reload")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=4)
                receipt["assembly_recovery"] = True
                previous = state
            if args.unload_recovery:
                command("hold")
                deadline = time.monotonic() + 10
                while not (project / "release-root.started").exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                require((project / "release-root.started").exists(), "Managed application thread did not start")
                (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", "5"), encoding="utf-8")
                run("managed-version-5", ["dotnet", "build", "--nologo", "-v", "minimal"])
                command("reload")
                time.sleep(2)
                fallback = sample()
                require(
                    fallback.get("placeholder") and fallback["cs_counter"] == 87,
                    "Unload failure lost placeholder state",
                )
                (project / "release-root").write_text("release", encoding="utf-8")
                deadline = time.monotonic() + 10
                while not (project / "release-root.finished").exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                require((project / "release-root.finished").exists(), "Managed application thread did not stop")
                command("reload")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5)
                diagnostics = command("diagnostics")["diagnostics"]
                receipt["debugger_diagnostics"] = diagnostics
                require(
                    any(".NET: Failed to unload assemblies." in text for text in diagnostics),
                    "Unload failure debugger diagnostic missing",
                )
                receipt["unload_recovery"] = True
                previous = state
            if args.native_recovery:
                descriptor_text = descriptor.read_text(encoding="utf-8")
                bad_library = project / "extensions/reload/bin/invalid-native.dll"
                invalid_descriptor = re.sub(
                    r'(windows\.debug\.x86_64\s*=\s*)"[^"]+"',
                    r'\1"res://extensions/reload/bin/invalid-native.dll"',
                    descriptor_text,
                )
                require(invalid_descriptor != descriptor_text, "Missing Windows Debug library mapping")
                for fault in ("missing", "invalid"):
                    if fault == "invalid":
                        bad_library.write_bytes(b"EGP deliberately invalid native library")
                    time.sleep(1.1)
                    descriptor.write_text(invalid_descriptor, encoding="utf-8")
                    command("reload")
                    time.sleep(2)
                    fallback = sample()
                    require(
                        fallback.get("native_unavailable")
                        and fallback["cpp_id"] == previous["cpp_id"]
                        and fallback["parent_ok"],
                        "Failed native load lost parent object identity",
                    )
                    if fault == "missing":
                        command("rename-native")
                time.sleep(1.1)
                descriptor.write_text(descriptor_text, encoding="utf-8")
                command("reload")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                require(state["cpp_name"] == "RecoveredNative", "Parent property edit during failure was lost")
                diagnostics = command("diagnostics")["diagnostics"]
                receipt["native_recovery_diagnostics"] = diagnostics
                require(
                    any("GDExtension" in text or "dynamic library" in text for text in diagnostics),
                    "Native failed-load diagnostic missing from debugger",
                )
                receipt["native_recovery"] = True
                previous = state
            if args.native_abi_recovery:
                original = source.replace("VERSION", "3")
                for return_type, result in (("String", '"7"'), ("int", "7")):
                    changed = original.replace('D_METHOD("get_message")', 'D_METHOD("get_message", "value")').replace(
                        'String get_message() const { return "3"; }',
                        f"{return_type} get_message(int value) const {{ return {result}; }}",
                    )
                    require(changed != original, "Unexpected native method scaffold")
                    source_path.write_text(changed, encoding="utf-8")
                    require(command("build", 900)["build_result"] == 0, "Changed-signature build failed")
                    time.sleep(2)
                    changed_state = sample("sample-abi")
                    expected = "7" if return_type == "String" else 7
                    require(
                        changed_state["cpp_version"] == expected
                        and changed_state["cpp_callable"] == expected
                        and changed_state["cpp_counter"] == 91
                        and changed_state["cpp_id"] == previous["cpp_id"]
                        and changed_state["parent_ok"],
                        "Changed-signature dynamic call lost code, state or identity",
                    )
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Original-signature repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                previous = state
                changed_base = (
                    original
                    .replace("classes/node.hpp", "classes/node2d.hpp")
                    .replace(
                        "class EGP_reload_Node : public EGP_reload_Ancestor {",
                        "class EGP_reload_Node : public Node2D {",
                    )
                    .replace("GDCLASS(EGP_reload_Node, EGP_reload_Ancestor)", "GDCLASS(EGP_reload_Node, Node2D)")
                )
                require(changed_base != original, "Unexpected native base scaffold")
                source_path.write_text(changed_base, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Changed-base build failed")
                time.sleep(2)
                # The editor withholds automatic game notification on NEEDS_RESTART.
                # Force this unsafe change in the fixture to exercise game recovery.
                require(sample("reload-native")["status"] == 4, "Changed-base reload did not report NEEDS_RESTART")
                fallback = sample()
                require(
                    fallback.get("native_unavailable")
                    and fallback["cpp_id"] == previous["cpp_id"]
                    and fallback["base_class"] == "Node"
                    and fallback["parent_ok"],
                    "Rejected native base change lost parent identity",
                )
                diagnostics = command("diagnostics")["diagnostics"]
                require(
                    any("cannot change parent type" in text and "Restart Godot" in text for text in diagnostics),
                    "Changed-base restart diagnostic missing",
                )
                require(sample("reload-native")["status"] == 4, "Rejected-base retry did not report NEEDS_RESTART")
                require(
                    any("changed signature" in text and "Cached method bindings" in text for text in diagnostics),
                    "Changed-signature cached-binding diagnostic missing",
                )
                command("rename-native")
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Original-base repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                require(state["cpp_name"] == "RecoveredNative", "Rejected-base parent property edit was lost")
                previous = state
                changed_parent = original.replace(
                    "class EGP_reload_Node : public EGP_reload_Ancestor {",
                    "class EGP_reload_Node : public EGP_reload_Parent {",
                ).replace(
                    "GDCLASS(EGP_reload_Node, EGP_reload_Ancestor)", "GDCLASS(EGP_reload_Node, EGP_reload_Parent)"
                )
                require(changed_parent != original, "Unexpected extension-parent scaffold")
                source_path.write_text(changed_parent, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Changed extension-parent build failed")
                time.sleep(2)
                require(sample("reload-native")["status"] == 4, "Extension-parent change bypassed rejection")
                fallback = sample()
                require(
                    fallback.get("native_unavailable")
                    and fallback["cpp_id"] == previous["cpp_id"]
                    and fallback["base_class"] == "Node"
                    and fallback["parent_ok"],
                    "Rejected extension-parent change lost original native parent",
                )
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Extension-parent repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                previous = state
                changed_ancestor = original.replace(
                    "class EGP_reload_Ancestor : public Node {", "class EGP_reload_Ancestor : public Node2D {"
                ).replace("GDCLASS(EGP_reload_Ancestor, Node)", "GDCLASS(EGP_reload_Ancestor, Node2D)")
                require(changed_ancestor != original, "Unexpected extension ancestor scaffold")
                source_path.write_text(changed_ancestor, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Changed ancestor build failed")
                time.sleep(2)
                require(sample("reload-native")["status"] == 4, "Rejected ancestor did not block descendant reload")
                fallback = sample()
                require(
                    fallback.get("native_unavailable")
                    and fallback["cpp_id"] == previous["cpp_id"]
                    and fallback["base_class"] == "Node"
                    and fallback["parent_ok"],
                    "Rejected ancestor lost descendant's native parent",
                )
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Ancestor repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                previous = state
                # Removing a class with a live object must also allow restoring it.
                removed_class = original.replace("GDREGISTER_CLASS(EGP_reload_Node);", "/* class removed */")
                require(removed_class != original, "Unexpected class registration scaffold")
                source_path.write_text(removed_class, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Removed-class build failed")
                time.sleep(2)
                require(sample("reload-native")["status"] == 4, "Removed-class reload did not report NEEDS_RESTART")
                fallback = sample()
                require(
                    fallback.get("native_unavailable")
                    and fallback["cpp_id"] == previous["cpp_id"]
                    and fallback["parent_ok"],
                    "Removed class lost native parent identity",
                )
                require(sample("reload-native")["status"] == 4, "Removed-class retry did not report NEEDS_RESTART")
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Removed-class repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                require(sample("reload-native")["status"] == 0, "Compatible native retry did not report OK")
                previous = state
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                previous = state
                receipt["native_abi_recovery"] = {
                    "passed": True,
                    "native_builds": 11,
                    "rejected_explicit_retries": 2,
                    "method_changes": ["argument-count", "return-type"],
                    "base_change": "Node to Node2D rejected; Node repair retains state and identity",
                    "extension_parent_change": "Node to extension-derived Node2D rejected; compatible repair retains state",
                    "ancestor_change": "Rejected ancestor blocks descendant registration; compatible repair retains state",
                    "class_removal": "Live parent and state retained until original class is restored",
                    "diagnostics": diagnostics,
                    "scope": "Dynamic methods and Callable lookup; cached raw MethodBind pointers and arbitrary ABI changes remain open",
                }
            receipt["reloads"] = 3 + int(args.assembly_recovery) + int(args.unload_recovery) + int(args.native_recovery)
        command("close")
        require(process.wait(timeout=60) == 0, "Editor/game teardown failed")
        game_log = (output / "game.log").read_text(encoding="utf-8")
        for diagnostic in (
            'Parameter "delegate_handle.value" is null',
            "ManagedCallableMiddleman::",
            "Return type is not bool",
            "SCRIPT ERROR:",
        ):
            require(diagnostic not in game_log, "Unexpected game diagnostic: " + diagnostic)
        receipt["game_diagnostics_checked"] = True
        if args.native_abi_recovery:
            stream.flush()
            log = (output / "editor.log").read_text(encoding="utf-8")
            for diagnostic in (
                "Attempt to unregister unexisting extension class",
                'Parameter "_extension" is null',
                "Cannot call invalid GDExtension method bind",
                "SCRIPT ERROR:",
            ):
                require(diagnostic not in log, "Unexpected reload diagnostic: " + diagnostic)
        require(digest(engine) == receipt["engine_sha256"], "Input engine changed during validation")
        require(
            all(digest(Path(path)) == expected for path, expected in receipt["managed_runtime_sha256"].items()),
            "Managed runtime changed during validation",
        )
        require(
            all(digest(ROOT / path) == expected for path, expected in receipt["fixture_sha256"].items()),
            "Fixture source changed during validation",
        )
        receipt["passed"] = True
    except (OSError, RuntimeError, KeyError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    finally:
        (project / "release-root").write_text("release", encoding="utf-8")
        if process is not None:
            terminate(process)
        if stream is not None:
            stream.close()
        receipt["generated_sha256"] = {
            str(p.relative_to(project)): digest(p)
            for p in project.rglob("*")
            if p.is_file()
            and (
                p.name
                in (
                    "ReloadFixture.dll",
                    "extension.cpp",
                    "ReloadProbe.cs",
                    "reload.gdextension",
                )
                or (p.suffix == ".cs" and "addons" in p.relative_to(project).parts)
                or (p.suffix == ".dll" and "extensions" in p.relative_to(project).parts)
            )
        }
        receipt["logs_sha256"] = {str(p): digest(p) for p in output.glob("*.log")}
        save()
    print("PASS" if receipt["passed"] else "FAIL", output / "receipt.json", receipt.get("error", ""), flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
