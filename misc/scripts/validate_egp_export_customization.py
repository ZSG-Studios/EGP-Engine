"""Verify modified export scenes still serialize plugin changes in both templates."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--editor", type=Path, required=True)
parser.add_argument("--debug-template", type=Path, required=True)
parser.add_argument("--release-template", type=Path, required=True)
parser.add_argument("--source-root", type=Path, default=Path(__file__).resolve().parents[2])
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
ROOT = args.source_root.resolve()
TREE = ROOT
OUT = args.output.resolve() / str(time.time_ns())
PROJECT = OUT / "project"
(PROJECT / "addons/export_proof").mkdir(parents=True)
FILES = {
    "project.godot": 'config_version=5\n[application]\nconfig/name="ExportCustomizationProof"\nrun/main_scene="res://main.tscn"\n[editor_plugins]\nenabled=PackedStringArray("res://addons/export_proof/plugin.cfg")\n',
    "main.tscn": '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://main.gd" id="1"]\n[node name="Fixture" type="Node"]\nscript=ExtResource("1")\n[node name="Original" type="Node" parent="."]\n',
    "main.gd": 'extends Node\nfunc _ready() -> void:\n\tvar proof := {"metadata":get_meta("export_probe", 0) == 42, "renamed":has_node("Customized"), "original_removed":not has_node("Original")}\n\tprint("EGP_EXPORT_CUSTOMIZATION ", JSON.stringify(proof))\n\tget_tree().quit(0 if proof.metadata and proof.renamed and proof.original_removed else 1)\n',
    "addons/export_proof/plugin.cfg": '[plugin]\nname="ExportProof"\ndescription="Isolated export customization acceptance"\nauthor="EGP"\nversion="1.0"\nscript="plugin.gd"\n',
    "addons/export_proof/plugin.gd": """@tool
extends EditorPlugin
class ExportProof extends EditorExportPlugin:
	func _get_name() -> String:
		return "EGPExportProof"
	func _begin_customize_scenes(_platform: EditorExportPlatform, _features: PackedStringArray) -> bool:
		return true
	func _get_customization_configuration_hash() -> int:
		return 1
	func _customize_scene(root: Node, path: String) -> Node:
		if path != "res://main.tscn":
			return null
		root.set_meta("export_probe", 42)
		root.get_node("Original").name = "Customized"
		return root
var exporter := ExportProof.new()
func _enter_tree() -> void:
	add_export_plugin(exporter)
func _exit_tree() -> void:
	remove_export_plugin(exporter)
""",
}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


for rel, content in FILES.items():
    (PROJECT / rel).write_text(content, encoding="utf-8")
EDITOR = args.editor.resolve()
r = dict(
    passed=False,
    source_commit=subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=TREE, text=True).strip(),
    editor_sha256=sha(EDITOR),
    fixture_sha256={rel: sha(PROJECT / rel) for rel in FILES},
    steps=[],
    validator_sha256=sha(__file__),
)


def save():
    (OUT / "receipt.json").write_text(json.dumps(r, indent=2) + "\n", encoding="utf-8")


def run(name, command, cwd, timeout):
    log = OUT / (name + ".log")
    with log.open("w", encoding="utf-8") as stream:
        child = subprocess.Popen(
            list(map(str, command)),
            cwd=cwd,
            stdout=stream,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        try:
            code = child.wait(timeout)
        except subprocess.TimeoutExpired:
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
            else:
                child.kill()
            child.wait(15)
            raise RuntimeError(name + " watchdog expired")
    text = log.read_text(encoding="utf-8")
    r["steps"].append(
        dict(name=name, command=list(map(str, command)), exit_code=code, pid=child.pid, log_sha256=sha(log))
    )
    save()
    if code or "ERROR:" in text:
        raise RuntimeError(name + " failed")
    return text


try:
    run(
        "cold-import",
        [EDITOR, "--headless", "--editor", "--path", PROJECT, "--import", "--max-fps", "30"],
        PROJECT,
        120,
    )
    for config in ("debug", "release"):
        template = (args.debug_template if config == "debug" else args.release_template).resolve()
        staging = OUT / ("staging-" + config)
        staging.mkdir()
        (PROJECT / "export_presets.cfg").write_text(
            f'''[preset.0]
name="Customization"
platform="Windows Desktop"
runnable=true
export_filter="all_resources"
include_filter="*.gd"
exclude_filter="addons/export_proof/*"
[preset.0.options]
custom_template/{config}="{template.as_posix()}"
binary_format/architecture="x86_64"
binary_format/embed_pck=false
application/modify_resources=false
debug/export_console_wrapper=0
''',
            encoding="utf-8",
        )
        game = staging / "EGP.ExportCustomization.exe"
        run(
            "export-" + config,
            [EDITOR, "--headless", "--path", PROJECT, "--max-fps", "30", "--export-" + config, "Customization", game],
            PROJECT,
            300,
        )
        relocated = OUT / ("relocated-" + config)
        shutil.copytree(staging, relocated)
        game = relocated / game.name
        text = run("runtime-" + config, [game, "--headless", "--max-fps", "60"], relocated, 30)
        markers = [
            line.split("EGP_EXPORT_CUSTOMIZATION ", 1)[1]
            for line in text.splitlines()
            if "EGP_EXPORT_CUSTOMIZATION " in line
        ]
        assert len(markers) == 1
        proof = json.loads(markers[0])
        assert proof == dict(metadata=True, renamed=True, original_removed=True)
        r[config] = dict(
            proof=proof,
            template_sha256=sha(template),
            bundle_sha256={str(p.relative_to(relocated)): sha(p) for p in relocated.rglob("*") if p.is_file()},
        )
    r.update(
        passed=True,
        assertions=6,
        scope="Plugin-modified root metadata and renamed child survive binary scene repacking in Debug and Release; unmodified inherited scenes have separate six-scene/36-assertion receipts",
    )
except Exception as error:
    r["error"] = str(error)
finally:
    save()
print("PASS" if r["passed"] else "FAIL", OUT / "receipt.json", r.get("error", ""), flush=True)
raise SystemExit(0 if r["passed"] else 1)
