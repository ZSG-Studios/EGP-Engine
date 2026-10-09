#!/usr/bin/env python3
"""Reject missing, duplicated, diverged or replaced Box3D hot-reload evidence."""

import copy
import unittest

from egp_hot_reload_box3d_evidence import box3d_failure


def sample(tick, previous_tick=-1):
    return {"box3d": {
        "world_id": "41", "tick": tick, "hash": "h%d" % tick, "reference_hash": "h%d" % tick,
        "position_y": 10000.0 - tick, "steps": tick, "failures": 0,
        "references_ok": True, "cpp_state_ok": True, "cs_state_ok": True,
        "replay_ok": True, "replayed_from": previous_tick,
    }}


class Box3DEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.first = sample(30)
        self.second = sample(90, 30)

    def mutated(self, **changes):
        state = copy.deepcopy(self.second)
        state["box3d"].update(changes)
        return state

    def test_valid_sequence(self):
        self.assertIsNone(box3d_failure(self.first))
        self.assertIsNone(box3d_failure(self.second, self.first))

    def test_missing_evidence(self):
        self.assertIsNotNone(box3d_failure({}))
        state = copy.deepcopy(self.second)
        del state["box3d"]["replay_ok"]
        self.assertIsNotNone(box3d_failure(state, self.first))
        self.assertIsNotNone(box3d_failure(self.second, {}))

    def test_diverged_state(self):
        self.assertIsNotNone(box3d_failure(self.mutated(hash="changed"), self.first))
        self.assertIsNotNone(box3d_failure(self.mutated(replay_ok=False), self.first))

    def test_duplicated_or_missing_steps(self):
        self.assertIsNotNone(box3d_failure(self.mutated(failures=1), self.first))
        self.assertIsNotNone(box3d_failure(self.mutated(steps=91), self.first))

    def test_lost_language_view(self):
        for key in ("references_ok", "cpp_state_ok", "cs_state_ok"):
            self.assertIsNotNone(box3d_failure(self.mutated(**{key: False}), self.first))

    def test_replaced_or_stalled_world(self):
        self.assertIsNotNone(box3d_failure(self.mutated(world_id="99"), self.first))
        self.assertIsNotNone(box3d_failure(self.mutated(tick=30, steps=30, hash="h30", reference_hash="h30"), self.first))
        self.assertIsNotNone(box3d_failure(self.mutated(position_y=10000.0), self.first))
        self.assertIsNotNone(box3d_failure(self.mutated(replayed_from=0), self.first))


if __name__ == "__main__":
    unittest.main()
