"""Physics ownership across explicit node tree exit and new-session admission."""


def box3d_lifecycle_failure(lifecycle, reentry, baseline):
    if len(lifecycle) != 2 or len(reentry) != 3:
        return "Missing adapter tree lifecycle/reentry checkpoints"
    first = baseline.get("box3d", {})
    previous = first
    for index, row in enumerate(lifecycle + reentry):
        state = row.get("box3d", {})
        if not row.get("passed") or any(
            state.get(key) is not True
            for key in ("attached", "references_ok", "cpp_state_ok", "cs_state_ok", "capsule_empty")
        ):
            return "Tree exit/reentry lost adapter/world ownership"
        if any(
            state.get(key) != first.get(key)
            for key in ("adapter_id", "world_id", "body_id", "body_count", "handoffs", "restores", "checks")
        ):
            return "Tree exit/reentry replaced physics state or managed ownership"
        if state.get("failures") != 0 or state.get("connections") != dict.fromkeys(
            ("before_step", "after_step", "failed"), 1
        ):
            return "Tree lifecycle lost or duplicated managed adapter callbacks"
        expected = 1 if index == 0 else 2
        if state.get("adapter_connections") != 1 or state.get("clock_connections") != expected:
            return "Tree lifecycle changed adapter clock attachment"
        if state.get("before") != state.get("tick") or state.get("after") != state.get("tick"):
            return "Tree lifecycle skipped or duplicated authoritative steps"
        if index < 2:
            if any(state.get(key) != first.get(key) for key in ("tick", "hash", "position_y", "velocity_y", "mapping")):
                return "Stopped tree lifecycle changed physics state"
        else:
            if state.get("entity") != row.get("entity") or state.get("mapping") != {str(row["entity"]): 10000}:
                return "Fresh session did not remap stable body to its new entity"
            if state.get("clock_offset", -1) + row.get("server_tick", 0) != state.get("tick"):
                return "Fresh session lost the retained physics clock offset"
            if state.get("tick", 0) <= previous.get("tick", 0) or state.get("position_y", 10000) >= previous.get(
                "position_y", 10000
            ):
                return "Fresh-session physics did not advance"
            if not previous.get("tick", 0) < state.get("client_tick", 0) <= state.get("tick", 0):
                return "Fresh session did not deliver a new physics baseline"
        previous = state
    return None
