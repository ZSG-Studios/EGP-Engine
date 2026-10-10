#!/usr/bin/env python3
"""Pin and verify every vendored library under thirdparty/ with one manifest schema.

Each library directory holds an `UPSTREAM.json` manifest (schema `egp-thirdparty/1`):

    {
      "schema": "egp-thirdparty/1",
      "name": "zstd",
      "license": "BSD-3-Clause",
      "upstream": {"repository": "...", "version": "1.5.7", "commit": "<40 hex>"},
      "verification": {"method": "upstream-commit" | "baseline", "reason": "..."},
      "files":     {"<path>": "<sha256>"},                  # byte-identical to upstream
      "patched":   {"<path>": {"upstream_path": "...", "upstream_sha256": "...", "sha256": "..."}},
                                                            # (null upstream fields: created by `patch`)
      "additions": {"<path>": "<sha256>"}                   # not from upstream (patches, glue, docs)
    }

Optional fields: `patches` ([{"name", "path", "sha256", "purpose"}], patch files relative to
the engine root; each `patched` entry may name its `patch`), `additions_reason`, and
`excluded_upstream_files` ({path: {"upstream_sha256", "reason", ...}}) for upstream files that
are intentionally not vendored.

Paths are relative to the library directory. Every tracked file in the directory, except
the manifest itself, must appear in exactly one of `files`, `patched` or `additions`.
Contents are hashed after CRLF -> LF normalization so Windows checkouts verify identically.

`pin` fetches the upstream commit and classifies each vendored file by content: identical
files are matched by hash anywhere in the upstream tree, locally modified files are matched
to their upstream counterpart by path, and everything else is an addition. Libraries
without a fetchable upstream commit are pinned with `verification.method = "baseline"`.

Usage:
    python misc/scripts/egp_thirdparty.py verify [LIB ...]
    python misc/scripts/egp_thirdparty.py pin LIB [--repository URL] [--commit SHA]
           [--version V] [--license L] [--upstream-dir DIR] [--baseline REASON]
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path, PurePosixPath
from typing import Any

SCHEMA = "egp-thirdparty/1"
ROOT = Path(__file__).resolve().parents[2]
THIRDPARTY = ROOT / "thirdparty"
MANIFEST = "UPSTREAM.json"
COMMIT = re.compile(r"[0-9a-f]{40}")
DIGEST = re.compile(r"[0-9a-f]{64}")


def content_digest(data: bytes) -> str:
    """SHA-256 after CRLF -> LF normalization (the convention of every EGP source pin).

    Git only rewrites line endings of text files, so binary content hashes identically on
    every checkout either way.
    """
    return hashlib.sha256(data.replace(b"\r\n", b"\n")).hexdigest()


def tracked_files(library: Path) -> list[str]:
    """Git-tracked files of a library, relative to it, excluding the manifest."""
    output = subprocess.run(
        ["git", "-c", "core.quotepath=off", "ls-files", "-z", "--", "."],
        cwd=library,
        check=True,
        capture_output=True,
    ).stdout.decode("utf-8")
    return sorted(path for path in output.split("\0") if path and path != MANIFEST)


def vendored_libraries() -> list[Path]:
    """Library directories with git-tracked content (untracked leftovers are ignored)."""
    output = subprocess.run(
        ["git", "-c", "core.quotepath=off", "ls-files", "-z", "--", "."],
        cwd=THIRDPARTY,
        check=True,
        capture_output=True,
    ).stdout.decode("utf-8")
    names = {path.split("/", 1)[0] for path in output.split("\0") if "/" in path}
    return [THIRDPARTY / name for name in sorted(names)]


def tree_digests(directory: Path) -> dict[str, str]:
    files = (
        path for path in directory.rglob("*") if path.is_file() and ".git" not in path.relative_to(directory).parts
    )
    return {path.relative_to(directory).as_posix(): content_digest(path.read_bytes()) for path in files}


def fetch_upstream(repository: str, commit: str, destination: Path) -> None:
    """Fetch exactly one upstream commit, without history."""

    def git(*arguments: str) -> None:
        subprocess.run(["git", *arguments], cwd=destination, check=True, capture_output=True)

    destination.mkdir(parents=True, exist_ok=True)
    git("init", "-q")
    git("remote", "add", "origin", repository)
    git("-c", "protocol.version=2", "fetch", "-q", "--depth", "1", "--filter=blob:none", "origin", commit)
    git("-c", "core.autocrlf=false", "checkout", "-q", "FETCH_HEAD")


def counterpart(path: str, upstream_paths: list[str]) -> str | None:
    """The upstream file a modified vendored file came from: same name, longest shared path suffix."""
    parts = PurePosixPath(path).parts
    best, best_score = None, 0
    for candidate in upstream_paths:
        other = PurePosixPath(candidate).parts
        if other[-1] != parts[-1]:
            continue
        score = 0
        while score < min(len(parts), len(other)) and parts[-1 - score] == other[-1 - score]:
            score += 1
        if score > best_score or (score == best_score and best is not None and len(candidate) < len(best)):
            best, best_score = candidate, score
    return best


def classify(
    vendored: dict[str, str], upstream: dict[str, str]
) -> tuple[dict[str, str], dict[str, dict[str, str]], dict[str, str]]:
    by_digest: dict[str, list[str]] = {}
    for path, digest in upstream.items():
        by_digest.setdefault(digest, []).append(path)
    files: dict[str, str] = {}
    patched: dict[str, dict[str, str]] = {}
    additions: dict[str, str] = {}
    upstream_paths = sorted(upstream)
    for path, digest in sorted(vendored.items()):
        if digest in by_digest:
            files[path] = digest
            continue
        origin = counterpart(path, upstream_paths)
        if origin is not None:
            patched[path] = {"upstream_path": origin, "upstream_sha256": upstream[origin], "sha256": digest}
        else:
            additions[path] = digest
    return files, patched, additions


def write_manifest(library: Path, manifest: dict[str, Any]) -> None:
    with open(library / MANIFEST, "w", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(manifest, indent=1, sort_keys=False) + "\n")


def pin(arguments: argparse.Namespace) -> int:
    library = THIRDPARTY / arguments.library
    if not library.is_dir():
        raise SystemExit(f"No such library: {library}")
    vendored = {path: content_digest((library / path).read_bytes()) for path in tracked_files(library)}
    upstream_info = {"repository": arguments.repository, "version": arguments.version, "commit": arguments.commit}
    manifest = {"schema": SCHEMA, "name": arguments.library, "license": arguments.license, "upstream": upstream_info}
    if arguments.baseline:
        manifest["verification"] = {"method": "baseline", "reason": arguments.baseline}
        manifest.update(files={}, patched={}, additions=dict(sorted(vendored.items())))
    else:
        if not arguments.repository or not arguments.commit or not COMMIT.fullmatch(arguments.commit):
            raise SystemExit("pin needs --repository and a full 40-character --commit (or --baseline REASON)")
        scratch = None
        try:
            if arguments.upstream_dir:
                source = Path(arguments.upstream_dir)
            else:
                scratch = Path(tempfile.mkdtemp(prefix=f"egp-upstream-{arguments.library}-"))
                fetch_upstream(arguments.repository, arguments.commit, scratch)
                source = scratch
            files, patched, additions = classify(vendored, tree_digests(source))
        finally:
            if scratch is not None:
                shutil.rmtree(scratch, ignore_errors=True)
        manifest["verification"] = {"method": "upstream-commit"}
        manifest.update(files=files, patched=patched, additions=additions)
    # Re-pinning keeps reviewed provenance that cannot be derived from file contents.
    try:
        previous = json.loads((library / MANIFEST).read_text(encoding="utf-8"))
    except (FileNotFoundError, json.JSONDecodeError):
        previous = {}
    if previous.get("schema") == SCHEMA:
        for key in ("patches", "additions_reason", "excluded_upstream_files"):
            if key in previous:
                manifest[key] = previous[key]
        for path, entry in manifest["patched"].items():
            if "patch" in previous.get("patched", {}).get(path, {}):
                entry["patch"] = previous["patched"][path]["patch"]
    write_manifest(library, manifest)
    print(
        f"{arguments.library}: {manifest['verification']['method']}, {len(manifest['files'])} identical, "
        f"{len(manifest['patched'])} patched, {len(manifest['additions'])} additions"
    )
    return 0


def check_library(library: Path) -> list[str]:
    errors: list[str] = []
    name = library.name
    try:
        manifest = json.loads((library / MANIFEST).read_text(encoding="utf-8"))
    except FileNotFoundError:
        return [f"{name}: missing {MANIFEST}"]
    except json.JSONDecodeError as error:
        return [f"{name}: invalid {MANIFEST}: {error}"]
    if manifest.get("schema") != SCHEMA:
        return [f"{name}: {MANIFEST} schema is {manifest.get('schema')!r}, expected {SCHEMA!r}"]
    if manifest.get("name") != name:
        errors.append(f"{name}: manifest name is {manifest.get('name')!r}")
    method = manifest.get("verification", {}).get("method")
    upstream = manifest.get("upstream", {})
    if method == "upstream-commit":
        if not upstream.get("repository") or not COMMIT.fullmatch(upstream.get("commit") or ""):
            errors.append(f"{name}: upstream-commit verification needs a repository and a 40-character commit")
    elif method == "baseline":
        if not manifest["verification"].get("reason"):
            errors.append(f"{name}: baseline verification needs a reason")
    else:
        errors.append(f"{name}: unknown verification method {method!r}")
    expected: dict[str, str] = {}
    for bucket in ("files", "additions"):
        for path, digest in manifest.get(bucket, {}).items():
            if not isinstance(digest, str) or not DIGEST.fullmatch(digest):
                errors.append(f"{name}: {bucket}[{path}] is not a SHA-256 digest")
            if path in expected:
                errors.append(f"{name}: {path} is listed more than once")
            expected[path] = digest
    for path, entry in manifest.get("patched", {}).items():
        if path in expected:
            errors.append(f"{name}: {path} is listed more than once")
        created = entry.get("upstream_path") is None and entry.get("upstream_sha256") is None and "patch" in entry
        upstream_ok = created or (
            entry.get("upstream_path") and DIGEST.fullmatch(str(entry.get("upstream_sha256", "")))
        )
        if not upstream_ok or not DIGEST.fullmatch(str(entry.get("sha256", ""))):
            errors.append(
                f"{name}: patched[{path}] needs upstream_path, upstream_sha256 and sha256 "
                "(or null upstream fields and a `patch` for files a patch creates)"
            )
        expected[path] = entry.get("sha256", "")
    patch_names = set()
    for patch in manifest.get("patches", []):
        patch_names.add(patch.get("name"))
        patch_file = ROOT / str(patch.get("path", ""))
        if (
            not patch.get("name")
            or not patch_file.is_file()
            or content_digest(patch_file.read_bytes()) != patch.get("sha256")
        ):
            errors.append(
                f"{name}: patch {patch.get('name')!r} ({patch.get('path')}) is missing or differs from its pin"
            )
    for path, entry in manifest.get("patched", {}).items():
        if "patch" in entry and entry["patch"] not in patch_names:
            errors.append(f"{name}: patched[{path}] names unknown patch {entry['patch']!r}")
    for path, entry in manifest.get("excluded_upstream_files", {}).items():
        if (
            path in expected
            or (library / path).exists()
            or not entry.get("reason")
            or not DIGEST.fullmatch(str(entry.get("upstream_sha256", "")))
        ):
            errors.append(f"{name}: invalid or reintroduced upstream exclusion {path}")
    actual = tracked_files(library)
    for path in sorted(set(actual) - set(expected)):
        errors.append(f"{name}: {path} is vendored but not in {MANIFEST}")
    for path in sorted(set(expected) - set(actual)):
        errors.append(f"{name}: {path} is in {MANIFEST} but not vendored")
    for path in sorted(set(actual) & set(expected)):
        digest = content_digest((library / path).read_bytes())
        if digest != expected[path]:
            errors.append(f"{name}: {path} differs from its pin (re-pin after reviewing the change)")
    return errors


def verify(arguments: argparse.Namespace) -> int:
    libraries = [THIRDPARTY / name for name in arguments.libraries] or vendored_libraries()
    errors, counts = [], {"upstream-commit": 0, "baseline": 0}
    for library in libraries:
        library_errors = check_library(library)
        errors.extend(library_errors)
        if not library_errors:
            method = json.loads((library / MANIFEST).read_text(encoding="utf-8"))["verification"]["method"]
            counts[method] += 1
    for error in errors:
        print(f"::error::{error}" if arguments.github else error)
    status = "FAIL" if errors else "PASS"
    print(
        f"EGP_THIRDPARTY_{status} libraries={len(libraries)} upstream_verified={counts['upstream-commit']} "
        f"baseline={counts['baseline']} errors={len(errors)}"
    )
    return 1 if errors else 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    commands = parser.add_subparsers(dest="command", required=True)
    check = commands.add_parser("verify", help="Verify vendored files against their manifests")
    check.add_argument("libraries", nargs="*")
    check.add_argument("--github", action="store_true", help="Emit GitHub Actions error annotations")
    check.set_defaults(handler=verify)
    create = commands.add_parser("pin", help="Create or refresh a library manifest")
    create.add_argument("library")
    create.add_argument("--repository")
    create.add_argument("--commit")
    create.add_argument("--version")
    create.add_argument("--license")
    create.add_argument("--upstream-dir", help="Use an existing upstream checkout instead of fetching")
    create.add_argument("--baseline", metavar="REASON", help="Pin current content without an upstream comparison")
    create.set_defaults(handler=pin)
    arguments = parser.parse_args(argv)
    return int(arguments.handler(arguments))


if __name__ == "__main__":
    sys.exit(main())
