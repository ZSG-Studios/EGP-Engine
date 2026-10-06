import unittest

from validate_egp_net_clock_process import evidence_failure


class ProcessClockEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.server = {
            "passed": True,
            "role": "server",
            "pid": 11,
            "epochs": [],
            "recoveries": [],
            "diagnostics": ["Fixed simulation exceeded its catch-up budget; resynchronization required."] * 3,
        }
        self.client = {
            "passed": True,
            "role": "client",
            "language": "cpp",
            "pid": 22,
            "epochs": [],
            "disconnects": [],
            "states": [],
            "poll_utc_ms": list(range(1000, 5000, 20)),
        }
        for n in range(1, 5):
            self.server["epochs"].append({
                "epoch": n,
                "entity": n,
                "peer_id": n,
                "client_id": 9876,
                "inputs": n,
                "client_physics_tick": 8 * n,
                "world_tick": 8 * n + 2,
            })
            self.client["epochs"].append({
                "epoch": n,
                "entity": n,
                "physics_tick": 8 * n,
                "same_session": True,
                "fresh_token": True,
                "retired_absent": True,
            })
            self.client["states"] += ["Connecting", "Synchronizing", "Connected", "Stopped"]
            if n < 4:
                self.client["states"] += ["Disconnected", "Stopped"]
                self.server["recoveries"].append({
                    "cycle": n,
                    "old_entity": n,
                    "checkpoint_tick": 8 * n + 4,
                    "checkpoint_hash": "0123456789abcdef",
                    "start_utc_ms": 500 + n * 1000,
                    "end_utc_ms": 1050 + n * 1000,
                    "gap_ms": 550,
                    "poll_error": 1,
                    "cleared": True,
                    "same_session": True,
                })
                self.client["disconnects"].append({"epoch": n, "entity": n, "utc_ms": 1080 + n * 1000, "cleared": True})

    def failure(self):
        return evidence_failure(self.server, self.client, "cpp", [11, 22])

    def test_complete_independent_recovery(self):
        self.assertIsNone(self.failure())

    def test_shared_process_cannot_prove_independence(self):
        self.client["pid"] = 11
        self.assertIsNotNone(self.failure())

    def test_missing_fault(self):
        self.server["recoveries"].pop()
        self.assertIsNotNone(self.failure())

    def test_client_was_paused_with_authority(self):
        self.client["poll_utc_ms"] = [t for t in self.client["poll_utc_ms"] if not 1500 <= t <= 2050]
        self.assertIsNotNone(self.failure())

    def test_client_only_polled_once_during_stall(self):
        self.client["poll_utc_ms"] = [t for t in self.client["poll_utc_ms"] if not 1500 <= t <= 2050 or t == 1500]
        self.assertIsNotNone(self.failure())

    def test_utc_jump_cannot_replace_monotonic_gap(self):
        self.server["recoveries"][0]["end_utc_ms"] += 1000
        self.assertIsNotNone(self.failure())

    def test_stale_physics_baseline(self):
        self.client["epochs"][1]["physics_tick"] = 8
        self.assertIsNotNone(self.failure())

    def test_stale_entity_handle(self):
        self.client["epochs"][1]["entity"] = 1
        self.assertIsNotNone(self.failure())

    def test_missing_native_disconnect(self):
        self.client["states"].remove("Disconnected")
        self.assertIsNotNone(self.failure())

    def test_uncleared_disconnected_baseline(self):
        self.client["disconnects"][0]["cleared"] = False
        self.assertIsNotNone(self.failure())

    def test_missing_owned_input(self):
        self.server["epochs"][1]["inputs"] = 1
        self.assertIsNotNone(self.failure())

    def test_reused_admission(self):
        self.client["epochs"][1]["fresh_token"] = False
        self.assertIsNotNone(self.failure())


if __name__ == "__main__":
    unittest.main()
