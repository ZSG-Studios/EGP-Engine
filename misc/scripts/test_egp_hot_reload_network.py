#!/usr/bin/env python3
"""Semantic rejection tests for network recovery across real language reloads."""

import copy
import unittest

from validate_egp_hot_reload import (
    facade_failure,
    network_live_failure,
    network_physics_failure,
    network_recovery_failure,
)


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


SIMULATION = {"simulated_latency_ms": 30, "simulated_jitter_ms": 5, "simulated_loss": 5}


def live_evidence():
    proofs = []
    for sequence in range(1, 7):
        proofs.append({
            "action": "network-live-start" if sequence == 1 else "network-live-check",
            "passed": True,
            "references_ok": True,
            "pid": 1234,
            "server_id": "-45",
            "client_id": "-46",
            "port": 47100,
            "peer": 257,
            "entity": 1,
            "epoch": 1,
            "peers": 1,
            "entities": 1,
            "sequence": sequence,
            "cpp_hits": sequence,
            "cs_hits": sequence,
            "server_state": "Listening",
            "client_state": "Connected",
            "diagnostics": [],
            "states": ["Connecting", "Synchronizing", "Connected"],
            "simulation": dict(SIMULATION),
            "baseline_hex": bytes([sequence, 0, 255, 42]).hex(),
            "revision": sequence,
            "server_tick": sequence * 30,
            "total_client_polls": sequence * 60,
            "packets": [{"peer": 257, "payload": bytes([n, 0, 255, 42]).hex()} for n in range(1, sequence + 1)],
            "client_packets": [
                {"peer": 0, "payload": bytes([128 + n, 0, 255, 42]).hex()} for n in range(1, sequence + 1)
            ],
        })
    return proofs


class LiveReloadEvidenceTests(unittest.TestCase):
    def test_complete_uninterrupted_admission(self):
        self.assertIsNone(network_live_failure(live_evidence(), SIMULATION))

    def reject(self, key, value):
        proofs = live_evidence()
        proofs[3][key] = value
        self.assertIsNotNone(network_live_failure(proofs, SIMULATION))

    def test_client_rejoin(self):
        self.reject("peer", 513)

    def test_entity_replacement(self):
        self.reject("entity", 2)

    def test_connection_interruption(self):
        self.reject("states", ["Connecting", "Synchronizing", "Connected", "Stopped", "Connected"])

    def test_authority_fault(self):
        self.reject("diagnostics", ["Fixed simulation exceeded its catch-up budget; resynchronization required."])

    def test_replaced_native_wrapper(self):
        self.reject("client_id", "-99")

    def test_ignored_loss_configuration(self):
        self.reject("simulation", dict(SIMULATION, simulated_loss=0))

    def test_lost_server_reply(self):
        self.reject("client_packets", [])

    def test_corrupt_server_reply(self):
        proof = live_evidence()[3]
        proof["client_packets"][3]["payload"] = "0400ff2a"
        self.reject("client_packets", proof["client_packets"])

    def test_duplicate_managed_callbacks(self):
        self.reject("cs_hits", 5)

    def test_stale_baseline(self):
        self.reject("baseline_hex", "0100ff2a")

    def test_stalled_revision(self):
        self.reject("revision", 3)

    def test_stalled_clock(self):
        self.reject("server_tick", 90)

    def test_stalled_language_pumps(self):
        self.reject("total_client_polls", 180)

    def test_missing_live_checkpoint(self):
        self.assertIsNotNone(network_live_failure(live_evidence()[:5], SIMULATION))


def physics_evidence(live):
    proofs = live_evidence() if live else evidence()
    for index, proof in enumerate(proofs):
        offset = 0 if live or index == 0 else 20
        tick = proof["server_tick"] + offset
        connected = proof["client_state"] == "Connected"
        client_tick = tick - 2 if connected else 0
        proof["physics"] = {
            "enabled": True,
            "world_id": "-47",
            "body_id": 10000,
            "body_count": 1,
            "cpp_state_ok": True,
            "cs_state_ok": True,
            "fingerprint": "egp-box3d:hz60:sub4:workers1",
            "tick": tick,
            "clock_offset": offset,
            "hash": f"{tick:016x}",
            "position_y": 10000.0 - tick,
            "velocity_y": -1.0,
            "client_tick": client_tick,
            "client_body_id": 10000 if connected else 0,
            "client_position_y": 10000.0 - client_tick if connected else 0.0,
            "checkpoint_tick": offset,
            "checkpoint_hash": f"{offset:016x}" if offset else "",
        }
    if not live:
        proofs[-1].update(
            corrupt_snapshot_error=16,
            corrupt_restore_unchanged=True,
            restored_tick=20,
            restored_hash="0000000000000014",
            restored_y=9980.0,
        )
    return proofs


class PhysicsReloadEvidenceTests(unittest.TestCase):
    def reject(self, index, key, value, live=False):
        proofs = physics_evidence(live)
        proofs[index]["physics"][key] = value
        self.assertIsNotNone(network_physics_failure(proofs, live))

    def reject_recovery(self, key, value):
        proofs = physics_evidence(False)
        proofs[-1][key] = value
        self.assertIsNotNone(network_physics_failure(proofs, False))

    def test_live_world_and_baselines(self):
        self.assertIsNone(network_physics_failure(physics_evidence(True), True))

    def test_explicit_checkpoint_restore(self):
        self.assertIsNone(network_physics_failure(physics_evidence(False), False))

    def test_world_replacement(self):
        self.reject(2, "world_id", "-99")

    def test_lost_body(self):
        self.reject(2, "body_count", 0)

    def test_lost_managed_physics_reference(self):
        self.reject(2, "cs_state_ok", False)

    def test_clock_offset_loss(self):
        self.reject(3, "clock_offset", 0)

    def test_wrong_profile(self):
        self.reject(2, "fingerprint", "egp-box3d:hz120:sub4:workers1")

    def test_nonfinite_state(self):
        self.reject(2, "position_y", float("nan"))

    def test_stale_client_baseline(self):
        self.reject(3, "client_tick", 20)

    def test_client_ahead_of_authority(self):
        self.reject(3, "client_tick", 33)

    def test_reused_body_mapping(self):
        self.reject(3, "client_body_id", 2)

    def test_live_physics_stalled(self):
        self.reject(3, "position_y", 9910.0, live=True)

    def test_accepted_corrupt_checkpoint(self):
        self.reject_recovery("corrupt_snapshot_error", 0)

    def test_corruption_changed_world(self):
        self.reject_recovery("corrupt_restore_unchanged", False)

    def test_inexact_restored_hash(self):
        self.reject_recovery("restored_hash", "0000000000000015")

    def test_inexact_restored_tick(self):
        self.reject_recovery("restored_tick", 21)

    def test_inexact_restored_position(self):
        self.reject_recovery("restored_y", 9979.0)

    def test_stopped_reload_changed_checkpoint(self):
        self.reject(2, "hash", "0000000000000015")


def facade_evidence(live=True):
    proofs = live_evidence() if live else evidence()
    for index, proof in enumerate(proofs):
        sequence = proof["sequence"] if live else proof["epoch"]
        restores = (0 if index < 3 else 1 if index < 5 else 2) if live else (0 if index < 2 else 1)
        proof["facade"] = {
            "checks": 23,
            "server_id": proof["server_id"],
            "client_id": proof["client_id"],
            "server_hits": sequence,
            "client_hits": sequence if live else 0,
            "capsules_empty": True,
            "handoffs": restores,
            "restores": restores,
            "server_connections": {
                "state_changed": 1,
                "peer_connected": 1,
                "peer_disconnected": 1,
                "application_received": 4,
                "packet_received": 1,
                "simulation_tick": 1,
                "diagnostic": 2,
            },
            "client_connections": {
                "state_changed": 2,
                "peer_connected": 1,
                "peer_disconnected": 1,
                "application_received": 2,
                "packet_received": 1,
                "simulation_tick": 1,
                "diagnostic": 1,
            },
        }
    return proofs


class ManagedFacadeEvidenceTests(unittest.TestCase):
    def test_complete_live_handoff(self):
        self.assertIsNone(facade_failure(facade_evidence(), True))

    def test_complete_stopped_handoff(self):
        self.assertIsNone(facade_failure(facade_evidence(False), False))

    def test_failed_runtime_checkpoint(self):
        proofs = facade_evidence()
        proofs[3]["passed"] = False
        self.assertIsNotNone(facade_failure(proofs, True))

    def reject(self, index, key, value, live=True):
        proofs = facade_evidence(live)
        proofs[index]["facade"][key] = value
        self.assertIsNotNone(facade_failure(proofs, live))

    def test_missing_checkpoint(self):
        self.assertIsNotNone(facade_failure(facade_evidence()[:-1], True))

    def test_missing_facade(self):
        proofs = facade_evidence()
        del proofs[3]["facade"]
        self.assertIsNotNone(facade_failure(proofs, True))

    def test_incomplete_self_tests(self):
        self.reject(0, "checks", 22)

    def test_orphaned_server_connections(self):
        proofs = facade_evidence()
        proofs[3]["facade"]["server_connections"]["simulation_tick"] = 2
        self.assertIsNotNone(facade_failure(proofs, True))

    def test_missing_client_connection(self):
        proofs = facade_evidence()
        del proofs[3]["facade"]["client_connections"]["application_received"]
        self.assertIsNotNone(facade_failure(proofs, True))

    def test_replaced_session(self):
        self.reject(3, "server_id", "other")

    def test_duplicate_server_callback(self):
        self.reject(4, "server_hits", 6)

    def test_missing_client_callback(self):
        self.reject(5, "client_hits", 5)

    def test_unconsumed_capsule(self):
        self.reject(3, "capsules_empty", False)

    def test_lost_handoff(self):
        self.reject(3, "handoffs", 2)

    def test_failed_compile_transferred_ownership(self):
        proofs = facade_evidence()
        proofs[1]["facade"].update(handoffs=1, restores=1)
        self.assertIsNotNone(facade_failure(proofs, True))

    def test_cpp_reload_transferred_managed_ownership(self):
        proofs = facade_evidence()
        proofs[4]["facade"].update(handoffs=2, restores=2)
        self.assertIsNotNone(facade_failure(proofs, True))

    def test_combined_reload_skipped_handoff(self):
        proofs = facade_evidence()
        proofs[5]["facade"].update(handoffs=1, restores=1)
        self.assertIsNotNone(facade_failure(proofs, True))

    def test_stopped_reload_skipped_handoff(self):
        proofs = facade_evidence(False)
        proofs[2]["facade"].update(handoffs=0, restores=0)
        self.assertIsNotNone(facade_failure(proofs, False))


if __name__ == "__main__":
    unittest.main()
