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


if __name__ == "__main__":
    unittest.main()
