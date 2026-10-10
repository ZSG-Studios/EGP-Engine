#!/usr/bin/env python3
"""Synchronize the EGP documentation fork from this engine's public sources."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANUALS = {
    "doc/egp_box2d.md": "box2d.md",
    "doc/egp_box3d.md": "box3d.md",
    "doc/egp_cpp_extensions.md": "cpp_extensions.md",
    "doc/egp_xmake.md": "xmake.md",
    "doc/egp_platform_validation.md": "platform_validation.md",
    "doc/egp_visionos_experimental.md": "visionos_experimental.md",
    "doc/egp_api_contract.md": "api_contract.md",
    "doc/egp_network_lab.md": "network_lab.md",
    "doc/egp_documentation.md": "documentation.md",
    "doc/egp_superpos.md": "networking_reference.md",
    "doc/egp_superpos_migration.md": "superpos_migration.md",
}
SOURCE_URL = "https://github.com/ZSG-Studios/EGP-Engine/blob/"


def digest(path: Path) -> str:
    # Git can check out text with CRLF on Windows; publish platform-neutral hashes.
    return hashlib.sha256(path.read_text(encoding="utf-8").encode("utf-8")).hexdigest()


def tracked_xml_sources() -> list[Path]:
    # An unrelated, untracked module must not become published API documentation.
    tracked = (
        subprocess
        .check_output(["git", "ls-files", "-z", "--", "doc/classes", "modules", "platform"], cwd=ROOT)
        .decode("utf-8")
        .split("\0")
    )
    selected = [
        ROOT / name
        for name in tracked
        if name.endswith(".xml")
        and (name.startswith("doc/classes/") or "doc_classes" in Path(name).parts)
        and (ROOT / name).is_file()
    ]
    manifest_path = ROOT / "modules/superpos/source_manifest.json"
    if manifest_path.is_file():
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        for name in manifest["classes"]:
            source = ROOT / "modules/superpos/doc_classes" / (name + ".xml")
            if source.is_file():
                selected.append(source)
    return sorted(set(selected))


def tracked_sources() -> list[Path]:
    sources = tracked_xml_sources()
    sources.extend(ROOT / name for name in MANUALS)
    sources.extend((ROOT / "modules/superpos").glob("*.h"))
    sources.extend((ROOT / "modules/superpos").glob("*.cpp"))
    sources.extend([ROOT / "modules/superpos/source_manifest.json", ROOT / "modules/superpos/core/source_manifest.json"])
    sources.extend([ROOT / "version.lua", ROOT / "doc/tools/make_rst.py", Path(__file__).resolve()])
    return sorted(set(sources))


def manual(source: str, revision: str) -> str:
    text = (ROOT / source).read_text(encoding="utf-8")

    def link(match: re.Match[str]) -> str:
        label, target = match.groups()
        if re.match(r"[a-zA-Z]+:|#|/", target):
            return match.group(0)
        path, separator, anchor = target.partition("#")
        resolved = (ROOT / source).parent.joinpath(path).resolve()
        try:
            relative = resolved.relative_to(ROOT).as_posix()
        except ValueError:
            return match.group(0)
        destination = MANUALS.get(relative)
        if destination is None:
            destination = SOURCE_URL + revision + "/" + relative
        return f"[{label}]({destination}{separator}{anchor})"

    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, text)
    return f"<!-- Generated from {source}; edit the engine source and run sync_egp_docs.py. -->\n\n{text}"


def helper_reference(revision: str) -> str:
    return """.. _doc_egp_helper_reference:

Superpos language API
=====================

GDScript, generated C# (``Godot.SuperposSession``) and generated godot-cpp
(``godot::SuperposSession``) call the same native ClassDB implementation.
Use the matching editor's generated bindings and C++17 extension SDK.
The independent core's C++23 headers are private engine implementation.

See :doc:`networking_reference` for API usage, checked counters, packet delivery,
owner retirement and qualification limits. See :doc:`superpos_migration` before
porting legacy helper users. Superpos does not install GDScript bridge helpers.

The old ``EGP.Networking`` and ``egp::networking`` facades and Superposition
nodes belong to the retired transport. Their declarations are not this API.
"""

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--docs", type=Path, required=True, help="Existing EGP-docs checkout")
    parser.add_argument("--check", action="store_true", help="Verify generated files without modifying the checkout")
    args = parser.parse_args()
    docs = args.docs.resolve()
    if docs == ROOT or not (docs / "conf.py").is_file() or not (docs / "egp/index.rst").is_file():
        parser.error("Destination must be the initialized EGP-docs fork, not the engine checkout")
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    sources = {path.relative_to(ROOT).as_posix(): digest(path) for path in tracked_sources()}
    with tempfile.TemporaryDirectory(prefix="egp-docs-") as temporary:
        output = Path(temporary)
        classes = output / "classes"
        classes.mkdir()
        generator_arguments = [
            str(ROOT / "doc/tools/make_rst.py"),
            "-o",
            str(classes),
            *(str(path) for path in tracked_xml_sources()),
        ]
        # Pass the exact tracked input list on stdin; Windows cannot fit the full
        # class inventory into its process command line. Keep original source
        # paths so generated provenance and class grouping remain accurate.
        subprocess.run(
            [
                sys.executable,
                "-c",
                "import json, runpy, sys; sys.argv = json.load(sys.stdin); "
                "runpy.run_path(sys.argv[0], run_name='__main__')",
            ],
            input=json.dumps(generator_arguments),
            text=True,
            cwd=ROOT,
            check=True,
        )
        expected = {
            "classes/" + path.name: path.read_text(encoding="utf-8").encode("utf-8") for path in classes.glob("*.rst")
        }
        expected.update({
            "egp/" + destination: manual(source, revision).encode("utf-8") for source, destination in MANUALS.items()
        })
        expected["egp/helper_reference.rst"] = helper_reference(revision).encode("utf-8")
        manifest = {
            "schema_version": 1,
            "engine_repository": "ZSG-Studios/EGP-Engine",
            "engine_revision": revision,
            "class_count": len(list(classes.glob("class_*.rst"))),
            "source_sha256": sources,
            "generated_sha256": {
                name: hashlib.sha256(content).hexdigest() for name, content in sorted(expected.items())
            },
        }
        expected["egp/source_manifest.json"] = (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode("utf-8")
        changed = [
            name
            for name, content in expected.items()
            if not (docs / name).is_file() or (docs / name).read_bytes() != content
        ]
        removed = [path for path in (docs / "classes").glob("class_*.rst") if "classes/" + path.name not in expected]
        if args.check:
            if changed or removed:
                print("Documentation drift: " + ", ".join(changed + ["removed:" + path.name for path in removed]))
                return 1
        else:
            for name in changed:
                path = docs / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(expected[name])
            for path in removed:
                path.unlink()
        print(
            f"EGP docs {'verified' if args.check else 'synchronized'}: {manifest['class_count']} classes, {len(MANUALS)} guides; engine {revision}"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
