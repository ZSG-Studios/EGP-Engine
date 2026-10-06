#!/usr/bin/env python3
"""Reject misleading lifecycle evidence, especially restart-timestamp masking."""

import unittest

from validate_egp_net_admission import CLOCK_DIAGNOSTIC, evidence_failure


class AdmissionEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.case = ("native", "zero", "same", "clock")
        self.result = {
            "passed": True,
            "api": "native",
            "key": "zero",
            "boundary": "same",
            "fault": "clock",
            "token_created_utc": 100,
            "token_expires_utc": 220,
            "restart_before_utc": 100,
            "restart_after_utc": 100,
            "token_lifetime_seconds": 120,
            "gap_ms": 550,
            "poll_error": 1,
            "diagnostics": [CLOCK_DIAGNOSTIC],
            "peers_before_join": 0,
            "entities_before_join": 0,
            "peers_before_close": 1,
            "expected_connected": True,
            "final_state": "Connected",
            "states": ["Connecting", "Synchronizing", "Connected"],
        }

    def test_retained_zero_same_second_admits(self):
        self.assertIsNone(evidence_failure(self.result, self.case))

    def test_key_value_cannot_change_same_second_contract(self):
        self.result["key"] = "nonzero"
        self.assertIsNone(evidence_failure(self.result, ("native", "nonzero", "same", "clock")))

    def test_cross_second_rejection_is_not_same_second_evidence(self):
        self.result["restart_before_utc"] = self.result["restart_after_utc"] = 101
        self.assertIsNotNone(evidence_failure(self.result, self.case))

    def test_second_boundary_during_listen_is_ambiguous(self):
        self.result["restart_after_utc"] = 101
        self.assertIsNotNone(evidence_failure(self.result, self.case))

    def test_generated_key_must_reject_even_same_second(self):
        self.result["key"] = "generated"
        self.assertIsNotNone(evidence_failure(self.result, ("native", "generated", "same", "clock")))

    def test_valid_cross_second_rejection(self):
        self.result.update(
            boundary="cross",
            restart_before_utc=101,
            restart_after_utc=101,
            gap_ms=1100,
            expected_connected=False,
            peers_before_close=0,
            final_state="Disconnected",
            states=["Connecting", "Stopped", "Disconnected"],
        )
        self.assertIsNone(evidence_failure(self.result, ("native", "zero", "cross", "clock")))

    def test_short_lifetime_cannot_mask_key_evidence(self):
        self.result["token_expires_utc"] = 219
        self.assertIsNotNone(evidence_failure(self.result, self.case))

    def test_missing_public_timestamp_rejected(self):
        self.result.pop("token_created_utc")
        self.assertIsNotNone(evidence_failure(self.result, self.case))

    def test_boolean_timestamp_is_not_time(self):
        self.result["token_created_utc"] = True
        self.assertIsNotNone(evidence_failure(self.result, self.case))

    def test_occupied_slot_cannot_prove_admission_rejection(self):
        self.result["peers_before_join"] = 1
        self.assertIsNotNone(evidence_failure(self.result, self.case))

    def test_uncleared_authority_is_rejected(self):
        self.result["entities_before_join"] = 1
        self.assertIsNotNone(evidence_failure(self.result, self.case))

    def test_no_fault_is_not_clock_recovery(self):
        self.result.update(poll_error=0, diagnostics=[])
        self.assertIsNotNone(evidence_failure(self.result, self.case))

    def test_graceful_stop_does_not_require_clock_failure(self):
        self.result.update(fault="graceful", poll_error=0, diagnostics=[])
        self.assertIsNone(evidence_failure(self.result, ("native", "zero", "same", "graceful")))


if __name__ == "__main__":
    unittest.main()
