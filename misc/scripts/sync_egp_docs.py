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
    "doc/egp_fastbuild.md": "fastbuild.md",
    "doc/egp_api_contract.md": "api_contract.md",
    "doc/egp_network_lab.md": "network_lab.md",
    "doc/egp_documentation.md": "documentation.md",
    "modules/egp_net/README.md": "networking_reference.md",
    "modules/egp_net/SUPERPOSITION.md": "superposition.md",
    "demos/box3d_arena/README.md": "physics_arena.md",
    "demos/box3d_deterministic/README.md": "deterministic_demo.md",
}
SOURCE_URL = "https://github.com/ZSG-Studios/EGP/blob/"


def digest(path: Path) -> str:
    # Git can check out text with CRLF on Windows; publish platform-neutral hashes.
    return hashlib.sha256(path.read_text(encoding="utf-8").encode("utf-8")).hexdigest()


def tracked_sources() -> list[Path]:
    # This is the same input selection used by the engine's own RST generator.
    sources = list((ROOT / "doc/classes").glob("*.xml"))
    for folder in (ROOT / "modules", ROOT / "platform"):
        for docs in folder.rglob("doc_classes"):
            sources.extend(docs.glob("*.xml"))
    sources.extend(ROOT / name for name in MANUALS)
    sources.extend((ROOT / "modules/egp_net/gdscript").glob("*.gd"))
    sources.extend((ROOT / "modules/egp_net/csharp").glob("*.cs"))
    sources.extend((ROOT / "modules/egp_net/cpp").glob("*.hpp"))
    sources.extend([ROOT / "version.py", ROOT / "doc/tools/make_rst.py", Path(__file__).resolve()])
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
    lines = [
        ".. _doc_egp_helper_reference:",
        "",
        "Networking helper API",
        "=====================",
        "",
        "These declarations are generated from EGP's shipped helper sources. Install them",
        "with ``install_egp_net_helpers.py`` before using the high-level APIs. Native",
        "classes are documented separately in the :ref:`class reference <doc_class_reference>`.",
        "",
        "For behavior, ownership, limits and errors, see :doc:`networking_reference`.",
        "",
        "GDScript",
        "--------",
        "",
    ]
    for source in sorted((ROOT / "modules/egp_net/gdscript").glob("*.gd")):
        text = source.read_text(encoding="utf-8")
        name = re.search(r"^class_name (\w+)", text, re.M)
        if not name:
            continue
        heading = name[1]
        lines.extend([heading, "~" * len(heading), ""])
        lines.extend([f"`Source <{SOURCE_URL}{revision}/{source.relative_to(ROOT).as_posix()}>`__", ""])
        # Underscore-prefixed hooks belong to the implementation, not the facade.
        signatures = re.findall(r"^func ((?!_)\w+\([^\n]*?\)(?: -> [^:\n]+)?):", text, re.M)
        lines.extend([".. code-block:: gdscript", ""] + ["    func " + signature for signature in signatures] + [""])
        signals = re.findall(r"^signal .+$", text, re.M)
        if signals:
            lines.extend(
                ["Signals:", "", ".. code-block:: gdscript", ""] + ["    " + signal for signal in signals] + [""]
            )
    lines.extend([
        "C#",
        "--",
        "",
        "Use the ``EGP.Networking`` namespace. The source files below declare the typed",
        "options, events, results, ownership and disposal contracts.",
        "",
    ])
    for source in sorted((ROOT / "modules/egp_net/csharp").glob("*.cs")):
        if source.name == "Shared.cs":
            continue
        heading = source.stem
        lines.extend([
            heading,
            "~" * len(heading),
            "",
            f"`Source <{SOURCE_URL}{revision}/{source.relative_to(ROOT).as_posix()}>`__",
            "",
        ])
        signatures = []
        for line in source.read_text(encoding="utf-8").splitlines():
            stripped = line.strip()
            if not stripped.startswith("public ") or " class " in stripped or " enum " in stripped:
                continue
            declaration = stripped.split("=>", 1)[0].split("{", 1)[0].rstrip().rstrip(";")
            if declaration:
                signatures.append(declaration + ";")
        lines.extend([".. code-block:: csharp", ""] + ["    " + signature for signature in signatures] + [""])
    lines.extend([
        "C++",
        "---",
        "",
        "Include ``addons/egp_net/cpp/egp_net.hpp`` and use ``egp::networking``.",
        "The header declares ``Options``, ``Session``, ``Net``, ``Prediction``,",
        "``Box3D``, and 2D/3D presentation helpers. Keep wrappers alive for their",
        "callbacks; perform calls and destruction on the constructing Godot thread.",
        "",
        f"`Complete C++ declarations <{SOURCE_URL}{revision}/modules/egp_net/cpp/egp_net.hpp>`__",
        "",
        "Standalone native servers instead include ``modules/egp_net/net_core.h`` and",
        "use ``egp::net::Session``. This API does not require the GDScript codec.",
        "",
    ])
    return "\n".join(lines)


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
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "doc/tools/make_rst.py"),
                "-o",
                str(classes),
                str(ROOT / "doc/classes"),
                str(ROOT / "modules"),
                str(ROOT / "platform"),
            ],
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
            "engine_repository": "ZSG-Studios/EGP",
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
