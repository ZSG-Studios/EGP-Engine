#!/usr/bin/env python3
"""Install EGP's shared networking helpers and optional C#/C++ APIs."""
import argparse
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--project", type=Path, required=True)
parser.add_argument("--languages", nargs="+", choices=("gdscript", "csharp", "cpp"), default=["gdscript"])
args = parser.parse_args()
project = args.project.resolve()
if not (project / "project.godot").is_file():
    parser.error("Destination must contain project.godot")
destination = project / "addons/egp_net"
destination.mkdir(parents=True, exist_ok=True)
# High-level façades deliberately share the GDScript codec and its validation.
groups = [("gdscript", "*.gd", destination)]
if "csharp" in args.languages:
    groups.append(("csharp", "*.cs", destination / "csharp"))
if "cpp" in args.languages:
    groups.append(("cpp", "*.hpp", destination / "cpp"))
for language, pattern, folder in groups:
    folder.mkdir(parents=True, exist_ok=True)
    for source in (ROOT / "modules/egp_net" / language).glob(pattern):
        target = folder / source.name
        if target.exists() and target.read_bytes() != source.read_bytes():
            shutil.copy2(target, target.with_suffix(target.suffix + ".previous"))
        shutil.copy2(source, target)
print(f"Installed EGP networking APIs ({', '.join(args.languages)}) into {destination}")
