"""Inventory EGP Git worktrees and optionally preserve dirty source for integration."""

import argparse
import hashlib
import json
import subprocess
import tarfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def git(path, *arguments):
    return subprocess.check_output(["git", "-C", str(path), *arguments], stderr=subprocess.PIPE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--snapshot", action="store_true", help="Save tracked binary patches and untracked source archives"
    )
    parser.add_argument("--output", type=Path, default=ROOT / ".build/integration-takeover")
    args = parser.parse_args()
    output = args.output.resolve() / str(time.time_ns())
    output.mkdir(parents=True)
    inventory = {"worktrees": [], "branches": git(ROOT, "branch", "-avv").decode("utf-8", "replace")}
    raw = git(ROOT, "worktree", "list", "--porcelain").decode("utf-8", "replace")
    for index, block in enumerate(raw.strip().split("\n\n")):
        lines = block.splitlines()
        path = Path(lines[0].removeprefix("worktree "))
        if not path.is_dir():
            inventory["worktrees"].append({"path": str(path), "unavailable": True})
            continue
        status = git(path, "status", "--short").decode("utf-8", "replace")
        item = {
            "path": str(path),
            "head": git(path, "rev-parse", "HEAD").decode().strip(),
            "metadata": lines[1:],
            "status": status,
            "recent_commits": git(path, "log", "-8", "--oneline").decode("utf-8", "replace"),
        }
        if args.snapshot:
            patch = git(path, "diff", "--binary", "HEAD")
            patch_path = output / f"tree-{index}-tracked.patch"
            patch_path.write_bytes(patch)
            item["tracked_patch"] = str(patch_path)
            item["tracked_patch_sha256"] = hashlib.sha256(patch).hexdigest()
            names = git(path, "ls-files", "--others", "--exclude-standard", "-z").decode("utf-8", "replace")
            untracked = [name for name in names.split("\0") if name]
            item["untracked_source"] = untracked
            archive_path = output / f"tree-{index}-untracked.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                for name in untracked:
                    source = path / name
                    if source.is_file() or source.is_symlink():
                        archive.add(source, arcname=name, recursive=False)
            item["untracked_archive"] = str(archive_path)
            item["untracked_archive_sha256"] = hashlib.sha256(archive_path.read_bytes()).hexdigest()
        inventory["worktrees"].append(item)
        print(f"{path}: {item['head'][:12]}, {len(status.splitlines())} changed paths", flush=True)
    (output / "inventory.json").write_text(json.dumps(inventory, indent=2) + "\n", encoding="utf-8")
    print(f"Inventory: {output / 'inventory.json'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
