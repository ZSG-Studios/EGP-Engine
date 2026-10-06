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
