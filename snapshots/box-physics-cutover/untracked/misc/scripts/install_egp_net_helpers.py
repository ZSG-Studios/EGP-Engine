#!/usr/bin/env python3
"""Install EGP's GDScript AIO helpers into an existing game project."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--project", type=Path, required=True)
args = parser.parse_args()
project = args.project.resolve()
if not (project / "project.godot").is_file():
    parser.error("Destination must contain project.godot")
destination = project / "addons/egp_net"
destination.mkdir(parents=True, exist_ok=True)
for source in (ROOT / "modules/egp_net/gdscript").glob("*.gd"):
    target = destination / source.name
    if target.exists() and target.read_bytes() != source.read_bytes():
        shutil.copy2(target, target.with_suffix(".gd.previous"))
    shutil.copy2(source, target)
print(f"Installed EGPNet and EGPNetEntity3D into {destination}")
