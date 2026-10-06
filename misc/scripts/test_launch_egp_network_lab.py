"""Reject incomplete or stale evidence from dedicated-server restart tests."""

import copy
import unittest

from launch_egp_network_lab import restart_failure, server_stall_failure, stall_failure


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


class ServerStallEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.receipt = {
            "clients": 2,
            "listener": {"port": 47000},
            "server_stall": {"milliseconds": 750, "at_seconds": 4},
            "processes": [
                {
                    "role": "server",
                    "pid": 10,
                    "exit_code": 0,
                    "result": {
                        "passed": True,
                        "generation": 2,
                        "admitted_clients": 2,
                        "input_clients": 2,
                        "restored_value": 103,
                        "persistent_value": 106,
                        "diagnostics": ["Fixed simulation exceeded its catch-up budget; resynchronization required."],
                        "connection_states": ["Listening", "Stopped", "Listening", "Stopped"],
                        "input_generations": {"0": {"1": True, "2": True}, "1": {"1": True, "2": True}},
                        "peer_generations": {"0": {"257": True, "769": True}, "1": {"514": True, "1026": True}},
                        "server_stall_proof": {
                            "checkpoint": {
                                "pid": 10,
                                "generation": 1,
                                "admitted_clients": 2,
                                "input_clients": 2,
                                "root_authority": -1,
                                "tick": 240,
                                "persistent_value": 103,
                            },
                            "elapsed_ms": 750,
                            "injected_at_ms": 4000,
                            "poll_error": 1,
                            "state_after_failure": "Stopped",
                            "entities_after_failure": 0,
                            "peers_after_failure": 0,
                            "tick_after_failure": 0,
                            "spawn_after_failure": 0,
                            "update_after_failure": 4,
                            "port": 47000,
                            "recovered_port": 47000,
                            "old_root": 1,
                            "new_root": 4,
                            "old_owners": {"0": 2, "1": 3},
                            "old_peers": {"0": {"257": True}, "1": {"514": True}},
                            "retired_admission": {
                                "states": ["Connecting", "Stopped", "Disconnected"],
                                "entities": 0,
                                "peers": 0,
                            },
                        },
                    },
                }
            ],
        }
        self.server = self.receipt["processes"][0]["result"]
        self.proof = self.server["server_stall_proof"]
        for index in range(2):
            self.receipt["processes"].append({
                "role": "client",
                "pid": 30 + index,
                "exit_code": 0,
                "result": {
                    "passed": True,
                    "index": index,
                    "diagnostics": [],
                    "generation": 2,
                    "replies": 2,
                    "disconnected": True,
                    "cleared_on_disconnect": True,
                    "epoch_ticks": {"1": 230, "2": 30},
                    "epoch_inputs": {"1": True, "2": True},
                    "epoch_server_pids": {"1": 10, "2": 10},
                    "restored_value": 103,
                    "persistent_value": 106,
                    "connection_states": ["Connected", "Stopped", "Disconnected", "Stopped", "Connected", "Stopped"],
                    "epoch_owners": {
                        "1": {"peer": 257 * (index + 1), "entity": index + 2},
                        "2": {"peer": 769 + index * 257, "entity": index + 5, "stale_input_enqueued": True},
                    },
                },
            })
        self.client = self.receipt["processes"][1]["result"]

    def test_complete_same_process_recovery(self):
        self.assertIsNone(server_stall_failure(self.receipt))

    def test_listen_host_uses_the_same_contract(self):
        self.receipt["processes"][0]["role"] = "host"
        self.assertIsNone(server_stall_failure(self.receipt))

    def test_short_gap_is_not_the_requested_fault(self):
        self.proof["elapsed_ms"] = 500
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_fault_injected_before_requested_time_fails(self):
        self.proof["injected_at_ms"] = 3999
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_failure_must_reject_work_and_clear_all_state(self):
        for field, wrong in (
            ("poll_error", 0),
            ("entities_after_failure", 1),
            ("peers_after_failure", 1),
            ("tick_after_failure", 1),
            ("spawn_after_failure", 7),
            ("update_after_failure", 0),
        ):
            receipt = copy.deepcopy(self.receipt)
            receipt["processes"][0]["result"]["server_stall_proof"][field] = wrong
            with self.subTest(field=field):
                self.assertIsNotNone(server_stall_failure(receipt))

    def test_listener_cannot_skip_stopped_state(self):
        self.server["connection_states"] = ["Listening", "Listening", "Stopped"]
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_retired_token_cannot_reach_connected(self):
        self.proof["retired_admission"]["states"].insert(1, "Connected")
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_retired_token_must_finish_rejection(self):
        self.proof["retired_admission"]["states"].pop()
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_replacement_server_pid_is_not_same_process_recovery(self):
        self.client["epoch_server_pids"]["2"] = 11
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_listener_port_must_be_preserved(self):
        self.proof["recovered_port"] = 47001
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_client_must_clear_disconnected_entities(self):
        self.client["cleared_on_disconnect"] = False
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_new_owner_cannot_reuse_retired_entity(self):
        self.client["epoch_owners"]["2"]["entity"] = self.client["epoch_owners"]["1"]["entity"]
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_new_root_cannot_reuse_retired_owned_entity(self):
        self.proof["new_root"] = 2
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_server_admissions_must_match_client_peer_history(self):
        self.server["peer_generations"]["0"] = {"257": True, "770": True}
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_retired_entity_input_is_exercised(self):
        self.client["epoch_owners"]["2"]["stale_input_enqueued"] = False
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_ack_required_before_and_after_outage(self):
        for epoch in ("1", "2"):
            receipt = copy.deepcopy(self.receipt)
            receipt["processes"][1]["result"]["epoch_inputs"].pop(epoch)
            self.assertIsNotNone(server_stall_failure(receipt))

    def test_extra_owner_input_fails_even_if_counters_match(self):
        for record in self.receipt["processes"]:
            record["result"]["persistent_value"] = 107
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_foreign_checkpoint_is_rejected(self):
        self.proof["checkpoint"]["pid"] = 11
        self.assertIsNotNone(server_stall_failure(self.receipt))

    def test_connection_history_must_span_disconnect(self):
        self.client["connection_states"] = ["Connected", "Connected", "Disconnected", "Stopped"]
        self.assertIsNotNone(server_stall_failure(self.receipt))


if __name__ == "__main__":
    unittest.main()
