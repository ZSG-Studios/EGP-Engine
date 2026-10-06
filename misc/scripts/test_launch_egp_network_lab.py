"""Reject incomplete or stale evidence from dedicated-server restart tests."""

import copy
import unittest

from launch_egp_network_lab import restart_failure, stall_failure


class RestartEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.receipt = {
            "clients": 2,
            "listener": {"port": 47000},
            "server_restart": {
                "initial_pid": 10,
                "replacement_pid": 20,
                "replacement_listener": {"port": 47000},
                "checkpoint": {
                    "pid": 10,
                    "generation": 1,
                    "admitted_clients": 2,
                    "input_clients": 2,
                    "root_authority": -1,
                    "tick": 50,
                    "persistent_value": 103,
                },
            },
            "processes": [
                {"role": "server", "pid": 10, "exit_code": 0, "result": {"passed": True}},
                {
                    "role": "server",
                    "pid": 20,
                    "exit_code": 0,
                    "result": {
                        "passed": True,
                        "generation": 2,
                        "input_clients": 2,
                        "admitted_clients": 2,
                        "restored_value": 103,
                        "persistent_value": 106,
                    },
                },
            ],
        }
        for index in range(2):
            self.receipt["processes"].append({
                "role": "client",
                "pid": 30 + index,
                "exit_code": 0,
                "result": {
                    "passed": True,
                    "index": index,
                    "generation": 2,
                    "disconnected": True,
                    "cleared_on_disconnect": True,
                    "epoch_ticks": {"1": 20, "2": 30},
                    "epoch_inputs": {"1": True, "2": True},
                    "epoch_server_pids": {"1": 10, "2": 20},
                    "restored_value": 103,
                    "persistent_value": 106,
                    "connection_states": ["Connected", "Disconnected", "Connected"],
                },
            })

    def test_complete_restart(self):
        self.assertIsNone(restart_failure(self.receipt))

    def test_markers_alone_are_insufficient(self):
        self.receipt["processes"][2]["result"] = {"passed": True, "index": 0}
        self.assertIsNotNone(restart_failure(self.receipt))

    def test_stale_entities_fail(self):
        self.receipt["processes"][2]["result"]["cleared_on_disconnect"] = False
        self.assertIsNotNone(restart_failure(self.receipt))

    def test_input_ack_required_in_both_generations(self):
        for generation in ("1", "2"):
            receipt = copy.deepcopy(self.receipt)
            receipt["processes"][2]["result"]["epoch_inputs"].pop(generation)
            self.assertIsNotNone(restart_failure(receipt))

    def test_old_server_identity_fails(self):
        self.receipt["processes"][2]["result"]["epoch_server_pids"]["2"] = 10
        self.assertIsNotNone(restart_failure(self.receipt))

    def test_wrong_checkpoint_value_fails(self):
        self.receipt["processes"][2]["result"]["restored_value"] = 100
        self.assertIsNotNone(restart_failure(self.receipt))

    def test_server_must_accept_new_owner_inputs(self):
        self.receipt["processes"][1]["result"]["input_clients"] = 1
        self.assertIsNotNone(restart_failure(self.receipt))

    def test_server_checkpoint_does_not_advance_without_inputs(self):
        self.receipt["processes"][1]["result"]["persistent_value"] = 103
        self.assertIsNotNone(restart_failure(self.receipt))

    def test_same_server_process_is_not_a_restart(self):
        self.receipt["server_restart"]["replacement_pid"] = 10
        self.assertIsNotNone(restart_failure(self.receipt))

    def test_duplicate_client_does_not_replace_missing_client(self):
        self.receipt["processes"][3]["result"]["index"] = 0
        self.assertIsNotNone(restart_failure(self.receipt))

    def test_disconnect_after_final_connection_fails(self):
        self.receipt["processes"][2]["result"]["connection_states"] = ["Connected", "Connected", "Disconnected"]
        self.assertIsNotNone(restart_failure(self.receipt))

    def test_endpoint_change_fails(self):
        self.receipt["server_restart"]["replacement_listener"]["port"] = 47001
        self.assertIsNotNone(restart_failure(self.receipt))


class RepeatedStallEvidenceTests(unittest.TestCase):
    def setUp(self):
        proofs = []
        for i in range(2):
            proofs.append({
                "old_generation": i + 1,
                "new_generation": i + 2,
                "old_peer": 101 + i,
                "new_peer": 102 + i,
                "old_entity": 11 + i,
                "new_entity": 12 + i,
                "elapsed_ms": 750,
                "injected_at_ms": 4000 + i * 8000,
                "poll_error": 1,
                "state_after_failure": "Stopped",
                "entities_after_failure": 0,
                "tick_after_failure": 0,
                "input_after_failure": 3,
                "stale_input_enqueued": True,
                "input_acknowledged": True,
                "before_tick": 90 + i * 100,
                "recovered_tick": 100 + i * 100,
                "before_value": 103 + i,
                "recovered_value": 104 + i,
            })
        self.receipt = {
            "clients": 2,
            "client_stall": {"index": 0, "count": 2, "milliseconds": 750, "at_seconds": 4, "interval_seconds": 8},
            "processes": [
                {
                    "role": "server",
                    "pid": 10,
                    "exit_code": 0,
                    "result": {
                        "passed": True,
                        "persistent_value": 105,
                        "peer_generations": {"0": {"101": True, "102": True, "103": True}, "1": {"201": True}},
                        "input_generations": {"0": {"1": True, "2": True, "3": True}, "1": {"1": True}},
                    },
                }
            ],
        }
        for index in range(2):
            self.receipt["processes"].append({
                "role": "client",
                "pid": 30 + index,
                "exit_code": 0,
                "result": {
                    "passed": True,
                    "index": index,
                    "generation": 3 if index == 0 else 1,
                    "replies": 3 if index == 0 else 1,
                    "highest_tick": 300,
                    "persistent_value": 105,
                    "connection_states": ["Connected", "Stopped"] * (2 if index == 0 else 0) + ["Connected", "Stopped"],
                    "diagnostics": ["Fixed simulation exceeded its catch-up budget; resynchronization required."]
                    * (2 if index == 0 else 0),
                    "epoch_inputs": {"1": True, "2": True, "3": True},
                    "epoch_ticks": {"1": 90, "2": 190, "3": 300},
                    "epoch_server_pids": {"1": 10, "2": 10, "3": 10},
                    "stall_proofs": proofs if index == 0 else [],
                    "stall_proof": proofs[-1] if index == 0 else {},
                },
            })

    def test_complete_repeated_recovery(self):
        self.assertIsNone(stall_failure(self.receipt))

    def test_success_markers_cannot_replace_missing_recovery(self):
        self.receipt["processes"][1]["result"]["stall_proofs"].pop()
        self.assertIsNotNone(stall_failure(self.receipt))

    def test_forged_peer_history_must_match_server_admissions(self):
        for proof in self.receipt["processes"][1]["result"]["stall_proofs"]:
            proof["old_peer"] += 1000
            proof["new_peer"] += 1000
        self.assertIsNotNone(stall_failure(self.receipt))

    def test_retired_peer_identity_cannot_be_reused(self):
        self.receipt["processes"][1]["result"]["stall_proofs"][0]["new_peer"] = 101
        self.assertIsNotNone(stall_failure(self.receipt))

    def test_previous_recovery_must_be_next_stall_identity(self):
        self.receipt["processes"][1]["result"]["stall_proofs"][1]["old_entity"] = 99
        self.assertIsNotNone(stall_failure(self.receipt))

    def test_rejected_poll_must_clear_entities(self):
        self.receipt["processes"][1]["result"]["stall_proofs"][0]["entities_after_failure"] = 1
        self.assertIsNotNone(stall_failure(self.receipt))

    def test_intermediate_owner_input_ack_is_required(self):
        self.receipt["processes"][1]["result"]["epoch_inputs"].pop("2")
        self.assertIsNotNone(stall_failure(self.receipt))

    def test_healthy_client_cannot_reconnect(self):
        self.receipt["processes"][2]["result"]["connection_states"].insert(1, "Connected")
        self.assertIsNotNone(stall_failure(self.receipt))

    def test_extra_owner_input_is_rejected_even_if_all_counters_agree(self):
        for process in self.receipt["processes"]:
            process["result"]["persistent_value"] = 106
        self.assertIsNotNone(stall_failure(self.receipt))

    def test_authoritative_server_failure_is_not_client_recovery(self):
        self.receipt["processes"][0]["result"]["passed"] = False
        self.assertIsNotNone(stall_failure(self.receipt))


if __name__ == "__main__":
    unittest.main()
