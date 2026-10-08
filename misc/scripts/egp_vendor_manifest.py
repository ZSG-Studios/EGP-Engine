"""Match raw source digests or an explicitly pinned LF text digest.

Only UTF-8 text may be normalized under the manifest's explicit LF policy. All
other bytes and the actual executed input digest are retained. The secondary pin
must be derived from independently verified upstream bytes, never from arbitrary
checkout content. This function does not rewrite files or regenerate a manifest.
"""

from __future__ import annotations

import hashlib
import re
from pathlib import Path
from typing import Any


def normalization_pins(manifest: dict[str, Any]) -> dict[str, str]:
    policy = manifest.get("checkout_normalization")
    if policy is None:
        return {}
    if not isinstance(policy, dict) or policy.get("policy") != "utf8_lf":
        raise ValueError("Unsupported vendored source normalization policy")
    pins = policy.get("files")
    if (
        not isinstance(pins, dict)
        or set(pins) != set(manifest["files"])
        or any(not isinstance(value, str) or re.fullmatch(r"[0-9a-f]{64}", value) is None for value in pins.values())
    ):
        raise ValueError("Incomplete or invalid normalized source pins")
    return pins


def pinned_digest_match(
    data: bytes, expected: str, *, normalized_lf_expected: str | None = None
) -> dict[str, str] | None:
    raw = hashlib.sha256(data).hexdigest()
    if raw == expected:
        return {"raw_sha256": raw, "manifest_sha256": expected, "comparison": "raw"}
    if normalized_lf_expected is None:
        return None
    # Binary and lone-CR content has no implicit text normalization policy.
    try:
        data.decode("utf-8", errors="strict")
    except UnicodeDecodeError:
        return None
    if b"\0" in data:
        return None
    lf = data.replace(b"\r\n", b"\n")
    if b"\r" in lf:
        return None
    if hashlib.sha256(lf).hexdigest() == normalized_lf_expected:
        return {
            "raw_sha256": raw,
            "manifest_sha256": expected,
            "comparison": "LF-pinned",
            "normalized_lf_sha256": normalized_lf_expected,
        }
    return None


def verify_excluded_files(vendor: Path, manifest: dict[str, Any]) -> None:
    """Retain upstream provenance for intentionally omitted build definitions."""
    active = manifest.get("files", manifest.get("sha256_lf", {}))
    for relative, entry in manifest.get("excluded_upstream_files", {}).items():
        path = (vendor / relative).resolve()
        if (
            not path.is_relative_to(vendor.resolve())
            or relative in active
            or path.exists()
            or not relative.endswith(("CMakeLists.txt", ".cmake", ".cmake.in"))
            or not entry.get("reason")
            or re.fullmatch(r"[0-9a-f]{64}", entry.get("upstream_sha256", "")) is None
        ):
            raise ValueError("Invalid or reintroduced upstream build exclusion: " + relative)
        for key in ("normalized_lf_sha256", "egp_patched_sha256"):
            if key in entry and re.fullmatch(r"[0-9a-f]{64}", entry[key]) is None:
                raise ValueError("Invalid excluded upstream digest: " + relative)
