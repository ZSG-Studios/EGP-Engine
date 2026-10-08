"""Resolve Godot's Windows console launcher to the process running the engine."""

import hashlib
import os
import re
from pathlib import Path


def resolve_engine_process(requested, platform=None):
    requested = Path(requested).resolve()
    if not requested.is_file():
        raise FileNotFoundError(f"Engine executable does not exist: {requested}")
    effective = requested
    if (platform or os.name) == "nt" and re.fullmatch(r"godot\.windows\..+\.console\.exe", requested.name, re.I):
        effective = requested.with_name(requested.name[: -len(".console.exe")] + ".exe")
        if not effective.is_file():
            raise FileNotFoundError(f"Windows console launcher requires its direct engine sibling: {effective}")
    return effective, {
        "requested": str(requested),
        "requested_sha256": hashlib.sha256(requested.read_bytes()).hexdigest(),
        "effective": str(effective),
        "effective_sha256": hashlib.sha256(effective.read_bytes()).hexdigest(),
        "console_launcher_resolved": effective != requested,
    }
