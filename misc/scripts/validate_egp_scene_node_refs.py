#!/usr/bin/env python3
"""Verify inherited GDScript/C# exported Node references and typed collections.

The C# cases use a fixture-local typed Node subclass, so this engine check has no
networking dependency.
"""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path
from xml.sax.saxutils import escape

TEMPLATES = {
    "base.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://owner.gd" id="1"]\n[node name="Owner" type="Node" node_paths=PackedStringArray("Single", "Items", "Values", "Keys", "ExistingRef")]\nscript=ExtResource("1")\nSingle=NodePath("Late")\nItems=Array[NodePath]([NodePath("Late"), NodePath("Existing"), NodePath("Late")])\nValues=Dictionary[String, NodePath]({"late":NodePath("Late"), "existing":NodePath("Existing")})\nKeys=Dictionary[NodePath, NodePath]({NodePath("Late"):NodePath("Existing"), NodePath("Existing"):NodePath("Late")})\nExistingRef=NodePath("Existing")\n[node name="Existing" type="Node" parent="."]\n',
    "check.gd": 'extends SceneTree\nfunc _initialize() -> void:\n\tvar proofs := []\n\tfor name in ["plain", "inherited", "layered", "cs_plain", "cs_inherited", "cs_layered"]:\n\t\tvar scene: PackedScene = load("res://%s.tscn" % name)\n\t\tvar node: Node = scene.instantiate()\n\t\tvar late := node.get_node("Late")\n\t\tvar existing := node.get_node("Existing")\n\t\tproofs.append({"case":name, "single":node.Single == late, "items":node.Items == [late, existing, late], "values":node.Values == {"late":late, "existing":existing}, "keys":node.Keys == {late:existing, existing:late}, "existing":node.ExistingRef == existing, "typed":node.TypedReferences() if node.has_method("TypedReferences") else true})\n\t\tnode.free()\n\tprint("EGP_SCENE_REFS ", JSON.stringify(proofs))\n\tquit(0)\n',
    "cs_base.tscn": '[gd_scene load_steps=3 format=3]\n[ext_resource type="Script" path="res://ReferenceOwner.cs" id="1"]\n[ext_resource type="Script" path="res://ReferenceTarget.cs" id="2"]\n[node name="Owner" type="Node" node_paths=PackedStringArray("Single", "Items", "Values", "Keys", "ExistingRef")]\nscript=ExtResource("1")\nSingle=NodePath("Late")\nItems=Array[NodePath]([NodePath("Late"), NodePath("Existing"), NodePath("Late")])\nValues=Dictionary[String, NodePath]({"late":NodePath("Late"), "existing":NodePath("Existing")})\nKeys=Dictionary[NodePath, NodePath]({NodePath("Late"):NodePath("Existing"), NodePath("Existing"):NodePath("Late")})\nExistingRef=NodePath("Existing")\n[node name="Existing" type="Node" parent="."]\nscript=ExtResource("2")\n',
    "cs_inherited.tscn": '[gd_scene load_steps=3 format=3]\n[ext_resource type="PackedScene" path="res://cs_base.tscn" id="1"]\n[ext_resource type="Script" path="res://ReferenceTarget.cs" id="2"]\n[node name="Owner" instance=ExtResource("1")]\n[node name="Late" type="Node" parent="."]\nscript=ExtResource("2")\n',
    "cs_layered.tscn": '[gd_scene load_steps=3 format=3]\n[ext_resource type="PackedScene" path="res://cs_middle.tscn" id="1"]\n[ext_resource type="Script" path="res://ReferenceTarget.cs" id="2"]\n[node name="Owner" instance=ExtResource("1")]\n[node name="Late" type="Node" parent="."]\nscript=ExtResource("2")\n',
    "cs_middle.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="PackedScene" path="res://cs_base.tscn" id="1"]\n[node name="Owner" instance=ExtResource("1")]\n[node name="Intermediate" type="Node" parent="."]\n',
    "cs_plain.tscn": '[gd_scene load_steps=3 format=3]\n[ext_resource type="Script" path="res://ReferenceOwner.cs" id="1"]\n[ext_resource type="Script" path="res://ReferenceTarget.cs" id="2"]\n[node name="Owner" type="Node" node_paths=PackedStringArray("Single", "Items", "Values", "Keys", "ExistingRef")]\nscript=ExtResource("1")\nSingle=NodePath("Late")\nItems=Array[NodePath]([NodePath("Late"), NodePath("Existing"), NodePath("Late")])\nValues=Dictionary[String, NodePath]({"late":NodePath("Late"), "existing":NodePath("Existing")})\nKeys=Dictionary[NodePath, NodePath]({NodePath("Late"):NodePath("Existing"), NodePath("Existing"):NodePath("Late")})\nExistingRef=NodePath("Existing")\n[node name="Existing" type="Node" parent="."]\nscript=ExtResource("2")\n[node name="Late" type="Node" parent="."]\nscript=ExtResource("2")\n',
    "inherited.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="PackedScene" path="res://base.tscn" id="1"]\n[node name="Owner" instance=ExtResource("1")]\n[node name="Late" type="Node" parent="."]\n',
    "layered.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="PackedScene" path="res://middle.tscn" id="1"]\n[node name="Owner" instance=ExtResource("1")]\n[node name="Late" type="Node" parent="."]\n',
    "middle.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="PackedScene" path="res://base.tscn" id="1"]\n[node name="Owner" instance=ExtResource("1")]\n[node name="Intermediate" type="Node" parent="."]\n',
    "owner.gd": "extends Node\n@export var Single: Node\n@export var Items: Array[Node] = []\n@export var Values: Dictionary[String, Node] = {}\n@export var Keys: Dictionary[Node, Node] = {}\n@export var ExistingRef: Node\n",
    "plain.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://owner.gd" id="1"]\n[node name="Owner" type="Node" node_paths=PackedStringArray("Single", "Items", "Values", "Keys", "ExistingRef")]\nscript=ExtResource("1")\nSingle=NodePath("Late")\nItems=Array[NodePath]([NodePath("Late"), NodePath("Existing"), NodePath("Late")])\nValues=Dictionary[String, NodePath]({"late":NodePath("Late"), "existing":NodePath("Existing")})\nKeys=Dictionary[NodePath, NodePath]({NodePath("Late"):NodePath("Existing"), NodePath("Existing"):NodePath("Late")})\nExistingRef=NodePath("Existing")\n[node name="Existing" type="Node" parent="."]\n[node name="Late" type="Node" parent="."]\n',
    "ReferenceTarget.cs": "using Godot;\npublic partial class ReferenceTarget : Node {\n}\n",
    "ReferenceOwner.cs": "using Godot;\npublic partial class ReferenceOwner : Node {\n    [Export] public ReferenceTarget? Single { get; set; }\n    [Export] public Godot.Collections.Array<ReferenceTarget> Items { get; set; } = new();\n    [Export] public Godot.Collections.Dictionary<string, ReferenceTarget> Values { get; set; } = new();\n    [Export] public Godot.Collections.Dictionary<ReferenceTarget, ReferenceTarget> Keys { get; set; } = new();\n    [Export] public ReferenceTarget? ExistingRef { get; set; }\n    public bool TypedReferences() => Single != null && Items.Count == 3 && Items[0] is ReferenceTarget && ExistingRef is ReferenceTarget;\n}\n",
}


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def source_commit(source):
    """Record the checkout commit, or the snapshot head of a non-git source mirror."""
    try:
        return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True,
                                       stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        marker = source / ".egp-source-head"
        return ("snapshot:" + marker.read_text(encoding="utf-8").strip()) if marker.is_file() else None


def evidence_failure(rows, legacy=False):
    expected = ["plain", "inherited", "layered", "cs_plain", "cs_inherited", "cs_layered"]
    if (
        not isinstance(rows, list)
        or any(not isinstance(r, dict) for r in rows)
        or [r.get("case") for r in rows] != expected
    ):
        return "Missing or unordered scene reference cases"
    for row in rows:
        inherited = "inherited" in row["case"] or "layered" in row["case"]
        success = not legacy or not inherited
        if row.get("existing") is not True:
            return "Existing reference regressed"
        if any(row.get(key) is not success for key in ("single", "items", "values", "keys")):
            return "Scalar/array/dictionary inherited reference mismatch"
        typed = success if row["case"].startswith("cs_") else True
        if row.get("typed") is not typed:
            return "C# typed ReferenceTarget reference mismatch"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--debug-template", type=Path)
    parser.add_argument("--release-template", type=Path)
    parser.add_argument(
        "--expect-missing-inherited",
        action="store_true",
        help="Record the legacy missing-reference control; not release qualification",
    )
    args = parser.parse_args()
    if bool(args.debug_template) != bool(args.release_template):
        parser.error("Both Debug and Release templates are required for packaged qualification")
    source = args.source_root.resolve()
    engine = args.engine.resolve()
    out = args.output.resolve() / str(time.time_ns())
    project = out / "project"
    project.mkdir(parents=True)
    environment = os.environ.copy()
    environment["NUGET_PACKAGES"] = str(out / "nuget-packages")
    r = {
        "passed": False,
        "engine": str(engine),
        "engine_sha256": digest(engine),
        "source_commit": source_commit(source),
        "legacy_control": args.expect_missing_inherited,
        "steps": [],
    }

    def save():
        (out / "receipt.json").write_text(json.dumps(r, indent=2) + "\n", encoding="utf-8")

    def run(name, command, cwd, timeout):
        log = out / (name + ".log")
        with log.open("w", encoding="utf-8") as stream:
            child = subprocess.Popen(
                [str(x) for x in command],
                cwd=cwd,
                stdout=stream,
                stderr=subprocess.STDOUT,
                stdin=subprocess.DEVNULL,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                env=environment,
            )
            try:
                code = child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                if os.name == "nt":
                    subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
                else:
                    child.kill()
                child.wait(timeout=15)
                raise RuntimeError(name + " watchdog expired")
        r["steps"].append({
            "name": name,
            "command": [str(x) for x in command],
            "cwd": str(cwd),
            "pid": child.pid,
            "exit_code": code,
            "log": str(log),
            "log_sha256": digest(log),
        })
        save()
        if code or (name.startswith("export-") and "ERROR:" in log.read_text(encoding="utf-8")):
            raise RuntimeError(name + " failed")
        return log.read_text(encoding="utf-8")

    try:
        for name, text in TEMPLATES.items():
            (project / name).write_text(text, encoding="utf-8")
        (project / "main.gd").write_text(
            TEMPLATES["check.gd"]
            .replace("extends SceneTree", "extends Node")
            .replace("func _initialize()", "func _ready()")
            .replace("\tquit(0)", "\tget_tree().quit(0)"),
            encoding="utf-8",
        )
        (project / "main.tscn").write_text(
            '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://main.gd" id="1"]\n[node name="Fixture" type="Node"]\nscript=ExtResource("1")\n',
            encoding="utf-8",
        )
        (project / "project.godot").write_text(
            'config_version=5\n[application]\nconfig/name="SceneReferenceFixture"\nrun/main_scene="res://main.tscn"\n[dotnet]\nproject/assembly_name="SceneReferenceFixture"\n',
            encoding="utf-8",
        )
        (project / "SceneReferenceFixture.csproj").write_text(
            '<Project Sdk="Godot.NET.Sdk/4.8.0-dev"><PropertyGroup><TargetFramework>net10.0</TargetFramework><EnableDynamicLoading>true</EnableDynamicLoading><Nullable>enable</Nullable></PropertyGroup></Project>',
            encoding="utf-8",
        )
        (project / "NuGet.Config").write_text(
            '<configuration><packageSources><clear/><add key="egp" value="'
            + escape(str(args.packages.resolve()))
            + '"/><add key="nuget" value="https://api.nuget.org/v3/index.json"/></packageSources></configuration>',
            encoding="utf-8",
        )
        r["validator_sha256"] = digest(__file__)
        run(
            "solution-create",
            ["dotnet", "new", "sln", "--name", "SceneReferenceFixture", "--format", "sln"],
            project,
            30,
        )
        run(
            "solution-add",
            ["dotnet", "sln", "SceneReferenceFixture.sln", "add", "SceneReferenceFixture.csproj"],
            project,
            30,
        )
        r["fixture_sha256"] = {str(p.relative_to(project)): digest(p) for p in project.rglob("*") if p.is_file()}
        save()
        run("managed-build", ["dotnet", "build", "--nologo", "-v", "minimal"], project, 120)
        output = run(
            "scene-references",
            [
                engine,
                "--headless",
                "--path",
                project,
                "--script",
                "res://check.gd",
                "--max-fps",
                "60",
                "--log-file",
                out / "engine.log",
            ],
            project,
            30,
        )
        if "SCRIPT ERROR:" in output:
            raise RuntimeError("Unexpected scene script diagnostic")
        lines = [line.split("EGP_SCENE_REFS ", 1)[1] for line in output.splitlines() if "EGP_SCENE_REFS " in line]
        if len(lines) != 1:
            raise RuntimeError("Missing or duplicated reference evidence")
        r["proofs"] = json.loads(lines[0])
        failure = evidence_failure(r["proofs"], args.expect_missing_inherited)
        if failure:
            raise RuntimeError(failure)
        r["packaged"] = []
        if args.debug_template:
            run(
                "cold-import",
                [engine, "--headless", "--editor", "--path", project, "--import", "--max-fps", "30"],
                project,
                120,
            )
            for configuration, path in [("debug", args.debug_template), ("release", args.release_template)]:
                template = path.resolve()
                template_sha = digest(template)
                (project / "export_presets.cfg").write_text(
                    f'''[preset.0]
name="Scene References"
platform="Windows Desktop"
runnable=true
export_filter="all_resources"
include_filter="*.gd,*.tscn"
exclude_filter=""

[preset.0.options]
custom_template/{configuration}="{template.as_posix()}"
binary_format/architecture="x86_64"
binary_format/embed_pck=false
application/modify_resources=false
debug/export_console_wrapper=0
''',
                    encoding="utf-8",
                )
                staging = out / ("staging-" + configuration)
                staging.mkdir()
                game = staging / "EGP.SceneReferences.exe"
                run(
                    "export-" + configuration,
                    [
                        engine,
                        "--headless",
                        "--path",
                        project,
                        "--max-fps",
                        "30",
                        "--export-" + configuration,
                        "Scene References",
                        game,
                    ],
                    project,
                    300,
                )
                relocated = out / ("relocated-" + configuration)
                shutil.copytree(staging, relocated)
                game = relocated / game.name
                if not game.with_suffix(".pck").is_file():
                    raise RuntimeError("Packaged PCK missing")
                text = run(
                    "packaged-" + configuration,
                    [game, "--headless", "--max-fps", "60", "--log-file", out / (configuration + "-engine.log")],
                    relocated,
                    30,
                )
                lines = [line.split("EGP_SCENE_REFS ", 1)[1] for line in text.splitlines() if "EGP_SCENE_REFS " in line]
                if len(lines) != 1 or "SCRIPT ERROR:" in text:
                    raise RuntimeError("Invalid packaged scene reference evidence")
                proofs = json.loads(lines[0])
                failure = evidence_failure(proofs, args.expect_missing_inherited)
                if failure:
                    raise RuntimeError(configuration + ": " + failure)
                if digest(template) != template_sha:
                    raise RuntimeError("Template changed during export")
                r["packaged"].append({
                    "configuration": configuration,
                    "template_sha256": template_sha,
                    "proofs": proofs,
                    "assertions": 36,
                    "bundle_sha256": {
                        str(p.relative_to(relocated)): digest(p) for p in sorted(relocated.rglob("*")) if p.is_file()
                    },
                })
        r["assembly_sha256"] = digest(project / ".godot/mono/temp/bin/Debug/SceneReferenceFixture.dll")
        if digest(engine) != r["engine_sha256"] or digest(__file__) != r["validator_sha256"]:
            raise RuntimeError("Engine/validator inputs changed during test")
        r["passed"] = True
        r["assertions"] = 36
        r["scope"] = (
            "Six non-inherited/inherited/three-level packed scenes, GDScript Node and C# ReferenceTarget scalar/array/dictionary key/value references; no arbitrary scene state, networking or hot-reload claim."
        )
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        r["error"] = str(error)
    finally:
        r["logs_sha256"] = {str(p): digest(p) for p in out.glob("*.log")}
        save()
    print("PASS" if r["passed"] else "FAIL", out / "receipt.json", r.get("error", ""), flush=True)
    return 0 if r["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
