#!/usr/bin/env python3
"""Reject missing, duplicated or replaced public physics adapter reload evidence."""

import copy
import unittest

from egp_hot_reload_box3d_evidence import box3d_failure
from egp_hot_reload_box3d_lifecycle import box3d_lifecycle_failure
from test_egp_hot_reload_node import node_evidence


def box_evidence(live=True):
    rows = node_evidence(live)
    for index, row in enumerate(rows):
        tick = 10 + index * 10 if live else [10, 10, 10, 20][index]
        restores = [0, 0, 0, 1, 1, 2][index] if live else [0, 0, 1, 1][index]
        row["box3d"] = {
            "adapter_id": "21",
            "world_id": "22",
            "attached": True,
            "references_ok": True,
            "cpp_state_ok": True,
            "cs_state_ok": True,
            "capsule_empty": True,
            "checks": 22,
            "failures": 0,
            "handoffs": restores,
            "restores": restores,
            "connections": dict.fromkeys(("before_step", "after_step", "failed"), 1),
            "clock_offset": tick - row["server_tick"],
            "adapter_connections": 1,
            "clock_connections": 2,
            "tick": tick,
            "before": tick,
            "after": tick,
            "body_id": 10000,
            "body_count": 1,
            "hash": str(tick),
            "entity": row["entity"],
            "mapping": {str(row["entity"]): 10000},
            "client_tick": tick - 1,
            "position_y": 10000 - tick,
            "velocity_y": -tick,
            "client_position_y": 10001 - tick,
        }
    return rows


class BoxEvidenceTests(unittest.TestCase):
    def test_live_valid(self):
        self.assertIsNone(box3d_failure(box_evidence(), True))

    def test_stopped_valid(self):
        self.assertIsNone(box3d_failure(box_evidence(False), False))

    def test_missing_checkpoint(self):
        self.assertIsNotNone(box3d_failure(box_evidence()[:-1], True))

    def test_stopped_mutation(self):
        rows = box_evidence(False)
        rows[2]["box3d"]["hash"] = "changed"
        self.assertIsNotNone(box3d_failure(rows, False))


def tree_evidence():
    baseline = box_evidence()[-1]
    lifecycle = [copy.deepcopy(baseline) for _ in range(2)]
    lifecycle[0]["box3d"]["clock_connections"] = 1
    reentry = []
    for index in range(3):
        row = copy.deepcopy(baseline)
        row["entity"] += index + 1
        state = row["box3d"]
        state.update(
            entity=row["entity"],
            mapping={str(row["entity"]): 10000},
            tick=70 + index * 10,
            before=70 + index * 10,
            after=70 + index * 10,
            client_tick=69 + index * 10,
            position_y=9930 - index * 10,
            clock_offset=70 + index * 10 - row["server_tick"],
        )
        reentry.append(row)
    return lifecycle, reentry, baseline


class BoxTreeTests(unittest.TestCase):
    def test_valid(self):
        self.assertIsNone(box3d_lifecycle_failure(*tree_evidence()))

    def test_missing(self):
        tree, reentry, baseline = tree_evidence()
        self.assertIsNotNone(box3d_lifecycle_failure(tree, reentry[:2], baseline))


def tree_rejection(index, key, value):
    def test(self):
        tree, reentry, baseline = tree_evidence()
        (tree + reentry)[index]["box3d"][key] = value
        self.assertIsNotNone(box3d_lifecycle_failure(tree, reentry, baseline))

    return test


for index in range(5):
    for key, value in {
        "adapter_id": "foreign",
        "world_id": "foreign",
        "body_id": 0,
        "body_count": 0,
        "handoffs": -1,
        "restores": -1,
        "checks": 0,
        "attached": False,
        "references_ok": False,
        "cpp_state_ok": False,
        "cs_state_ok": False,
        "capsule_empty": False,
        "failures": 1,
        "connections": {},
        "adapter_connections": 0,
        "clock_connections": 0,
        "before": -1,
        "after": -1,
    }.items():
        setattr(BoxTreeTests, f"test_reject_{index}_{key}", tree_rejection(index, key, value))
    if index < 2:
        for key, value in {"tick": 0, "hash": "changed", "position_y": 0, "velocity_y": 0, "mapping": {}}.items():
            setattr(BoxTreeTests, f"test_preserved_{index}_{key}", tree_rejection(index, key, value))
    else:
        for key, value in {
            "entity": 0,
            "mapping": {},
            "clock_offset": -1,
            "position_y": 10000,
            "client_tick": 0,
        }.items():
            setattr(BoxTreeTests, f"test_progress_{index}_{key}", tree_rejection(index, key, value))


def rejection(index, key, value, live):
    def test(self):
        rows = box_evidence(live)
        rows[index]["box3d"][key] = value
        self.assertIsNotNone(box3d_failure(rows, live))

    return test


invalid = {
    "adapter_id": "foreign",
    "world_id": "foreign",
    "attached": False,
    "references_ok": False,
    "cpp_state_ok": False,
    "cs_state_ok": False,
    "capsule_empty": False,
    "checks": 21,
    "failures": 1,
    "handoffs": -1,
    "restores": -1,
    "connections": {"before_step": 2, "after_step": 1, "failed": 1},
    "adapter_connections": 2,
    "clock_connections": 1,
    "tick": 0,
    "before": -1,
    "after": -1,
    "body_id": 0,
    "body_count": 0,
    "hash": "",
}
for live in (True, False):
    for index in range(6 if live else 4):
        # Identity drift requires a later checkpoint to compare against the first.
        for key, value in invalid.items():
            if index == 0 and key in ("adapter_id", "world_id"):
                value = ""
            setattr(
                BoxEvidenceTests, f"test_reject_{live}_{index}_{key}", rejection(index, key, copy.deepcopy(value), live)
            )
        if live or index in (0, 3):
            for key, value in {
                "entity": 0,
                "mapping": {},
                "client_tick": 0,
                "position_y": 10000,
                "velocity_y": 0,
                "client_position_y": 10000,
            }.items():
                setattr(BoxEvidenceTests, f"test_active_{live}_{index}_{key}", rejection(index, key, value, live))


if __name__ == "__main__":
    unittest.main()
