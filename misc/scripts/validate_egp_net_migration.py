#!/usr/bin/env python3
"""Verify legacy networking conversion rejects before writes and clean conversion works."""

import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    engine = args.engine.resolve()
    output = (args.output or ROOT / ".build/egp-net-migration" / str(time.time_ns())).resolve()
    output.mkdir(parents=True, exist_ok=True)
    cases = {
        "gd-keyword": ("Actor.gd", "extends Spatial\nremote func move():\n\tpass\n"),
        "gd-rpc-call": ("Actor.gd", 'extends Node\nfunc run():\n\trpc_id(2, "move")\n'),
        "gd-peer": ("Actor.gd", "extends Node\nfunc run():\n\tget_tree().set_network_peer(null)\n"),
        "cs-attribute": ("Actor.cs", "using Godot;\npublic class Actor : Node { [RemoteSync] void Move() {} }\n"),
        "cs-peer": (
            "Actor.cs",
            "using Godot;\npublic class Actor : Node { void Join() { GetTree().SetNetworkPeer(null); } }\n",
        ),
        "embedded": (
            "Actor.tscn",
            '[gd_scene load_steps=2 format=2]\n[sub_resource type="GDScript" id=1]\nscript/source = "extends Node\\nremote func move():\\n\\tpass\\n"\n[node name="Actor" type="Node"]\nscript = SubResource( 1 )\n',
        ),
        "resource": ("Peer.tres", '[gd_resource type="NetworkedMultiplayerENet" format=2]\n[resource]\n'),
        "webrtc-data-native": ("Peer.gd", "extends WebRTCDataChannelGDNative\n"),
        "webrtc-peer-native": ("Peer.gd", "extends WebRTCPeerConnectionGDNative\n"),
        "webrtc-data-extension": ("Peer.gd", "extends WebRTCDataChannelExtension\n"),
        "webrtc-peer-extension": ("Peer.gd", "extends WebRTCPeerConnectionExtension\n"),
        "physics-backend": ("Backend.gd", "extends Physics2DServerSW\n"),
    }
    checks = []
    receipt = {"engine_sha256": digest(engine), "checks": checks, "passed": False}
    try:
        for name, (filename, content) in cases.items():
            for mode in ("validate-conversion-3to4", "convert-3to4"):
                project = output / (name + "-" + mode)
                project.mkdir()
                (project / "project.godot").write_text(
                    'config_version=4\n[application]\nconfig/name="MigrationFixture"\n'
                )
                (project / filename).write_text(content)
                before = {p.name: digest(p) for p in project.iterdir() if p.is_file()}
                result = subprocess.run(
                    [str(engine), "--headless", "--path", str(project), "--" + mode],
                    cwd=project,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    timeout=45,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
                log = result.stdout.decode(errors="replace")
                (output / (project.name + ".log")).write_text(log)
                unchanged = all(digest(project / p) == value for p, value in before.items())
                diagnostic = (
                    "EGP physics migration required"
                    if name == "physics-backend"
                    else "EGP networking migration required"
                )
                passed = (
                    result.returncode != 0
                    and diagnostic in log
                    and "No project files have been modified" in log
                    and unchanged
                )
                checks.append({
                    "name": project.name,
                    "passed": passed,
                    "exit_code": result.returncode,
                    "source_unchanged": unchanged,
                })
                if not passed:
                    raise RuntimeError(project.name)
        project = output / "clean"
        project.mkdir()
        (project / "project.godot").write_text('config_version=4\n[application]\nconfig/name="CleanFixture"\n')
        (project / "Actor.gd").write_text("extends Spatial\nonready var value = 1\n# remote func comment_only():\n")
        result = subprocess.run(
            [str(engine), "--headless", "--path", str(project), "--convert-3to4"],
            cwd=project,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=45,
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
        log = result.stdout.decode(errors="replace")
        (output / "clean.log").write_text(log)
        code = (project / "Actor.gd").read_text()
        passed = (
            result.returncode == 0
            and "extends Node3D" in code
            and "@onready" in code
            and "EGP networking migration required" not in log
        )
        checks.append({"name": "clean-conversion", "passed": passed, "exit_code": result.returncode})
        if not passed:
            raise RuntimeError("clean-conversion")
        receipt["passed"] = True
    except (RuntimeError, OSError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(output / "receipt.json")
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
