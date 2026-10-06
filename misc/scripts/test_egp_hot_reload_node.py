#!/usr/bin/env python3
"""Reject false positives in high-level C# reload and tree lifecycle receipts."""

import copy
import unittest

from egp_hot_reload_node_evidence import SIGNALS, node_failure, node_lifecycle_failure
from test_egp_hot_reload_network import evidence, live_evidence


def node_evidence(live=True):
    rows = live_evidence() if live else evidence()
    if not live:
        rows[-1]["diagnostics"] = rows[1]["diagnostics"].copy()
    for index, row in enumerate(rows):
        sequence = index + 1 if live else (2 if index == 3 else 1)
        row.update(sequence=sequence, cpp_hits=3 * sequence, cs_hits=3 * sequence)
        active = live or index in (0, 3)
        row["baseline_state"] = {"sequence": sequence, "blob_hex": "00ff2a"} if active else {}
        state = {
            "checks": 6,
            "server_node": "11",
            "client_node": "12",
            "server_bridge": "13",
            "client_bridge": "14",
            "server_native": row["server_id"],
            "client_native": row["client_id"],
            "references_ok": True,
            "server_children": 1,
            "client_children": 1,
            "server_auto_poll": False,
            "client_auto_poll": False,
            "server_connections": dict.fromkeys(SIGNALS, 1),
            "client_connections": dict.fromkeys(SIGNALS, 1),
            "server_messages": sequence,
            "client_messages": sequence,
            "inputs": sequence,
            "server_packets": sequence,
            "client_packets": sequence,
            "server_spawns": row["epoch"],
            "client_spawns": row["epoch"],
            "restores": [0, 0, 0, 1, 1, 2][index] if live else [0, 0, 1, 1][index],
            "server_history": [],
            "client_history": [],
            "input_history": [],
        }
        for number in range(1, sequence + 1):
            peer = rows[-1]["peer"] if not live and number == 2 else rows[0]["peer"]
            entity = rows[-1]["entity"] if not live and number == 2 else rows[0]["entity"]
            state["server_history"].append({"peer": peer, "sequence": number, "payload": f"{number:02x}00ff2a"})
            state["client_history"].append({"peer": 0, "sequence": number, "payload": f"{128 + number:02x}00ff2a"})
            state["input_history"].append({"peer": peer, "entity": entity, "sequence": number})
        row["node_state"] = state
    return rows


def lifecycle_evidence():
    rows = [copy.deepcopy(node_evidence()[0]) for _ in range(2)]
    for index, row in enumerate(rows):
        row.update(action=["network-node-exit", "network-node-reenter"][index], peers=0, entities=0)
        row["node_state"].update(server_native="", client_native="")
        for key in ("server_connections", "client_connections"):
            row["node_state"][key] = dict.fromkeys(SIGNALS, index)
    return rows


class NodeEvidenceTests(unittest.TestCase):
    def test_live_valid(self):
        rows = node_evidence()
        self.assertIsNone(node_failure(rows, True, rows[0]["simulation"]))

    def test_stopped_valid(self):
        self.assertIsNone(node_failure(node_evidence(False), False, {}))

    def test_tree_valid(self):
        self.assertIsNone(node_lifecycle_failure(lifecycle_evidence()))


def rejection(index, path, value, live=True, tree=False):
    def test(self):
        rows = lifecycle_evidence() if tree else node_evidence(live)
        target = rows[index]
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = value
        failure = node_lifecycle_failure(rows) if tree else node_failure(rows, live, rows[0].get("simulation", {}))
        self.assertIsNotNone(failure, (index, path, value))

    return test


CASES: list[tuple[int, tuple[str, ...], object]] = [
    (3, ("node_state", "checks"), 0),
    (3, ("passed",), False),
    (3, ("references_ok",), False),
    (3, ("pid",), 999),
    (3, ("sequence",), 3),
    (3, ("cpp_hits",), 0),
    (3, ("cs_hits",), 99),
    (3, ("baseline_state",), {}),
    (3, ("server_tick",), 0),
    (3, ("revision",), 0),
    (3, ("states",), []),
    (3, ("diagnostics",), ["unexpected"]),
    (3, ("simulation",), {}),
    (3, ("server_state",), "Stopped"),
    (3, ("client_state",), "Disconnected"),
    (3, ("peers",), 0),
    (3, ("entities",), 2),
    (0, ("pid",), 0),
    (3, ("server_id",), "replaced"),
    (3, ("client_id",), "replaced"),
    (3, ("port",), -1),
    (3, ("peer",), 999),
    (3, ("entity",), 999),
    (3, ("epoch",), 2),
]
for key in ("server_node", "client_node", "server_bridge", "client_bridge", "server_native", "client_native"):
    CASES.append((3, ("node_state", key), "changed"))
for key in (
    "server_children",
    "client_children",
    "server_messages",
    "client_messages",
    "inputs",
    "server_packets",
    "client_packets",
    "server_spawns",
    "client_spawns",
    "restores",
):
    CASES.append((3, ("node_state", key), 99))
for key in ("server_auto_poll", "client_auto_poll"):
    CASES.append((3, ("node_state", key), True))
for key in ("server_history", "client_history", "input_history"):
    CASES.append((3, ("node_state", key), []))
for key in ("server_connections", "client_connections"):
    for signal in SIGNALS:
        CASES.append((3, ("node_state", key, signal), 2))
for number, (index, path, value) in enumerate(CASES):
    setattr(NodeEvidenceTests, f"test_live_reject_{number:02d}", rejection(index, path, value))

STOPPED: list[tuple[int, tuple[str, ...], object]] = [
    (1, ("poll_error",), 0),
    (1, ("gap_ms",), 549),
    (1, ("client_polls",), 19),
    (2, ("server_state",), "Listening"),
    (2, ("baseline_state",), {"sequence": 1}),
    (2, ("node_state", "restores"), 0),
    (3, ("peer",), 257),
    (3, ("entity",), 1),
    (3, ("retired_peer_error",), 0),
    (3, ("retired_entity_error",), 0),
    (1, ("server_tick",), 1),
    (2, ("peers",), 1),
    (2, ("entities",), 1),
    (1, ("diagnostics",), []),
    (3, ("diagnostics",), []),
    (1, ("epoch",), 2),
    (3, ("old_peer",), 999),
    (3, ("old_entity",), 999),
    (3, ("states",), []),
]
for number, (index, path, value) in enumerate(STOPPED):
    setattr(NodeEvidenceTests, f"test_stopped_reject_{number:02d}", rejection(index, path, value, live=False))

TREE: list[tuple[int, tuple[str, ...], object]] = [(0, ("passed",), False), (0, ("peers",), 1), (0, ("entities",), 1)]
for key in ("server_native", "client_native"):
    TREE.append((0, ("node_state", key), "retained"))
for key in ("server_node", "client_node", "server_bridge", "client_bridge"):
    TREE.append((1, ("node_state", key), "replaced"))
for key in ("server_children", "client_children"):
    TREE.append((1, ("node_state", key), 2))
for key in ("server_connections", "client_connections"):
    for signal in SIGNALS:
        TREE.append((0, ("node_state", key, signal), 1))
        TREE.append((1, ("node_state", key, signal), 2))
for number, (index, path, value) in enumerate(TREE):
    setattr(NodeEvidenceTests, f"test_tree_reject_{number:02d}", rejection(index, path, value, tree=True))


if __name__ == "__main__":
    unittest.main()
