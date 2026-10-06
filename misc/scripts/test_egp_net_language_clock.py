import copy
import unittest

from validate_egp_net_languages import clock_recovery_failure


class ClockEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.result = {"clock_recovery": {}}
        for language, body in (("csharp", 10000), ("cpp", 20000)):
            high = [
                {
                    "cycle": n,
                    "old_entity": n,
                    "new_entity": n + 1,
                    "poll_error": 1,
                    "gap_ms": 550,
                    "checkpoint_tick": 8 * n,
                    "final_network_tick": 8,
                    "final_physics_tick": 8 * (n + 1),
                    "checkpoint_hash": "0123456789abcdef",
                    "client_id": 777 if language == "csharp" else 888,
                    "client_live_polls": 30,
                    "client_same_session": True,
                    "fresh_token": True,
                    "client_reset_cleared": True,
                    "client_retired_absent": True,
                    "client_physics_tick": 8 * n + 1,
                    "owner_input_count": n,
                    "invalid_input_count": 0,
                    "interest_roundtrip": True,
                }
                for n in range(1, 4)
            ]
            self.result["clock_recovery"][language] = {
                "passed": True,
                "same_session": True,
                "body_id": body,
                "cycles": high,
                "low_cycles": copy.deepcopy(high),
                "diagnostics": ["Fixed simulation exceeded its catch-up budget; resynchronization required."] * 3,
                "low_diagnostics": ["Fixed simulation exceeded its catch-up budget; resynchronization required."] * 3,
                "client_states": ["Stopped"] + ["Connecting", "Synchronizing", "Connected", "Stopped"] * 4,
                "client_latency_ms": 20,
                "client_jitter_ms": 5,
            }

    def test_complete_repeated_failure(self):
        self.assertIsNone(clock_recovery_failure(self.result))

    def test_missing_language(self):
        self.result["clock_recovery"].pop("cpp")
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_fewer_failures(self):
        self.result["clock_recovery"]["cpp"]["low_cycles"].pop()
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_retired_handle_reused(self):
        self.result["clock_recovery"]["csharp"]["cycles"][1]["new_entity"] = 2
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_forgotten_session(self):
        self.result["clock_recovery"]["cpp"]["same_session"] = False
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_physics_clock_restarted(self):
        self.result["clock_recovery"]["csharp"]["cycles"][1]["final_physics_tick"] = 8
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_missing_native_failure(self):
        self.result["clock_recovery"]["cpp"]["low_cycles"][0]["poll_error"] = 0
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_wrong_diagnostic(self):
        self.result["clock_recovery"]["csharp"]["low_diagnostics"] = []
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_client_was_paused_with_server(self):
        self.result["clock_recovery"]["cpp"]["cycles"][0]["client_live_polls"] = 0
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_client_baseline_was_not_cleared(self):
        self.result["clock_recovery"]["csharp"]["cycles"][1]["client_reset_cleared"] = False
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_retired_token_was_reused(self):
        self.result["clock_recovery"]["cpp"]["cycles"][0]["fresh_token"] = False
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_missing_reconnect_history(self):
        self.result["clock_recovery"]["csharp"]["client_states"].pop()
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_connected_before_baseline_synchronization(self):
        self.result["clock_recovery"]["cpp"]["client_states"].remove("Synchronizing")
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_client_replicated_old_physics_time(self):
        self.result["clock_recovery"]["cpp"]["cycles"][1]["client_physics_tick"] = 16
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_retired_input_was_accepted(self):
        self.result["clock_recovery"]["csharp"]["cycles"][1]["invalid_input_count"] = 1
        self.assertIsNotNone(clock_recovery_failure(self.result))

    def test_interest_did_not_restore(self):
        self.result["clock_recovery"]["cpp"]["cycles"][2]["interest_roundtrip"] = False
        self.assertIsNotNone(clock_recovery_failure(self.result))


if __name__ == "__main__":
    unittest.main()
