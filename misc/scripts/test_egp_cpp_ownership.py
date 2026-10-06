"""Reject contradictory/missing ownership evidence, including signed RefCounted IDs."""

import copy
import unittest

from validate_egp_cpp_ownership import evidence_failure


class CppOwnershipEvidenceTests(unittest.TestCase):
    def proof(self):
        phases = []
        for version in (1, 2, 3):
            tick = 10 + version * 8
            phase = dict(
                version=version,
                world_tick=tick,
                world_id=-123,
                world_hash="1234567890abcdef",
                entity=1,
                before=tick,
                after=tick,
                messages=version,
                capsules=0,
                before_connections=1,
                clock_connections=1,
                server_state="Listening",
                client_state="Connected",
                body_y=10000.0 - version,
                body_map={"1": 10000},
                client_entity={"entity": 1, "state": {"physics_tick": tick - 1}},
                handoffs=version - 1,
                restores=version - 1,
                checks=30 * (version - 1),
            )
            for index, name in enumerate(("server", "client", "adapter", "server_session", "client_session"), 1):
                identity = index if index <= 2 else -index
                phase[name + "_id"] = identity
                phase[name + "_live_id"] = identity
            phases.append(phase)
        transfers = [
            dict(
                version=v,
                node_id=91,
                restored_node_id=91,
                tick=10 + (v - 1) * 8,
                restored_tick=10 + (v - 1) * 8,
                hash="1234567890abcdef",
                restored_hash="1234567890abcdef",
            )
            for v in (2, 3)
        ]
        return dict(passed=True, assertions=100, phases=phases, transfers=transfers)

    def test_signed_reference_ids_and_complete_success(self):
        self.assertIsNone(evidence_failure(self.proof()))

    def test_missing_phases_and_transfer_state_rejected(self):
        for invalid in (None, {}, {"passed": 1}, {"passed": True}, {"passed": True, "phases": []}):
            self.assertIsNotNone(evidence_failure(invalid))
        for index in range(2):
            for key in ("version", "node_id", "restored_node_id", "tick", "restored_tick", "hash", "restored_hash"):
                for value in (None, False, "wrong", 0):
                    proof = self.proof()
                    proof["transfers"][index][key] = value
                    with self.subTest(index=index, key=key, value=value):
                        self.assertIsNotNone(evidence_failure(proof))

    def test_every_required_phase_field_and_numeric_type(self):
        for index, phase in enumerate(self.proof()["phases"]):
            for key in phase:
                proof = self.proof()
                del proof["phases"][index][key]
                if index == 0 and key in ("handoffs", "restores", "checks"):
                    continue
                with self.subTest(index=index, key=key):
                    self.assertIsNotNone(evidence_failure(proof))
            for key in ("before", "version", "messages", "world_tick", "server_id", "body_y"):
                proof = self.proof()
                proof["phases"][index][key] = True
                self.assertIsNotNone(evidence_failure(proof))

    def test_replaced_ids_duplicates_and_missing_physics_rejected(self):
        mutations = [
            ("before_connections", 2),
            ("clock_connections", 0),
            ("capsules", 1),
            ("failures", 1),
            ("server_state", "Stopped"),
            ("client_state", "Connecting"),
            ("world_id", -456),
            ("entity", 2),
            ("body_y", float("nan")),
            ("body_y", float("inf")),
            ("body_map", {"1": 1}),
            ("client_entity", {"entity": 1, "state": {"physics_tick": True}}),
            ("server_live_id", 77),
            ("restores", 0),
            ("checks", 29),
            ("unexpected_tick_callback", True),
        ]
        for key, value in mutations:
            proof = copy.deepcopy(self.proof())
            proof["phases"][1][key] = value
            with self.subTest(key=key, value=value):
                self.assertIsNotNone(evidence_failure(proof))


if __name__ == "__main__":
    unittest.main()
