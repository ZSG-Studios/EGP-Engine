"""Match raw source digests or an explicitly pinned LF text digest.

Only UTF-8 text may be normalized under the manifest's explicit LF policy. All
other bytes and the actual executed input digest are retained. The secondary pin
must be derived from independently verified upstream bytes, never from arbitrary
checkout content. This function does not rewrite files or regenerate a manifest.
"""

from __future__ import annotations

import hashlib
import re
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
