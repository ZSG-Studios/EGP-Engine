#!/usr/bin/env python3
"""Semantic rejection tests for network recovery across real language reloads."""

import copy
import unittest

from validate_egp_hot_reload import network_recovery_failure


def evidence():
    base = {
        "passed": True,
        "references_ok": True,
        "pid": 1234,
        "server_id": "-45",
        "client_id": "-46",
        "server_state": "Listening",
        "client_state": "Connected",
        "server_tick": 12,
        "peers": 1,
        "entities": 1,
        "epoch": 1,
        "cpp_hits": 1,
        "cs_hits": 1,
        "peer": 257,
        "entity": 1,
        "old_peer": 0,
        "old_entity": 0,
        "packets": [{"peer": 257, "payload": "0100ff2a"}],
        "diagnostics": [],
        "states": ["Connecting", "Synchronizing", "Connected"],
    }
    initial = dict(copy.deepcopy(base), action="network-start")
    fault = dict(
        copy.deepcopy(base),
        action="network-fault",
        server_state="Stopped",
        client_state="Stopped",
        server_tick=0,
        peers=0,
        entities=0,
        gap_ms=550,
        client_polls=64,
        poll_error=1,
        diagnostics=["Fixed simulation exceeded its catch-up budget; resynchronization required."],
    )
    stopped = dict(copy.deepcopy(fault), action="network-stopped")
    recovered = dict(
        copy.deepcopy(base),
        action="network-recover",
        epoch=2,
        cpp_hits=2,
        cs_hits=2,
        peer=513,
        entity=2,
        old_peer=257,
        old_entity=1,
        retired_peer_error=33,
        retired_entity_error=33,
        packets=[{"peer": 257, "payload": "0100ff2a"}, {"peer": 513, "payload": "0200ff2a"}],
        states=[
            "Connecting",
            "Synchronizing",
            "Connected",
            "Stopped",
            "Disconnected",
            "Stopped",
            "Connecting",
            "Synchronizing",
            "Connected",
        ],
    )
    return [initial, fault, stopped, recovered]


class NetworkReloadEvidenceTests(unittest.TestCase):
    def test_complete_bounded_recovery(self):
        self.assertIsNone(network_recovery_failure(evidence()))

    def reject(self, index, key, value):
        proofs = evidence()
        proofs[index][key] = value
        self.assertIsNotNone(network_recovery_failure(proofs))

    def test_session_replacement(self):
        self.reject(2, "server_id", "-99")

    def test_game_restart(self):
        self.reject(3, "pid", 5678)

    def test_lost_serialized_reference(self):
        self.reject(2, "references_ok", False)

    def test_implicit_restart(self):
        self.reject(2, "server_state", "Listening")

    def test_lost_fault_diagnostic(self):
        self.reject(1, "diagnostics", [])

    def test_stalled_client(self):
        self.reject(1, "client_polls", 0)

    def test_duplicate_callback(self):
        self.reject(3, "cpp_hits", 3)

    def test_lost_managed_callback(self):
        self.reject(3, "cs_hits", 1)

    def test_retired_handle_reuse(self):
        self.reject(3, "entity", 1)

    def test_retired_handle_still_usable(self):
        self.reject(3, "retired_peer_error", 0)

    def test_corrupted_opaque_payload(self):
        self.reject(3, "packets", [{"peer": 257, "payload": "01002a"}, {"peer": 513, "payload": "0200ff2a"}])

    def test_skipped_fresh_admission(self):
        self.reject(3, "states", ["Connected"])

    def test_missing_checkpoint(self):
        self.assertIsNotNone(network_recovery_failure(evidence()[:3]))


if __name__ == "__main__":
    unittest.main()
