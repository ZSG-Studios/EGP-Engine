#!/usr/bin/env python3
"""Format changed C# files using an explicit project and individual include paths."""

import os
import subprocess
import sys
from pathlib import Path


def project_commands(files, root):
    projects = list(root.glob("**/*.csproj"))
    groups = {}
    standalone = {}
    for file in files:
        source = (root / file).resolve()
        owners = [project for project in projects if source.is_relative_to(project.parent.resolve())]
        if not owners:
            # Installed helper sources intentionally have no project. Still
            # format them, without inventing an SDK or silently skipping them.
            standalone.setdefault(source.parent, []).append(str(source))
            continue
        # Nested projects own their sources; a parent project must not format them twice.
        owner = max(owners, key=lambda project: len(project.parent.parts))
        groups.setdefault(owner, []).append(str(source))
    commands = []
    for directory, includes in sorted(standalone.items()):
        commands.append(["dotnet", "format", "whitespace", str(directory), "--folder", "--include", *includes])
    for project, includes in sorted(groups.items()):
        # Game samples consume the engine's generated Godot.NET.Sdk, which isn't
        # installed during static lint CI. Folder whitespace formatting needs no
        # MSBuild/SDK resolution and is the same formatting check CI performs.
        if 'Sdk="Godot.NET.Sdk' in project.read_text(encoding="utf-8"):
            commands.append(["dotnet", "format", "whitespace", str(project.parent), "--folder", "--include", *includes])
        else:
            commands.append(["dotnet", "format", str(project), "--include", *includes])
    return commands


def main(argv=None):
    files = sys.argv[1:] if argv is None else argv
    if not files:
        print("Usage: dotnet_format.py FILE [FILE ...]")
        return 1
    root = Path.cwd()
    try:
        commands = project_commands(files, root)
    except ValueError as error:
        print(str(error), file=sys.stderr)
        return 1
    generated = root / "modules/mono/SdkPackageVersions.props"
    if not generated.exists():
        generated.parent.mkdir(parents=True, exist_ok=True)
        generated.write_text("<Project />", encoding="utf-8")
    environment = dict(os.environ, GodotSkipGenerated="true")
    failed = False
    for command in commands:
        result = subprocess.run(command, env=environment)
        failed = result.returncode != 0 or failed
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
