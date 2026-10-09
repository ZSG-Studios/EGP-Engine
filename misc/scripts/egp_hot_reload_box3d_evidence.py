#!/usr/bin/env python3
"""Box3D hot-reload evidence: one live EGPBox3DWorld across C# and C++ reloads.

The running game owns an EGPBox3DWorld that the C# probe steps once per physics
frame and the C++ extension node reads. Each sample reports the world's identity,
tick and state hash, both language views of it, an uninterrupted reference world
advanced to the same tick, and whether the previous sample's EG3SNAP3 snapshot,
restored and stepped forward, reproduces the live state.
"""

FIELDS = (
    "world_id", "tick", "hash", "reference_hash", "position_y", "steps", "failures",
    "references_ok", "cpp_state_ok", "cs_state_ok", "replay_ok", "replayed_from",
)


def box3d_failure(state, previous=None):
    """Return the first violated Box3D reload invariant, or None."""
    box = state.get("box3d")
    if not isinstance(box, dict) or any(key not in box for key in FIELDS):
        return "Box3D evidence missing"
    if not (box["references_ok"] and box["cpp_state_ok"] and box["cs_state_ok"]):
        return "C# or C++ lost or diverged from the live Box3D world"
    if box["failures"] != 0:
        return "Box3D step rejected (duplicated or out-of-order owner step)"
    if box["steps"] != box["tick"]:
        return "C# owner step count diverged from the world tick"
    if box["hash"] != box["reference_hash"]:
        return "Reload changed deterministic Box3D state"
    if not box["replay_ok"]:
        return "Previous snapshot did not reproduce the live Box3D state"
    if previous is None:
        return None
    before = previous.get("box3d")
    if not isinstance(before, dict):
        return "Previous Box3D evidence missing"
    if box["world_id"] != before["world_id"]:
        return "Live Box3D world identity changed"
    if box["replayed_from"] != before["tick"]:
        return "Replay did not start from the previous sample's snapshot"
    if box["tick"] <= before["tick"] or box["position_y"] >= before["position_y"]:
        return "Box3D world did not advance after reload"
    return None
