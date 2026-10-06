#!/usr/bin/env python3
"""Qualify independent authority stalls and explicit C#/C++ client recovery."""

import argparse
import hashlib
import json
import re
import subprocess
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def evidence_failure(server, client, language, pids):
    if (
        not server.get("passed")
        or not client.get("passed")
        or server.get("role") != "server"
        or client.get("role") != "client"
        or client.get("language") != language
    ):
        return "Missing authority/client completion"
    if [server.get("pid"), client.get("pid")] != pids or len(set(pids)) != 2:
        return "Authority and client must have distinct captured process identities"
    epochs, baselines = server.get("epochs", []), client.get("epochs", [])
    faults, disconnects = server.get("recoveries", []), client.get("disconnects", [])
    if len(epochs) != 4 or len(baselines) != 4 or len(faults) != 3 or len(disconnects) != 3:
        return "Missing repeated faults/admissions/disconnections"
    states = client.get("states", [])
    if states and states[0] == "Stopped":
        states = states[1:]
    expected = []
    for epoch in range(1, 5):
        expected += ["Connecting", "Synchronizing", "Connected", "Stopped"]
        if epoch < 4:
            expected += ["Disconnected", "Stopped"]
    if states != expected:
        return "Missing native disconnect and fresh synchronization history"
    if server.get("diagnostics") != ["Fixed simulation exceeded its catch-up budget; resynchronization required."] * 3:
        return "Missing authority clock diagnostics"
    polls = client.get("poll_utc_ms", [])
    if (
        len(polls) < 30
        or any(not isinstance(t, int) or t <= 0 for t in polls)
        or any(not 0 < b - a <= 250 for a, b in zip(polls, polls[1:]))
    ):
        return "Client polling stalled or wall-clock observations are inconsistent"
    for number, (authority, baseline) in enumerate(zip(epochs, baselines), 1):
        handle = authority.get("entity", 0)
        if (
            authority.get("epoch") != number
            or baseline.get("epoch") != number
            or handle <= 0
            or baseline.get("entity") != handle
            or authority.get("client_id") != 9876
            or authority.get("peer_id", 0) <= 0
            or authority.get("inputs") != number
        ):
            return "Invalid authenticated authority/owner input history"
        if any(
            baseline.get(flag) is not True for flag in ("same_session", "fresh_token", "retired_absent")
        ) or not 0 < baseline.get("physics_tick", 0) == authority.get("client_physics_tick", 0) <= authority.get(
            "world_tick", 0
        ):
            return "Missing fresh owned physics baseline"
        if number > 1 and (
            handle == epochs[number - 2]["entity"]
            or baseline["physics_tick"] <= faults[number - 2].get("checkpoint_tick", 0)
        ):
            return "Retired entity or checkpoint time reused"
    for number, (fault, disconnected) in enumerate(zip(faults, disconnects), 1):
        begin, end = fault.get("start_utc_ms", 0), fault.get("end_utc_ms", 0)
        state_hash = fault.get("checkpoint_hash", "")
        if (
            fault.get("cycle") != number
            or fault.get("old_entity") != epochs[number - 1]["entity"]
            or fault.get("checkpoint_tick", 0) < epochs[number - 1]["world_tick"]
            or fault.get("gap_ms", 0) < 550
            or fault.get("poll_error") != 1
            or fault.get("cleared") is not True
            or fault.get("same_session") is not True
        ):
            return "Invalid authority failure/checkpoint history"
        if (
            not isinstance(state_hash, str)
            or not re.fullmatch("[0-9a-f]{16}", state_hash)
            or abs(end - begin - fault["gap_ms"]) > 50
            or sum(begin <= t <= end for t in polls) < 10
        ):
            return "Missing continuous independent client observations during authority stall"
        if (
            disconnected.get("epoch") != number
            or disconnected.get("entity") != fault["old_entity"]
            or disconnected.get("cleared") is not True
            or not end <= disconnected.get("utc_ms", 0) <= end + 5000
        ):
            return "Missing native disconnect and stale-baseline clearance after the fault"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--client-language", choices=("csharp", "cpp"), required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    engine = args.engine.resolve()
    output = (args.output or ROOT / ".build/egp-net-clock-processes" / str(time.time_ns())).resolve()
    output.mkdir(parents=True, exist_ok=False)
    receipt = {
        "passed": False,
        "engine": str(engine),
        "engine_sha256": hashlib.sha256(engine.read_bytes()).hexdigest(),
        "language": args.client_language,
        "processes": [],
        "source_sha256": {},
    }
    for rel in (
        "misc/egp/network_lab/process_clock_server.gd",
        "modules/egp_net/samples/trilingual/InteropFixture.cs",
        "modules/egp_net/samples/trilingual/extension/probe.cpp",
        "misc/scripts/validate_egp_net_clock_process.py",
        "misc/scripts/test_egp_net_clock_process.py",
    ):
        receipt["source_sha256"][rel] = hashlib.sha256((ROOT / rel).read_bytes()).hexdigest()
    children, logs = [], []
    try:
        with tempfile.TemporaryDirectory(prefix="egp-clock-process-") as temporary:
            handoff = Path(temporary)
            base = [str(engine), "--headless", "--max-fps", "60"]
            if args.project:
                base += ["--path", str(args.project.resolve())]
            base += ["--", "--fixture=clock-processes", "--handoff=" + str(handoff)]
            for role in ("server", "client"):
                if role == "client":
                    deadline = time.monotonic() + 10
                    while (
                        not (handoff / "profile.txt").is_file()
                        or not (handoff / "epoch-1.bin").is_file()
                        or (handoff / "epoch-1.bin").stat().st_size != 2048
                    ):
                        if children[0].poll() is not None or time.monotonic() > deadline:
                            raise RuntimeError("Authority did not issue its fixture admission")
                        time.sleep(0.05)
                path = output / (role + ".log")
                log = path.open("w", encoding="utf-8")
                logs.append(log)
                command = base + ["--role=" + role, "--language=" + args.client_language]
                child = subprocess.Popen(
                    command,
                    cwd=engine.parent,
                    stdout=log,
                    stderr=subprocess.STDOUT,
                    creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
                )
                children.append(child)
                receipt["processes"].append({"role": role, "pid": child.pid, "command": command, "log": str(path)})
            deadline = time.monotonic() + 35
            while any(p.poll() is None for p in children):
                if time.monotonic() > deadline or any(p.poll() not in (None, 0) for p in children):
                    raise RuntimeError("Independent-process fixture failed or exceeded watchdog")
                time.sleep(0.05)
            for item, child, log in zip(receipt["processes"], children, logs):
                log.close()
                text = Path(item["log"]).read_text(encoding="utf-8", errors="replace")
                match = re.search(r"EGP_CLOCK_PROCESS (\{[^\n]+\})", text)
                if child.returncode or not match or "ERROR:" in text:
                    raise RuntimeError(item["role"] + " did not provide clean completion evidence")
                item["exit_code"] = child.returncode
                item["result"] = json.loads(match.group(1))
            failure = evidence_failure(
                *(p["result"] for p in receipt["processes"]), args.client_language, [p.pid for p in children]
            )
            if failure:
                raise RuntimeError(failure)
            receipt["passed"] = True
    except (RuntimeError, OSError, ValueError) as error:
        receipt["error"] = str(error)
    finally:
        for child in children:
            if child.poll() is None:
                child.kill()
                child.wait(timeout=5)
        for log in logs:
            log.close()
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(
        "EGP_CLOCK_PROCESSES "
        + json.dumps({
            "passed": receipt["passed"],
            "language": args.client_language,
            "evidence": str(output / "receipt.json"),
            "error": receipt.get("error"),
        })
    )
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
