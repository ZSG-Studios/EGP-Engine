"""Reject incomplete or stale evidence from dedicated-server restart tests."""

import copy
import unittest

from launch_egp_network_lab import restart_failure


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


if __name__ == "__main__":
    unittest.main()
