"""Bounded high-level C# node reload and tree-lifecycle evidence contracts."""

SIGNALS = (
    "state_changed",
    "peer_connected",
    "peer_disconnected",
    "entity_spawned",
    "entity_changed",
    "entity_despawned",
    "message_received",
    "input_received",
    "packet_received",
    "simulation_tick",
    "diagnostic",
)


def node_failure(proofs, live, simulation):
    if len(proofs) != (6 if live else 4):
        return "Missing high-level node checkpoints"
    actions = (
        ["network-live-start"] + ["network-live-check"] * 5
        if live
        else ["network-start", "network-fault", "network-stopped", "network-recover"]
    )
    if [row.get("action") for row in proofs] != actions:
        return "Unordered high-level node checkpoints"
    initial = proofs[0]
    first = initial.get("node_state", {})
    if initial.get("pid", 0) <= 0 or initial.get("peer", 0) <= 0 or initial.get("entity", 0) <= 0:
        return "Invalid high-level process/admission identities"
    identities = ("server_node", "client_node", "server_bridge", "client_bridge", "server_native", "client_native")
    if len({first.get(key) for key in identities}) != 6 or any(not first.get(key) for key in identities):
        return "Invalid high-level node identities"
    for index, proof in enumerate(proofs):
        state = proof.get("node_state", {})
        if state.get("checks") != 6:
            return "Codec replacement/packet forwarding self-checks missing"
        if state.get("reentry_checks") != 18:
            return "Reentrant close/stop replacement self-checks missing"
        sequence = index + 1 if live else (2 if index == 3 else 1)
        if not proof.get("passed") or not proof.get("references_ok") or not state.get("references_ok"):
            return "High-level runtime or native/bridge reference checks failed"
        if any(state.get(key) != first.get(key) for key in identities) or proof.get("pid") != initial.get("pid"):
            return "High-level node/bridge/session identities changed during reload"
        if proof.get("server_id") != first["server_native"] or proof.get("client_id") != first["client_native"]:
            return "Native evidence differs from retained high-level sessions"
        if (
            proof.get("sequence") != sequence
            or proof.get("cpp_hits") != 3 * sequence
            or proof.get("cs_hits") != 3 * sequence
        ):
            return "Missing or duplicated native codec traffic"
        if any(
            state.get(key) != sequence
            for key in ("server_messages", "client_messages", "inputs", "server_packets", "client_packets")
        ):
            return "Lost/duplicated typed events or unowned input accepted"
        if any(state.get(key) != 1 for key in ("server_children", "client_children")) or any(
            state.get(key) is not False for key in ("server_auto_poll", "client_auto_poll")
        ):
            return "Reload created another codec child or lost poll policy"
        if any(state.get(key) != dict.fromkeys(SIGNALS, 1) for key in ("server_connections", "client_connections")):
            return "Orphaned or missing high-level signal connections"
        expected_server = [
            {
                "peer": proofs[-1]["peer"] if not live and i == 2 else initial["peer"],
                "sequence": i,
                "payload": f"{i:02x}00ff2a",
            }
            for i in range(1, sequence + 1)
        ]
        expected_client = [
            {"peer": 0, "sequence": i, "payload": f"{128 + i:02x}00ff2a"} for i in range(1, sequence + 1)
        ]
        expected_inputs = [
            {
                "peer": row["peer"],
                "entity": proofs[-1]["entity"] if not live and row["sequence"] == 2 else initial["entity"],
                "sequence": row["sequence"],
            }
            for row in expected_server
        ]
        if (
            state.get("server_history") != expected_server
            or state.get("client_history") != expected_client
            or state.get("input_history") != expected_inputs
        ):
            return "Registered handler or ownership payload history changed"
        active = live or index in (0, 3)
        if active:
            if (
                proof.get("server_state") != "Listening"
                or proof.get("client_state") != "Connected"
                or proof.get("entities") != 1
                or proof.get("peers") != 1
                or proof.get("server_tick", 0) < 8
            ):
                return "High-level connection/admission lost"
            if proof.get("baseline_state") != {"sequence": sequence, "blob_hex": "00ff2a"}:
                return "High-level decoded entity state lost"
            if state.get("server_spawns") != proof.get("epoch") or state.get("client_spawns") != proof.get("epoch"):
                return "Scene/entity spawn events lost or duplicated"
        elif (
            proof.get("server_state") != "Stopped"
            or proof.get("client_state") != "Stopped"
            or proof.get("baseline_state") != {}
            or any(proof.get(key) != 0 for key in ("server_tick", "peers", "entities"))
        ):
            return "Stopped node cache or lifecycle was not cleared"
    if live:
        if any(row.get(key) != initial.get(key) for row in proofs for key in ("port", "peer", "entity", "epoch")):
            return "High-level live reload changed admission provenance"
        if any(row.get("simulation") != simulation or row.get("diagnostics") != [] for row in proofs):
            return "Impairment profile changed or unexpected high-level diagnostic"
        if any(row.get("states") != ["Connecting", "Synchronizing", "Connected"] for row in proofs):
            return "High-level native client reconnected during live reload"
        if any(
            proofs[i]["server_tick"] >= proofs[i + 1]["server_tick"]
            or proofs[i]["revision"] >= proofs[i + 1]["revision"]
            for i in range(5)
        ):
            return "High-level simulation/replication stopped advancing"
        restores = [row["node_state"]["restores"] for row in proofs]
        if restores != [0, 0, 0, 1, 1, 2]:
            return "High-level handler resubscription did not follow managed reloads"
    else:
        fault = proofs[1]
        if fault.get("poll_error") != 1 or fault.get("gap_ms", 0) < 550 or fault.get("client_polls", 0) < 20:
            return "Native authority clock fault or continuing client poll missing"
        if [row["node_state"].get("restores") for row in proofs] != [0, 0, 1, 1]:
            return "Stopped managed lifecycle did not restore handlers"
        diagnostic = ["Fixed simulation exceeded its catch-up budget; resynchronization required."]
        if [row.get("diagnostics") for row in proofs] != [[], diagnostic, diagnostic, diagnostic]:
            return "Stopped fixed-clock diagnostic missing or duplicated"
        if [row.get("epoch") for row in proofs] != [1, 1, 1, 2]:
            return "Recovery implicitly changed admission epoch"
        if (
            proofs[-1]["peer"] <= initial["peer"]
            or proofs[-1]["entity"] <= initial["entity"]
            or proofs[-1].get("retired_peer_error") != 33
            or proofs[-1].get("retired_entity_error") != 33
            or proofs[-1].get("old_peer") != initial["peer"]
            or proofs[-1].get("old_entity") != initial["entity"]
        ):
            return "High-level recovery reused retired admission/ownership handles"
        if proofs[-1].get("states") != [
            "Connecting",
            "Synchronizing",
            "Connected",
            "Stopped",
            "Disconnected",
            "Stopped",
            "Connecting",
            "Synchronizing",
            "Connected",
        ]:
            return "Unexpected high-level native client recovery lifecycle"
    return None


def node_lifecycle_failure(proofs):
    if len(proofs) != 2 or [row.get("action") for row in proofs] != ["network-node-exit", "network-node-reenter"]:
        return "Missing ordered node tree lifecycle checkpoints"
    exited, entered = proofs
    if not all(row.get("passed") for row in proofs):
        return "Node tree lifecycle runtime checks failed"
    for row in proofs:
        state = row["node_state"]
        if (
            row.get("peers") != 0
            or row.get("entities") != 0
            or state.get("server_native")
            or state.get("client_native")
        ):
            return "Leaving the tree retained an active session/cache"
        if state["server_children"] != 1 or state["client_children"] != 1:
            return "Tree lifecycle duplicated a codec child"
    for key in ("server_node", "client_node", "server_bridge", "client_bridge"):
        if exited["node_state"][key] != entered["node_state"][key]:
            return "Reentering replaced the existing node/codec bridge"
    for key in ("server_connections", "client_connections"):
        if exited["node_state"].get(key) != dict.fromkeys(SIGNALS, 0) or entered["node_state"].get(
            key
        ) != dict.fromkeys(SIGNALS, 1):
            return "Tree exit left callbacks or reentry failed to reconnect exactly once"
    return None


NATIVE_SIGNALS = (
    "state_changed",
    "peer_connected",
    "peer_disconnected",
    "application_received",
    "packet_received",
    "simulation_tick",
    "diagnostic",
)


def node_reentry_failure(proofs, baseline):
    """Fresh native sessions share a retained codec, never retired callbacks or global numeric handle identity."""
    if len(proofs) != 3 or any(row.get("action") != "network-node-reopen" for row in proofs):
        return "Missing repeated fresh-session reentry checkpoints"
    previous = baseline
    seen = {baseline["server_id"], baseline["client_id"]}
    for row in proofs:
        state = row.get("node_state", {})
        sequence = previous["sequence"] + 1
        if state.get("reentry_checks") != 18:
            return "Reentry lost reentrant close/stop self-checks"
        if not row.get("passed") or not row.get("references_ok") or not state.get("references_ok"):
            return "Reentry runtime/session references failed"
        if row.get("pid") != baseline.get("pid") or any(
            state.get(key) != baseline["node_state"].get(key)
            for key in ("server_node", "client_node", "server_bridge", "client_bridge")
        ):
            return "Reentry replaced a node, codec or game process"
        if row.get("previous_sessions") != {"server": previous["server_id"], "client": previous["client_id"]}:
            return "Closed session provenance lost"
        ids = [row.get("server_id"), row.get("client_id")]
        if any(not value or value in seen for value in ids) or ids[0] == ids[1]:
            return "Reentry reused a closed native session"
        seen.update(ids)
        if ids != [state.get("server_native"), state.get("client_native")]:
            return "Reentry node/native identity mismatch"
        if row.get("closed_caches") != dict.fromkeys(
            ("server_peers", "client_peers", "server_entities", "client_entities"), 0
        ):
            return "Reentry retained closed codec caches"
        if row.get("retired_quiet") is not True or row.get("retired_connections") != {
            key: dict.fromkeys(NATIVE_SIGNALS, 0) for key in ("server", "client")
        }:
            return "Closed native callbacks reached retained codec"
        if row.get("native_connections") != {key: dict.fromkeys(NATIVE_SIGNALS, 1) for key in ("server", "client")}:
            return "Fresh native callbacks missing or duplicated"
        if any(
            state.get(key) != dict.fromkeys(SIGNALS, 1) for key in ("server_connections", "client_connections")
        ) or any(state.get(key) != 1 for key in ("server_children", "client_children")):
            return "Reentry duplicated codec forwarding or children"
        if (
            any(state.get(key) is not False for key in ("server_auto_poll", "client_auto_poll"))
            or state.get("checks") != 6
            or state.get("restores") != baseline["node_state"].get("restores")
        ):
            return "Reentry lost managed state/poll policy"
        if (
            row.get("server_state") != "Listening"
            or row.get("client_state") != "Connected"
            or any(row.get(key) != 1 for key in ("peers", "entities", "server_codec_entities"))
            or row.get("server_tick", 0) < 8
        ):
            return "Reentry admission, codec cache or fixed clock missing"
        if (
            row.get("baseline_state") != {"sequence": sequence, "blob_hex": "00ff2a"}
            or row.get("sequence") != sequence
            or row.get("epoch") != previous["epoch"] + 1
        ):
            return "Reentry baseline/epoch lost"
        if row.get("simulation") != baseline.get("simulation") or row.get("port") != baseline.get("port"):
            return "Reentry changed simulation or listener port"
        if (
            row.get("peer", 0) <= 0
            or row.get("entity", 0) <= 0
            or row.get("diagnostics") != baseline.get("diagnostics")
        ):
            return "Reentry admission handles or diagnostics invalid"
        if row.get("states") != previous.get("states", []) + ["Stopped", "Connecting", "Synchronizing", "Connected"]:
            return "Reentry native client lifecycle lost or duplicated"
        if (
            row.get("cpp_hits") != 3 * sequence
            or row.get("cs_hits") != 3 * sequence
            or any(
                state.get(key) != sequence
                for key in ("server_messages", "client_messages", "inputs", "server_packets", "client_packets")
            )
        ):
            return "Reentry lost/duplicated traffic or accepted unowned input"
        expected = {
            "server_history": {"peer": row["peer"], "sequence": sequence, "payload": f"{sequence:02x}00ff2a"},
            "client_history": {"peer": 0, "sequence": sequence, "payload": f"{128 + sequence:02x}00ff2a"},
            "input_history": {"peer": row["peer"], "entity": row["entity"], "sequence": sequence},
        }
        if any(state.get(key) != previous["node_state"][key] + [value] for key, value in expected.items()):
            return "Reentry handler/ownership payload history changed"
        if any(state.get(key) != row["epoch"] for key in ("server_spawns", "client_spawns")):
            return "Reentry lost or duplicated spawn events"
        previous = row
    return None
