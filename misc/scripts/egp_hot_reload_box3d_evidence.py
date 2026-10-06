"""Public C# physics adapter ownership and tick evidence; no client rollback claim."""


def box3d_failure(proofs, live):
    if len(proofs) != (6 if live else 4):
        return "Missing public adapter reload checkpoints"
    first = proofs[0].get("box3d", {})
    if not first.get("adapter_id") or not first.get("world_id") or first["adapter_id"] == first["world_id"]:
        return "Invalid adapter/world identity"
    restores = [0, 0, 0, 1, 1, 2] if live else [0, 0, 1, 1]
    for index, row in enumerate(proofs):
        state = row.get("box3d", {})
        if not row.get("passed") or any(
            state.get(key) is not True
            for key in ("attached", "references_ok", "cpp_state_ok", "cs_state_ok", "capsule_empty")
        ):
            return "Adapter ownership, language references or physics state failed"
        if any(state.get(key) != first.get(key) for key in ("adapter_id", "world_id")):
            return "Reload replaced adapter or world"
        if state.get("checks") != 22 or state.get("failures") != 0:
            return "Adapter capsule checks or failure diagnostics changed"
        if state.get("handoffs") != restores[index] or state.get("restores") != restores[index]:
            return "Adapter transfer/resubscription did not follow managed reload"
        if state.get("connections") != dict.fromkeys(("before_step", "after_step", "failed"), 1):
            return "Adapter callbacks missing or duplicated"
        if state.get("adapter_connections") != 1 or state.get("clock_connections") != 2:
            return "Adapter authority clock connection missing or duplicated"
        tick = state.get("tick", 0)
        if tick <= 0 or state.get("before") != tick or state.get("after") != tick:
            return "Adapter did not step once per authoritative tick"
        if state.get("body_id") != 10000 or state.get("body_count") != 1 or not state.get("hash"):
            return "Stable physics body lost"
        active = live or index in (0, 3)
        if active:
            if state.get("clock_offset", -1) + row.get("server_tick", 0) != tick:
                return "Adapter world clock differs from the retained authority clock"
            if state.get("entity") != row.get("entity") or state.get("mapping") != {str(row["entity"]): 10000}:
                return "Stable body mapping lost during reload"
            if not 0 < state.get("client_tick", 0) <= tick or state.get("position_y", 10000) >= 10000:
                return "Authoritative falling-body baseline missing"
            if state.get("velocity_y", 0) >= 0 or state.get("client_position_y", 10000) >= 10000:
                return "Authoritative movement did not reach client"
            if state["client_position_y"] < state["position_y"] - 0.001:
                return "Client physics baseline is ahead of authority"
        if index and live:
            previous = proofs[index - 1]["box3d"]
            if tick <= previous["tick"] or state["position_y"] >= previous["position_y"]:
                return "Physics stopped advancing across compatible reload"
        if (
            not live
            and index == 2
            and any(
                state.get(key) != proofs[1]["box3d"].get(key)
                for key in ("tick", "hash", "position_y", "velocity_y", "before", "after", "mapping")
            )
        ):
            return "Stopped-authority reload changed preserved physics state"
    return None
