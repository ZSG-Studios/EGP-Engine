"""Reject contradictory/missing ownership evidence, including signed RefCounted IDs."""

import copy
import unittest

from validate_egp_cpp_ownership import diagnostic_failure, evidence_failure


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

    def recovery_proof(self):
        proof = self.proof()
        proof["native_recovery"] = True
        proof["faults"] = []
        for transfer in proof["transfers"]:
            version = transfer["version"]
            transfer.update(fault_elapsed_ms=20, parent_name=f"RecoveredOwnership{version}invalid")
            for kind in ("missing", "invalid"):
                proof["faults"].append(
                    dict(
                        kind=kind,
                        version=version,
                        status=1,
                        node_id=transfer["node_id"],
                        parent_id=90,
                        name=f"RecoveredOwnership{version}{kind}",
                        children=2,
                        methods_unavailable=True,
                        library_closed=True,
                    )
                )
        return proof

    def test_failed_library_boundaries_and_mode(self):
        proof = self.recovery_proof()
        self.assertIsNone(evidence_failure(proof, True))
        self.assertIsNotNone(evidence_failure(proof))
        self.assertIsNotNone(evidence_failure(self.proof(), True))
        for index, row in enumerate(proof["faults"]):
            for key in row:
                altered = self.recovery_proof()
                del altered["faults"][index][key]
                with self.subTest(index=index, key=key):
                    self.assertIsNotNone(evidence_failure(altered, True))
            for key, value in (
                ("status", 0),
                ("status", True),
                ("children", 0),
                ("node_id", 92),
                ("parent_id", 0),
                ("methods_unavailable", 1),
                ("library_closed", False),
                ("name", "lost"),
            ):
                altered = self.recovery_proof()
                altered["faults"][index][key] = value
                self.assertIsNotNone(evidence_failure(altered, True))

    def test_long_fault_or_lost_parent_edit_rejected(self):
        for index in (0, 1):
            for value in (None, True, -1, 500, "20"):
                proof = self.recovery_proof()
                proof["transfers"][index]["fault_elapsed_ms"] = value
                self.assertIsNotNone(evidence_failure(proof, True))
            proof = self.recovery_proof()
            del proof["transfers"][index]["parent_name"]
            self.assertIsNotNone(evidence_failure(proof, True))
        proof = self.recovery_proof()
        proof["faults"].reverse()
        self.assertIsNotNone(evidence_failure(proof, True))

    def diagnostics(self):
        return "\n".join(
            [
                'ERROR: Condition "!FileAccess::exists(path)" is true. Returning: ERR_FILE_NOT_FOUND',
                "ERROR: GDExtension dynamic library not found: 'res://ownership.gdextension'.",
                "ERROR: Can't open GDExtension dynamic library: 'res://ownership.gdextension'.",
                "ERROR: Can't open dynamic library: C:/fixture/bin/ownership-invalid.dll. Error: Bad image.",
            ]
            * 2
        )

    def test_expected_errors_do_not_hide_unrelated_failures(self):
        text = self.diagnostics()
        self.assertIsNone(diagnostic_failure(text, True))
        self.assertIsNone(diagnostic_failure("clean"))
        for altered in (
            text + "\nERROR: other failure",
            text + "\nSCRIPT ERROR: invalid",
            text + "\nEGP_CPP_OWNERSHIP_FAILED lost",
            text.replace("ownership-invalid.dll", "other.dll"),
            text.replace("not found", "wrong"),
            text.splitlines()[0],
        ):
            self.assertIsNotNone(diagnostic_failure(altered, True))
        self.assertIsNotNone(diagnostic_failure(text))


if __name__ == "__main__":
    unittest.main()
