#!/usr/bin/env python3
"""Reject incomplete native callback ownership and repeated node reentry evidence."""

import copy
import unittest

from egp_hot_reload_node_evidence import NATIVE_SIGNALS, node_reentry_failure
from test_egp_hot_reload_node import node_evidence


def evidence(live=True):
    baseline = node_evidence(live)[-1]
    previous = baseline
    rows = []
    for cycle in range(3):
        row = copy.deepcopy(previous)
        sequence = previous["sequence"] + 1
        row.update(
            action="network-node-reopen",
            server_id=str(-100 - cycle * 2),
            client_id=str(-101 - cycle * 2),
            previous_sessions={"server": previous["server_id"], "client": previous["client_id"]},
            closed_caches=dict.fromkeys(("server_peers", "client_peers", "server_entities", "client_entities"), 0),
            retired_quiet=True,
            retired_connections={key: dict.fromkeys(NATIVE_SIGNALS, 0) for key in ("server", "client")},
            native_connections={key: dict.fromkeys(NATIVE_SIGNALS, 1) for key in ("server", "client")},
            sequence=sequence,
            epoch=previous["epoch"] + 1,
            cpp_hits=3 * sequence,
            cs_hits=3 * sequence,
            server_codec_entities=1,
            server_tick=18,
            baseline_state={"sequence": sequence, "blob_hex": "00ff2a"},
            states=previous["states"] + ["Stopped", "Connecting", "Synchronizing", "Connected"],
        )
        # Numeric handles are session-scoped; a new native session may issue the same numbers.
        row.update(peer=257, entity=1)
        state = row["node_state"]
        state.update(server_native=row["server_id"], client_native=row["client_id"])
        for key in ("server_messages", "client_messages", "inputs", "server_packets", "client_packets"):
            state[key] = sequence
        for key in ("server_spawns", "client_spawns"):
            state[key] = row["epoch"]
        state["server_history"].append({"peer": row["peer"], "sequence": sequence, "payload": f"{sequence:02x}00ff2a"})
        state["client_history"].append({"peer": 0, "sequence": sequence, "payload": f"{128 + sequence:02x}00ff2a"})
        state["input_history"].append({"peer": row["peer"], "entity": row["entity"], "sequence": sequence})
        rows.append(row)
        previous = row
    return rows, baseline


class ReentryEvidenceTests(unittest.TestCase):
    def test_live_valid(self):
        self.assertIsNone(node_reentry_failure(*evidence()))

    def test_stopped_valid(self):
        self.assertIsNone(node_reentry_failure(*evidence(False)))

    def test_missing_cycles(self):
        rows, baseline = evidence()
        self.assertIsNotNone(node_reentry_failure(rows[:2], baseline))

    def test_numeric_handles_are_session_scoped(self):
        rows, baseline = evidence()
        self.assertEqual(len({row["peer"] for row in rows}), 1)
        self.assertEqual(len({row["entity"] for row in rows}), 1)
        self.assertIsNone(node_reentry_failure(rows, baseline))


def reject(path, value):
    def test(self):
        rows, baseline = evidence()
        target = rows[1]
        for key in path[:-1]:
            target = target[key]
        target[path[-1]] = value
        self.assertIsNotNone(node_reentry_failure(rows, baseline), (path, value))

    return test


CASES: list[tuple[tuple[str, ...], object]] = [
    (("passed",), False),
    (("references_ok",), False),
    (("action",), "wrong"),
    (("pid",), 999),
    (("server_id",), "-100"),
    (("client_id",), "-101"),
    (("previous_sessions",), {}),
    (("closed_caches", "server_entities"), 1),
    (("closed_caches", "client_entities"), 1),
    (("closed_caches", "server_peers"), 1),
    (("closed_caches", "client_peers"), 1),
    (("retired_quiet",), False),
    (("server_codec_entities",), 0),
    (("server_state",), "Stopped"),
    (("client_state",), "Disconnected"),
    (("peers",), 0),
    (("entities",), 0),
    (("server_tick",), 0),
    (("baseline_state",), {}),
    (("sequence",), 0),
    (("epoch",), 0),
    (("simulation",), {}),
    (("port",), -1),
    (("peer",), 0),
    (("entity",), 0),
    (("diagnostics",), ["unexpected"]),
    (("states",), []),
    (("cpp_hits",), 99),
    (("cs_hits",), 99),
]
for key in ("server_node", "client_node", "server_bridge", "client_bridge", "server_native", "client_native"):
    CASES.append((("node_state", key), "replaced"))
for key in (
    "server_children",
    "client_children",
    "checks",
    "reentry_checks",
    "restores",
    "server_messages",
    "client_messages",
    "inputs",
    "server_packets",
    "client_packets",
    "server_spawns",
    "client_spawns",
):
    CASES.append((("node_state", key), 99))
for key in ("server_auto_poll", "client_auto_poll"):
    CASES.append((("node_state", key), True))
for key in ("server_history", "client_history", "input_history"):
    CASES.append((("node_state", key), []))
for signal in NATIVE_SIGNALS:
    for key in ("server", "client"):
        CASES.append((("retired_connections", key, signal), 1))
        CASES.append((("native_connections", key, signal), 2))
for number, (path, value) in enumerate(CASES):
    setattr(ReentryEvidenceTests, f"test_reject_{number:02d}", reject(path, value))


if __name__ == "__main__":
    unittest.main()
