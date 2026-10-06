#!/usr/bin/env python3
"""Exercise live C#/C++ objects through the editor's real debugger and build panel."""

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import subprocess
import time
from pathlib import Path
from xml.sax.saxutils import escape

ROOT = Path(__file__).resolve().parents[2]
PROBE = """using Godot;
using System.Runtime.Loader;
public partial class ReloadProbe : Node, ISerializationListener {
    [Export] public int Counter { get; set; } = 1;
    [Export] public Vector3 PositionValue { get; set; }
    [Export] public Node? Reference { get; set; }
    [Export] public int ReadyCount { get; set; }
    [Export] public int BeforeCount { get; set; }
    [Export] public int AfterCount { get; set; }
    [Export] public Godot.Collections.Dictionary NetworkState { get; set; } = new();
    [Export] public int NetworkHits { get; set; }
    [Signal] public delegate void PulseEventHandler(int value);
    public override void _Ready() { ReadyCount++; }
    public int Version() => VERSION;
    public bool Collectible() => AssemblyLoadContext.GetLoadContext(GetType().Assembly)!.IsCollectible;
    public void Fire() => EmitSignal(SignalName.Pulse, 1);
    public Error PollNetwork(bool authority) {
        var server = NetworkState["server"].AsGodotObject();
        var client = NetworkState["client"].AsGodotObject();
        var result = authority ? (Error)server.Call("poll").AsInt32() : Error.Ok;
        var clientResult = (Error)client.Call("poll").AsInt32();
        return result != Error.Ok ? result : clientResult;
    }
    public void ReceiveNetwork(long peer, byte[] payload) { NetworkHits++; }
    public Godot.Collections.Dictionary GetPhysicsState() {
        if (!NetworkState.ContainsKey("world")) return new();
        var world = NetworkState["world"].AsGodotObject();
        var state = world.Call("get_body_state", 10000).AsGodotDictionary();
        state["tick"] = world.Call("get_tick");
        state["hash"] = world.Call("get_state_hash");
        return state;
    }
    public void HoldRoot(string path) {
        var thread = new System.Threading.Thread(() => {
            System.IO.File.WriteAllText(path + ".started", "started");
            while (!System.IO.File.Exists(path)) System.Threading.Thread.Sleep(10);
            System.IO.File.WriteAllText(path + ".finished", "finished");
        });
        thread.IsBackground = true;
        thread.Start();
    }
    public void OnBeforeSerialize() { BeforeCount++; }
    public void OnAfterDeserialize() { AfterCount++; }
}
"""
RECEIVER = """using Godot;
public partial class ReloadReceiver : Node {
    [Export] public int Hits { get; set; }
    public void Listen(ReloadProbe probe) { probe.Pulse += Receive; }
    private void Receive(int value) { Hits += value; }
}
"""
FACADE_MEMBERS = """
    [Export] public Godot.Collections.Dictionary ReloadCapsules { get; set; } = new();
    [Export] public int FacadeServerHits { get; set; }
    [Export] public int FacadeClientHits { get; set; }
    [Export] public int FacadeHandoffs { get; set; }
    [Export] public int FacadeRestores { get; set; }
    [Export] public int FacadeChecks { get; set; }
    private EGP.Networking.NetSession? facadeServer, facadeClient;
    private void RequireFacade(bool condition) {
        if (!condition) throw new System.InvalidOperationException("Managed facade self-test failed");
        FacadeChecks++;
    }
    private void RejectCapsule(Godot.Collections.Dictionary capsule) {
        try { EGP.Networking.NetSession.ResumeAfterReload(capsule); }
        catch (System.ArgumentException) { FacadeChecks++; return; }
        throw new System.InvalidOperationException("Invalid reload capsule accepted");
    }
    private void CheckFacadeInputs() {
        RejectCapsule(null!);
        RejectCapsule(new());
        using var scratch = new EGP.Networking.NetSession();
        RequireFacade(scratch.Configure() == Error.Ok);
        var reference = scratch.Native;
        int oldHits = 0, newHits = 0;
        scratch.ApplicationReceived += (peer, payload) => oldHits++;
        reference.EmitSignal("application_received", 0L, System.Array.Empty<byte>());
        RequireFacade(oldHits == 1);
        var capsule = scratch.DetachForReload();
        var duplicate = capsule.Duplicate();
        reference.EmitSignal("application_received", 0L, System.Array.Empty<byte>());
        RequireFacade(oldHits == 1);
        try { _ = scratch.Native; throw new System.InvalidOperationException("Detached wrapper remained usable"); }
        catch (System.ObjectDisposedException) { FacadeChecks++; }
        try { scratch.DetachForReload(); throw new System.InvalidOperationException("Second detach accepted"); }
        catch (System.ObjectDisposedException) { FacadeChecks++; }
        foreach (var key in new[] { "version", "session", "token" }) {
            var missing = capsule.Duplicate(); missing.Remove(key); RejectCapsule(missing);
            var wrong = capsule.Duplicate(); wrong[key] = key == "session" ? (Variant)1 : (Variant)false;
            RejectCapsule(wrong);
        }
        var future = capsule.Duplicate(); future["version"] = 4294967297L; RejectCapsule(future);
        var forged = capsule.Duplicate(); forged["token"] = "invalid"; RejectCapsule(forged);
        using var foreign = new RefCounted();
        var wrongClass = capsule.Duplicate(); wrongClass["session"] = foreign; RejectCapsule(wrongClass);
        RequireFacade(capsule.Count == 3);
        using var restored = EGP.Networking.NetSession.ResumeAfterReload(capsule);
        restored.ApplicationReceived += (peer, payload) => newHits++;
        scratch.Dispose();
        RequireFacade(capsule.Count == 0 && GodotObject.IsInstanceValid(reference) && restored.Native == reference);
        RejectCapsule(capsule);
        RejectCapsule(duplicate);
        reference.EmitSignal("application_received", 0L, System.Array.Empty<byte>());
        RequireFacade(oldHits == 1 && newHits == 1 && restored.Poll() == Error.Ok);
    }
    private void SubscribeFacades() {
        facadeServer!.ApplicationReceived += (peer, payload) => FacadeServerHits++;
        facadeClient!.ApplicationReceived += (peer, payload) => FacadeClientHits++;
    }
    public Godot.Collections.Dictionary CreateNetworkSessions() {
        CheckFacadeInputs();
        facadeServer = new(); facadeClient = new(); SubscribeFacades();
        return new() { ["server"] = facadeServer.Native, ["client"] = facadeClient.Native };
    }
    public Godot.Collections.Dictionary GetFacadeState() => new() {
        ["checks"] = FacadeChecks, ["server_hits"] = FacadeServerHits, ["client_hits"] = FacadeClientHits,
        ["handoffs"] = FacadeHandoffs, ["restores"] = FacadeRestores,
        ["server_id"] = facadeServer == null ? "" : unchecked((long)facadeServer.Native.GetInstanceId()).ToString(),
        ["client_id"] = facadeClient == null ? "" : unchecked((long)facadeClient.Native.GetInstanceId()).ToString(),
        ["capsules_empty"] = ReloadCapsules.Count == 0
    };
    public void CloseFacades() { facadeClient?.Dispose(); facadeServer?.Dispose(); facadeClient = null; facadeServer = null; }
"""


def facade_failure(proofs, live):
    """Check real public-facade callbacks and consumed ownership after assembly reload."""
    if len(proofs) != (6 if live else 4):
        return "Missing managed facade checkpoints"
    for proof in proofs:
        if not proof.get("passed"):
            return "Managed facade runtime checkpoint failed"
        state = proof.get("facade", {})
        sequence = proof.get("sequence", proof.get("epoch")) if live else proof.get("epoch")
        client_hits = sequence if live else 0
        if (
            state.get("checks") != 21
            or state.get("server_id") != proof.get("server_id")
            or state.get("client_id") != proof.get("client_id")
            or state.get("server_hits") != sequence
            or state.get("client_hits") != client_hits
            or not state.get("capsules_empty")
            or state.get("handoffs") != state.get("restores")
        ):
            return "Managed facade ownership, self-test or callbacks failed"
    if live:
        initial, managed_failure, native_failure, cs, cpp, combined = proofs
        if any(p["facade"]["restores"] != initial["facade"]["restores"] for p in (managed_failure, native_failure)):
            return "Failed compile transferred facade ownership"
        if (
            cs["facade"]["restores"] <= initial["facade"]["restores"]
            or cpp["facade"]["restores"] != cs["facade"]["restores"]
        ):
            return "C# handoff missing or C++ reload changed managed ownership"
        if combined["facade"]["restores"] <= cpp["facade"]["restores"]:
            return "Combined reload did not restore managed facade"
    elif proofs[2]["facade"]["restores"] <= proofs[1]["facade"]["restores"]:
        return "Stopped reload did not restore managed facade"
    return None


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def network_recovery_failure(proofs):
    """Reject incomplete combined-reload evidence, including implicit recovery."""
    if len(proofs) != 4 or [p.get("action") for p in proofs] != [
        "network-start",
        "network-fault",
        "network-stopped",
        "network-recover",
    ]:
        return "Missing ordered network reload checkpoints"
    initial, fault, stopped, recovered = proofs
    for proof in proofs:
        if not proof.get("passed") or not proof.get("references_ok"):
            return "Native session references or runtime checks failed"
        if any(proof.get(k) != initial.get(k) for k in ("pid", "server_id", "client_id")):
            return "Game or native sessions were replaced during reload"
    if initial.get("server_id") == initial.get("client_id") or int(initial.get("pid", 0)) <= 0:
        return "Invalid native session/process identities"
    if fault.get("poll_error") != 1 or fault.get("gap_ms", 0) < 550 or fault.get("client_polls", 0) < 20:
        return "Authority fault or continuing client poll evidence missing"
    for proof in (fault, stopped):
        if any(
            proof.get(k) != v
            for k, v in {
                "server_state": "Stopped",
                "client_state": "Stopped",
                "server_tick": 0,
                "peers": 0,
                "entities": 0,
                "epoch": 1,
                "cpp_hits": 1,
                "cs_hits": 1,
            }.items()
        ):
            return "Fault caches were retained or reload implicitly restarted networking"
        if proof.get("diagnostics") != ["Fixed simulation exceeded its catch-up budget; resynchronization required."]:
            return "Fixed-clock diagnostic missing or duplicated"
    for proof, epoch in ((initial, 1), (recovered, 2)):
        if (
            any(
                proof.get(k) != v
                for k, v in {
                    "server_state": "Listening",
                    "client_state": "Connected",
                    "peers": 1,
                    "entities": 1,
                    "epoch": epoch,
                    "cpp_hits": epoch,
                    "cs_hits": epoch,
                }.items()
            )
            or proof.get("server_tick", 0) < 8
        ):
            return "Fresh admission, replication or callback checks missing"
        expected = [{"peer": initial["peer"], "payload": "0100ff2a"}]
        if epoch == 2:
            expected.append({"peer": recovered["peer"], "payload": "0200ff2a"})
        if proof.get("packets") != expected:
            return "Application data lost, duplicated or assigned to a retired peer"
    if recovered.get("peer", 0) <= initial.get("peer", 0) or recovered.get("entity", 0) <= initial.get("entity", 0):
        return "Recovery reused retired handles"
    if recovered.get("old_peer") != initial["peer"] or recovered.get("old_entity") != initial["entity"]:
        return "Recovery lost original handle provenance"
    if recovered.get("retired_peer_error") != 33 or recovered.get("retired_entity_error") != 33:
        return "Retired handles remain usable"
    expected_states = [
        "Connecting",
        "Synchronizing",
        "Connected",
        "Stopped",
        "Disconnected",
        "Stopped",
        "Connecting",
        "Synchronizing",
        "Connected",
    ]
    if recovered.get("states") != expected_states:
        return "Unexpected native client recovery lifecycle"
    return None


def network_live_failure(proofs, simulation):
    """Require one uninterrupted admission across compiler failures and reloads."""
    if len(proofs) != 6 or [p.get("action") for p in proofs] != ["network-live-start"] + ["network-live-check"] * 5:
        return "Missing live reload checkpoints"
    initial = proofs[0]
    if int(initial.get("pid", 0)) <= 0 or not 0 < initial.get("port", 0) <= 65535:
        return "Invalid process or listener identity"
    if initial.get("server_id") == initial.get("client_id") or any(
        int(initial.get(k, 0)) == 0 for k in ("server_id", "client_id", "peer", "entity")
    ):
        return "Invalid session, peer or entity identity"
    for index, proof in enumerate(proofs):
        sequence = index + 1
        if not proof.get("passed") or not proof.get("references_ok"):
            return "Live runtime or serialized references failed"
        if any(proof.get(k) != initial.get(k) for k in ("pid", "server_id", "client_id", "port", "peer", "entity")):
            return "Active reload replaced a process, session, admission or entity"
        if proof.get("simulation") != simulation:
            return "Network impairment configuration missing or changed"
        expected = {
            "server_state": "Listening",
            "client_state": "Connected",
            "epoch": 1,
            "peers": 1,
            "entities": 1,
            "sequence": sequence,
            "cpp_hits": sequence,
            "cs_hits": sequence,
            "diagnostics": [],
            "states": ["Connecting", "Synchronizing", "Connected"],
            "baseline_hex": bytes([sequence, 0, 255, 42]).hex(),
        }
        if any(proof.get(k) != value for k, value in expected.items()):
            return "Active reload interrupted lifecycle, callbacks or replication"
        packets = [{"peer": initial["peer"], "payload": bytes([n, 0, 255, 42]).hex()} for n in range(1, sequence + 1)]
        replies = [{"peer": 0, "payload": bytes([128 + n, 0, 255, 42]).hex()} for n in range(1, sequence + 1)]
        if proof.get("packets") != packets or proof.get("client_packets") != replies:
            return "Bidirectional application data lost, duplicated or corrupt"
        if any(proof.get(k, 0) <= 0 for k in ("server_tick", "revision", "total_client_polls")):
            return "Live simulation, entity revision or language polling evidence missing"
        if index and any(proof[k] <= proofs[index - 1][k] for k in ("server_tick", "revision", "total_client_polls")):
            return "Live simulation, replication or language pumps did not advance"
    return None


def network_physics_failure(proofs, live):
    """Validate authoritative world references, replication and explicit rollback."""
    if len(proofs) != (6 if live else 4):
        return "Missing physics reload checkpoints"
    initial = proofs[0].get("physics", {})
    if not initial.get("world_id") or int(initial["world_id"]) == 0:
        return "Missing authoritative world identity"
    for proof in proofs:
        physics = proof.get("physics", {})
        if any(
            physics.get(k) != value
            for k, value in {
                "enabled": True,
                "world_id": initial["world_id"],
                "body_id": 10000,
                "body_count": 1,
                "cpp_state_ok": True,
                "cs_state_ok": True,
                "fingerprint": initial.get("fingerprint"),
            }.items()
        ):
            return "Physics identity, body mapping or language state changed"
        if ":hz60:" not in physics.get("fingerprint", "") or not re.fullmatch(r"[0-9a-f]{16}", physics.get("hash", "")):
            return "Physics profile/hash evidence missing"
        if physics.get("tick", 0) <= 0 or physics["tick"] != physics.get("clock_offset", -1) + proof.get(
            "server_tick", -1
        ):
            return "Physics lost authoritative fixed-clock offset"
        if any(
            not isinstance(physics.get(k), (float, int)) or not math.isfinite(physics[k])
            for k in ("position_y", "velocity_y", "client_position_y")
        ):
            return "Nonfinite physics state"
        if physics["velocity_y"] >= 0:
            return "Gravity-driven body stopped advancing"
        if proof.get("client_state") == "Connected":
            if (
                physics.get("client_body_id") != 10000
                or not physics.get("clock_offset", 0) < physics.get("client_tick", 0) <= physics["tick"]
            ):
                return "Replicated body or physics tick missing/stale"
            if physics["client_position_y"] < physics["position_y"]:
                return "Client physics baseline is ahead of authority"
    if live:
        for previous, current in zip(proofs, proofs[1:]):
            before, after = previous["physics"], current["physics"]
            if (
                after["clock_offset"] != 0
                or after["tick"] <= before["tick"]
                or after["client_tick"] <= before["client_tick"]
                or after["position_y"] >= before["position_y"]
            ):
                return "Live physics reset or stopped through language reload"
    else:
        fault, stopped, recovered = proofs[1:]
        saved = fault["physics"]
        for proof in (fault, stopped):
            physics = proof["physics"]
            if (
                physics.get("client_tick") != 0
                or physics.get("checkpoint_tick") != physics["tick"]
                or physics.get("checkpoint_hash") != physics["hash"]
            ):
                return "Stopped physics checkpoint or cleared client baseline missing"
            if any(
                physics.get(k) != saved.get(k)
                for k in ("tick", "hash", "position_y", "checkpoint_tick", "checkpoint_hash")
            ):
                return "Stopped reload changed preserved checkpoint state"
        if recovered.get("corrupt_snapshot_error") != 16 or not recovered.get("corrupt_restore_unchanged"):
            return "Corrupt checkpoint was accepted or modified live state"
        if (
            recovered.get("restored_tick") != saved["tick"]
            or recovered.get("restored_hash") != saved["hash"]
            or recovered.get("restored_y") != saved["position_y"]
        ):
            return "Trusted checkpoint restore lost exact solver state"
        after = recovered["physics"]
        if (
            after["clock_offset"] != saved["tick"]
            or after["client_tick"] <= saved["tick"]
            or after["position_y"] >= saved["position_y"]
        ):
            return "Recovered baseline did not resume stable body from checkpoint"
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--packages", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect-disabled", action="store_true", help="Record the pre-fix opt-in rejection")
    parser.add_argument(
        "--network-csharp-facade",
        action="store_true",
        help="Transfer public NetSession ownership and reconnect managed events across assembly reload",
    )
    parser.add_argument(
        "--network-physics",
        action="store_true",
        help="Retain an authoritative Box3D world during network reload; restore a trusted checkpoint in fault recovery",
    )
    parser.add_argument("--disable-runtime", action="store_true", help="Verify the default non-collectible player")
    parser.add_argument(
        "--network-live-reload",
        action="store_true",
        help="Keep one authenticated client connected across failed builds and live C#/C++ reloads",
    )
    parser.add_argument(
        "--network-latency-ms",
        type=float,
        default=30,
        help="Live fixture outbound latency in each direction (default: 30 ms)",
    )
    parser.add_argument(
        "--network-jitter-ms",
        type=float,
        default=5,
        help="Live fixture outbound jitter in each direction (default: 5 ms)",
    )
    parser.add_argument(
        "--network-loss-percent",
        type=float,
        default=5,
        help="Live fixture configured packet loss in each direction (default: 5 percent)",
    )
    parser.add_argument(
        "--network-recovery",
        action="store_true",
        help="Retain native sessions through C++/C# reload after an authority clock fault",
    )
    parser.add_argument(
        "--feature-override", action="store_true", help="Enable runtime reload through the editor feature override"
    )
    parser.add_argument(
        "--assembly-recovery", action="store_true", help="Also reject and recover a corrupted managed assembly"
    )
    parser.add_argument(
        "--unload-recovery",
        action="store_true",
        help="Also recover after a live application thread prevents assembly unload",
    )
    parser.add_argument(
        "--native-recovery", action="store_true", help="Also recover a missing and invalid native library"
    )
    parser.add_argument(
        "--native-abi-recovery",
        action="store_true",
        help="Exercise changed method signatures and rejected base-class repair",
    )
    args = parser.parse_args()
    if args.disable_runtime and args.feature_override:
        parser.error("--disable-runtime and --feature-override are mutually exclusive")
    if args.native_abi_recovery and (args.disable_runtime or args.expect_disabled):
        parser.error("--native-abi-recovery requires runtime reload")
    if args.network_recovery and (args.disable_runtime or args.expect_disabled):
        parser.error("--network-recovery requires runtime reload")
    if args.network_live_reload and (args.disable_runtime or args.expect_disabled):
        parser.error("--network-live-reload requires runtime reload")
    if args.network_live_reload and args.network_recovery:
        parser.error("--network-live-reload and --network-recovery require separate isolated fixtures")
    values = (args.network_latency_ms, args.network_jitter_ms, args.network_loss_percent)
    if not all(math.isfinite(value) and 0 <= value <= maximum for value, maximum in zip(values, (5000, 5000, 100))):
        parser.error("network simulation requires finite latency/jitter in [0, 5000] ms and loss in [0, 100] percent")
    if not args.network_live_reload and values != (30, 5, 5):
        parser.error("network simulation options require --network-live-reload")
    network_enabled = args.network_recovery or args.network_live_reload
    if args.network_physics and not network_enabled:
        parser.error("--network-physics requires --network-live-reload or --network-recovery")
    if args.network_csharp_facade and not network_enabled:
        parser.error("--network-csharp-facade requires --network-live-reload or --network-recovery")
    probe_source = PROBE
    if args.network_csharp_facade:
        probe_source = probe_source.replace("    [Signal]", FACADE_MEMBERS + "    [Signal]")
        probe_source = probe_source.replace(
            '        var server = NetworkState["server"].AsGodotObject();',
            "        if (facadeServer != null && facadeClient != null) { "
            "var first = authority ? facadeServer.Poll() : Error.Ok; var second = facadeClient.Poll(); "
            "return first != Error.Ok ? first : second; }\n"
            '        var server = NetworkState["server"].AsGodotObject();',
        )
        probe_source = probe_source.replace(
            "BeforeCount++;",
            "BeforeCount++; if (facadeServer != null && facadeClient != null) { "
            'ReloadCapsules = new() { ["server"] = facadeServer.DetachForReload(), ["client"] = facadeClient.DetachForReload() }; '
            "facadeServer = null; facadeClient = null; FacadeHandoffs++; }",
        ).replace(
            "AfterCount++;",
            "AfterCount++; if (ReloadCapsules.Count != 0) { "
            'facadeServer = EGP.Networking.NetSession.ResumeAfterReload(ReloadCapsules["server"].AsGodotDictionary()); '
            'facadeClient = EGP.Networking.NetSession.ResumeAfterReload(ReloadCapsules["client"].AsGodotDictionary()); '
            "ReloadCapsules.Clear(); SubscribeFacades(); FacadeRestores++; }",
        )
    output = args.output.resolve() / str(time.time_ns())
    project = output / "project"
    addon = project / "addons/reload_fixture"
    addon.mkdir(parents=True)
    engine = args.engine.resolve()
    env = os.environ.copy()
    env["NUGET_PACKAGES"] = str(output / "nuget-packages")
    options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    receipt = {"passed": False, "engine": str(engine), "engine_sha256": digest(engine), "steps": [], "samples": []}
    fixture_paths = [
        ROOT / "misc/scripts" / name
        for name in (
            "egp_hot_reload_game.gd",
            "egp_hot_reload_editor.gd",
            "egp_hot_reload_network.gd",
            "validate_egp_hot_reload.py",
        )
    ]
    receipt["source_commit"] = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    receipt["fixture_sha256"] = {str(p.relative_to(ROOT)): digest(p) for p in fixture_paths}
    if args.network_csharp_facade:
        for name in ("NetApi.cs", "NetSessionSignals.cs"):
            helper = ROOT / "modules/egp_net/csharp" / name
            receipt["fixture_sha256"][str(helper.relative_to(ROOT))] = digest(helper)
    runtime_files = [engine.parent / "GodotSharp/Api/Debug" / name for name in ("GodotSharp.dll", "GodotPlugins.dll")]
    receipt["managed_runtime_sha256"] = {str(path): digest(path) for path in runtime_files}
    receipt["scope"] = (
        "Headless editor and separate running game; live objects, properties, callables, signals, compile recovery. No exported-template or arbitrary-ABI-change claim."
    )
    process = None
    stream = None
    request_id = 0

    def save():
        (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")

    def require(value, message):
        if not value:
            raise RuntimeError(message)

    def run(name, command, expected_success=True, timeout=300):
        command = [str(x) for x in command]
        start = time.monotonic()
        with (output / (name + ".log")).open("w", encoding="utf-8") as log:
            child = subprocess.Popen(command, cwd=project, env=env, stdout=log, stderr=subprocess.STDOUT, **options)
            try:
                code = child.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                terminate(child)
                raise RuntimeError(name + " watchdog expired")
        passed = (code == 0) == expected_success
        receipt["steps"].append({
            "name": name,
            "command": command,
            "exit_code": code,
            "pid": child.pid,
            "log": str(output / (name + ".log")),
            "passed": passed,
            "elapsed_seconds": round(time.monotonic() - start, 3),
        })
        save()
        require(passed, name + " failed")
        print(name, "PASS", flush=True)

    def terminate(child):
        if child.poll() is None:
            if os.name == "nt":
                subprocess.run(["taskkill", "/PID", str(child.pid), "/T", "/F"], capture_output=True)
            else:
                child.kill()
            child.wait(timeout=15)

    def wait_json(path, predicate, timeout):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            require(process.poll() is None, "Editor exited unexpectedly; see editor.log")
            try:
                data = json.loads(path.read_text(encoding="utf-8"))
                if predicate(data):
                    return data
            except (OSError, ValueError):
                pass
            time.sleep(0.05)
        raise RuntimeError("Watchdog expired waiting for " + path.name)

    def command(action, timeout=30):
        nonlocal request_id
        request_id += 1
        # Replace atomically so the editor never parses a partial JSON document.
        temporary = project / "command.tmp"
        temporary.write_text(json.dumps({"id": request_id, "action": action}), encoding="utf-8")
        deadline = time.monotonic() + 2
        while True:
            try:
                temporary.replace(project / "command.json")
                break
            except PermissionError:
                # Windows readers may briefly hold a file without delete sharing.
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
        response = wait_json(project / "response.json", lambda x: x["id"] == request_id, timeout)
        require(response["passed"], action + " command failed")
        return response

    def sample(action="sample"):
        command(action)
        state = wait_json(project / "sample.json", lambda x: x["request"] == request_id, 15)
        receipt["samples"].append(state)
        save()
        return state

    def verify(state, version, previous, cs_version=None):
        cs_version = version if cs_version is None else cs_version
        require(not state.get("native_unavailable"), "Compatible native repair did not restore the live class")
        require(not state["editor_hint"], "Fixture is an editor tool rather than a running game")
        require(int(state["pid"]) != process.pid, "Game was not launched as a separate process")
        require(state["collectible"], "Running-game project assembly is not collectible")
        for key in ("cpp_counter", "cs_counter", "ready_count"):
            require(state[key] == {"cpp_counter": 91, "cs_counter": 87, "ready_count": 1}[key], key + " changed")
        require(all(state[key] for key in ("vector_ok", "reference_ok", "parents_ok")), "Live state or parent lost")
        require(
            state["cpp_version"] == str(version) and state["cpp_callable"] == str(version),
            "C++ method/cached callable retained old code",
        )
        require(
            state["cs_version"] == cs_version and state["cs_callable"] == cs_version,
            "C# method/cached callable retained old code",
        )
        require(
            state["cpp_hits"] == state["cs_hits"] == state["receiver_hits"],
            "Signal/delegate subscription lost or duplicated",
        )
        if previous:
            require(
                all(state[key] == previous[key] for key in ("cpp_id", "cs_id", "receiver_id")),
                "Live native identity changed",
            )
            require(state["cpp_hits"] == previous["cpp_hits"] + 1, "Signal callback count changed")
            require(
                state["before_count"] >= previous["before_count"] and state["after_count"] >= previous["after_count"],
                "Serialization callback state lost",
            )

    try:
        (project / "project.godot").write_text(
            'config_version=5\n[application]\nconfig/name="ReloadFixture"\nrun/main_scene="res://main.tscn"\n[dotnet]\nproject/assembly_name="ReloadFixture"\n[debug]\nhot_reload/enable_runtime='
            + (
                "false\nhot_reload/enable_runtime.editor=true"
                if args.feature_override
                else "false"
                if args.disable_runtime
                else "true"
            )
            + "\n[editor]\nrun/main_run_args="
            + json.dumps(
                f'--headless --max-fps 60 --ignore-error-breaks --log-file "{(output / "game.log").as_posix()}"'
            )
            + '\n[editor_plugins]\nenabled=PackedStringArray("res://addons/reload_fixture/plugin.cfg")\n',
            encoding="utf-8",
        )
        (project / "main.tscn").write_text(
            '[gd_scene load_steps=2 format=3]\n[ext_resource type="Script" path="res://main.gd" id="1"]\n[node name="Fixture" type="Node"]\nscript=ExtResource("1")\n',
            encoding="utf-8",
        )
        shutil.copyfile(ROOT / "misc/scripts/egp_hot_reload_game.gd", project / "main.gd")
        shutil.copyfile(ROOT / "misc/scripts/egp_hot_reload_editor.gd", addon / "plugin.gd")
        if network_enabled:
            shutil.copyfile(ROOT / "misc/scripts/egp_hot_reload_network.gd", project / "network.gd")
        if args.network_physics:
            (project / "physics_enabled").write_text("enabled", encoding="utf-8")
        if args.network_csharp_facade:
            for name in ("NetApi.cs", "NetSessionSignals.cs"):
                shutil.copyfile(ROOT / "modules/egp_net/csharp" / name, project / name)
        simulation = {"simulated_latency_ms": values[0], "simulated_jitter_ms": values[1], "simulated_loss": values[2]}
        if args.network_live_reload:
            (project / "network_options.json").write_text(json.dumps(simulation), encoding="utf-8")
            receipt["network_simulation"] = simulation
        (addon / "plugin.cfg").write_text(
            '[plugin]\nname="ReloadFixture"\ndescription="Isolated reload fixture"\nauthor="EGP"\nversion="1"\nscript="plugin.gd"\n',
            encoding="utf-8",
        )
        (project / "ReloadFixture.csproj").write_text(
            '<Project Sdk="Godot.NET.Sdk/4.8.0-dev"><PropertyGroup><TargetFramework>net10.0</TargetFramework><EnableDynamicLoading>true</EnableDynamicLoading><Nullable>enable</Nullable></PropertyGroup></Project>\n',
            encoding="utf-8",
        )
        (project / "NuGet.Config").write_text(
            f'<configuration><packageSources><clear/><add key="egp" value="{escape(str(args.packages.resolve()))}"/><add key="nuget" value="https://api.nuget.org/v3/index.json"/></packageSources></configuration>\n',
            encoding="utf-8",
        )
        (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", "1"), encoding="utf-8")
        (project / "ReloadReceiver.cs").write_text(RECEIVER, encoding="utf-8")
        run("managed-initial", ["dotnet", "build", "--nologo", "-v", "minimal"])
        editor_command = [str(engine), "--headless", "--editor", "--path", str(project), "--max-fps", "30"]
        receipt["editor_command"] = editor_command
        stream = (output / "editor.log").open("w", encoding="utf-8")
        process = subprocess.Popen(editor_command, env=env, stdout=stream, stderr=subprocess.STDOUT, **options)
        receipt["editor_pid"] = process.pid
        command("prepare", 120)
        source_path = project / "extensions/reload/src/extension.cpp"
        source = source_path.read_text(encoding="utf-8")
        source = source.replace(
            'ClassDB::bind_method(D_METHOD("get_message"), &EGP_reload_Node::get_message);',
            'ClassDB::bind_method(D_METHOD("get_message"), &EGP_reload_Node::get_message);\n\t\tClassDB::bind_method(D_METHOD("set_counter", "value"), &EGP_reload_Node::set_counter);\n\t\tClassDB::bind_method(D_METHOD("get_counter"), &EGP_reload_Node::get_counter);\n\t\tADD_PROPERTY(PropertyInfo(Variant::INT, "counter"), "set_counter", "get_counter");\n\t\tADD_SIGNAL(MethodInfo("pulse", PropertyInfo(Variant::INT, "value")));',
        )
        source = source.replace(
            "public:\n",
            "public:\n\tint counter = 1;\n\tvoid set_counter(int value) { counter = value; }\n\tint get_counter() const { return counter; }\n",
        )
        require("Hello from reload!" in source, "Unexpected scaffold template")
        source = source.replace("Hello from reload!", "VERSION")
        if network_enabled:
            source = (
                "#include <godot_cpp/classes/egp_net_session.hpp>\n#include <godot_cpp/classes/egp_box3d_world.hpp>\n"
                + source
            )
            source = source.replace(
                'ClassDB::bind_method(D_METHOD("get_message"), &EGP_reload_Node::get_message);',
                'ClassDB::bind_method(D_METHOD("get_message"), &EGP_reload_Node::get_message);\n'
                '\t\tClassDB::bind_method(D_METHOD("set_network_state", "value"), &EGP_reload_Node::set_network_state);\n'
                '\t\tClassDB::bind_method(D_METHOD("get_network_state"), &EGP_reload_Node::get_network_state);\n'
                '\t\tADD_PROPERTY(PropertyInfo(Variant::DICTIONARY, "network_state"), "set_network_state", "get_network_state");\n'
                '\t\tClassDB::bind_method(D_METHOD("set_network_hits", "value"), &EGP_reload_Node::set_network_hits);\n'
                '\t\tClassDB::bind_method(D_METHOD("get_network_hits"), &EGP_reload_Node::get_network_hits);\n'
                '\t\tADD_PROPERTY(PropertyInfo(Variant::INT, "network_hits"), "set_network_hits", "get_network_hits");\n'
                '\t\tClassDB::bind_method(D_METHOD("poll_network", "authority"), &EGP_reload_Node::poll_network);\n'
                '\t\tClassDB::bind_method(D_METHOD("get_physics_state"), &EGP_reload_Node::get_physics_state);\n'
                '\t\tClassDB::bind_method(D_METHOD("receive_network", "peer", "payload"), &EGP_reload_Node::receive_network);',
            ).replace(
                "public:\n",
                "public:\n"
                "\tDictionary network_state;\n\tint network_hits = 0;\n"
                "\tvoid set_network_state(const Dictionary &value) { network_state = value; }\n"
                "\tDictionary get_network_state() const { return network_state; }\n"
                "\tvoid set_network_hits(int value) { network_hits = value; }\n"
                "\tint get_network_hits() const { return network_hits; }\n"
                "\tDictionary get_physics_state() const {\n"
                '\t\tif (!network_state.has("world")) return Dictionary();\n'
                '\t\tRef<EGPBox3DWorld> world = network_state["world"];\n'
                "\t\tif (world.is_null()) return Dictionary();\n"
                "\t\tDictionary state = world->get_body_state(10000);\n"
                '\t\tstate["tick"] = world->get_tick(); state["hash"] = world->get_state_hash();\n'
                "\t\treturn state;\n\t}\n"
                "\tvoid receive_network(int64_t peer, const PackedByteArray &payload) { network_hits++; }\n"
                "\tError poll_network(bool authority) {\n"
                '\t\tRef<EGPNetSession> server = network_state["server"];\n'
                '\t\tRef<EGPNetSession> client = network_state["client"];\n'
                "\t\tError result = authority ? server->poll() : OK;\n"
                "\t\tError client_result = client->poll();\n"
                "\t\treturn result != OK ? result : client_result;\n\t}\n",
            )
        if args.native_abi_recovery:
            source = (
                source
                .replace("classes/node.hpp", "classes/node2d.hpp")
                .replace(
                    "class EGP_reload_Node : public Node {", "class EGP_reload_Node : public EGP_reload_Ancestor {"
                )
                .replace("GDCLASS(EGP_reload_Node, Node)", "GDCLASS(EGP_reload_Node, EGP_reload_Ancestor)")
                .replace(
                    "class EGP_reload_Node :",
                    "class EGP_reload_Ancestor : public Node {\n"
                    "    GDCLASS(EGP_reload_Ancestor, Node);\n"
                    "protected:\n    static void _bind_methods() {}\n};\n\n"
                    "class EGP_reload_Parent : public Node2D {\n"
                    "    GDCLASS(EGP_reload_Parent, Node2D);\n"
                    "protected:\n    static void _bind_methods() {}\n};\n\nclass EGP_reload_Node :",
                )
                .replace(
                    "GDREGISTER_CLASS(EGP_reload_Node);",
                    "GDREGISTER_CLASS(EGP_reload_Ancestor);\n        GDREGISTER_CLASS(EGP_reload_Parent);\n        GDREGISTER_CLASS(EGP_reload_Node);",
                )
            )
        source_path.write_text(source.replace("VERSION", "1"), encoding="utf-8")
        require(command("build", 900)["build_result"] == 0, "Initial C++ panel build failed")
        command("play", 120)
        # Wait for the remote debugger to attach before sending a sample.
        time.sleep(3)
        initial = sample()
        if args.expect_disabled or args.disable_runtime:
            require(not initial["editor_hint"] and not initial["collectible"], "Expected non-collectible baseline")
            receipt["disabled_baseline"] = True
        else:
            verify(initial, 1, None)
            previous = initial
            descriptor = project / "extensions/reload/reload.gdextension"
            for version in (2, 3):
                before = digest(descriptor)
                if version == 2:
                    source_path.write_text(
                        source.replace("VERSION", "2") + "\n#error EGP deliberate runtime diagnostic\n",
                        encoding="utf-8",
                    )
                    require(command("build", 900)["build_result"] != 0, "Invalid C++ unexpectedly compiled")
                    require(digest(descriptor) == before, "Failed C++ build published a descriptor")
                    failed = sample()
                    verify(failed, 1, previous)
                    previous = failed
                    (project / "ReloadProbe.cs").write_text(
                        probe_source.replace("VERSION", "invalid!"), encoding="utf-8"
                    )
                    run("managed-invalid", ["dotnet", "build", "--nologo", "-v", "minimal"], expected_success=False)
                    failed = sample()
                    verify(failed, 1, previous)
                    previous = failed
                (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", str(version)), encoding="utf-8")
                run("managed-version-" + str(version), ["dotnet", "build", "--nologo", "-v", "minimal"])
                source_path.write_text(source.replace("VERSION", str(version)), encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "C++ recovery build failed")
                # C++ publication broadcasts reload to each running debugger session.
                time.sleep(2)
                state = sample()
                verify(state, version, previous)
                require(
                    state["before_count"] > previous["before_count"] and state["after_count"] > previous["after_count"],
                    "C# lifecycle hooks did not run",
                )
                previous = state
            receipt["reloads"] = 2
            # Exercise the same debugger command after a C#-only rebuild.
            (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", "4"), encoding="utf-8")
            run("managed-version-4", ["dotnet", "build", "--nologo", "-v", "minimal"])
            command("reload")
            time.sleep(2)
            state = sample()
            verify(state, 3, previous, cs_version=4)
            require(state["after_count"] > previous["after_count"], "C#-only reload did not deserialize")
            previous = state
            command("reload")
            time.sleep(1)
            state = sample()
            verify(state, 3, previous, cs_version=4)
            require(state["after_count"] == previous["after_count"], "No-change command reloaded the assembly again")
            previous = state
            if args.assembly_recovery:
                assembly = project / ".godot/mono/temp/bin/Debug/ReloadFixture.dll"
                backup = assembly.read_bytes()
                time.sleep(1.1)
                assembly.write_bytes(b"EGP deliberately invalid managed assembly")
                command("reload")
                time.sleep(2)
                fallback = sample()
                require(
                    fallback.get("placeholder")
                    and fallback["cs_id"] == previous["cs_id"]
                    and fallback["cs_counter"] == 87,
                    "Failed load lost placeholder identity or state",
                )
                command("drop")
                fallback = sample()
                require(
                    fallback.get("placeholder") and not fallback["extra_alive"], "Deleted placeholder remained alive"
                )
                time.sleep(1.1)
                assembly.write_bytes(backup)
                command("reload")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=4)
                receipt["assembly_recovery"] = True
                previous = state
            if args.unload_recovery:
                command("hold")
                deadline = time.monotonic() + 10
                while not (project / "release-root.started").exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                require((project / "release-root.started").exists(), "Managed application thread did not start")
                (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", "5"), encoding="utf-8")
                run("managed-version-5", ["dotnet", "build", "--nologo", "-v", "minimal"])
                command("reload")
                time.sleep(2)
                fallback = sample()
                require(
                    fallback.get("placeholder") and fallback["cs_counter"] == 87,
                    "Unload failure lost placeholder state",
                )
                (project / "release-root").write_text("release", encoding="utf-8")
                deadline = time.monotonic() + 10
                while not (project / "release-root.finished").exists() and time.monotonic() < deadline:
                    time.sleep(0.05)
                require((project / "release-root.finished").exists(), "Managed application thread did not stop")
                command("reload")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5)
                diagnostics = command("diagnostics")["diagnostics"]
                receipt["debugger_diagnostics"] = diagnostics
                require(
                    any(".NET: Failed to unload assemblies." in text for text in diagnostics),
                    "Unload failure debugger diagnostic missing",
                )
                receipt["unload_recovery"] = True
                previous = state
            if args.native_recovery:
                descriptor_text = descriptor.read_text(encoding="utf-8")
                bad_library = project / "extensions/reload/bin/invalid-native.dll"
                invalid_descriptor = re.sub(
                    r'(windows\.debug\.x86_64\s*=\s*)"[^"]+"',
                    r'\1"res://extensions/reload/bin/invalid-native.dll"',
                    descriptor_text,
                )
                require(invalid_descriptor != descriptor_text, "Missing Windows Debug library mapping")
                for fault in ("missing", "invalid"):
                    if fault == "invalid":
                        bad_library.write_bytes(b"EGP deliberately invalid native library")
                    time.sleep(1.1)
                    descriptor.write_text(invalid_descriptor, encoding="utf-8")
                    command("reload")
                    time.sleep(2)
                    fallback = sample()
                    require(
                        fallback.get("native_unavailable")
                        and fallback["cpp_id"] == previous["cpp_id"]
                        and fallback["parent_ok"],
                        "Failed native load lost parent object identity",
                    )
                    if fault == "missing":
                        command("rename-native")
                time.sleep(1.1)
                descriptor.write_text(descriptor_text, encoding="utf-8")
                command("reload")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                require(state["cpp_name"] == "RecoveredNative", "Parent property edit during failure was lost")
                diagnostics = command("diagnostics")["diagnostics"]
                receipt["native_recovery_diagnostics"] = diagnostics
                require(
                    any("GDExtension" in text or "dynamic library" in text for text in diagnostics),
                    "Native failed-load diagnostic missing from debugger",
                )
                receipt["native_recovery"] = True
                previous = state
            if args.native_abi_recovery:
                original = source.replace("VERSION", "3")
                for return_type, result in (("String", '"7"'), ("int", "7")):
                    changed = original.replace('D_METHOD("get_message")', 'D_METHOD("get_message", "value")').replace(
                        'String get_message() const { return "3"; }',
                        f"{return_type} get_message(int value) const {{ return {result}; }}",
                    )
                    require(changed != original, "Unexpected native method scaffold")
                    source_path.write_text(changed, encoding="utf-8")
                    require(command("build", 900)["build_result"] == 0, "Changed-signature build failed")
                    time.sleep(2)
                    changed_state = sample("sample-abi")
                    expected = "7" if return_type == "String" else 7
                    require(
                        changed_state["cpp_version"] == expected
                        and changed_state["cpp_callable"] == expected
                        and changed_state["cpp_counter"] == 91
                        and changed_state["cpp_id"] == previous["cpp_id"]
                        and changed_state["parent_ok"],
                        "Changed-signature dynamic call lost code, state or identity",
                    )
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Original-signature repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                previous = state
                changed_base = (
                    original
                    .replace("classes/node.hpp", "classes/node2d.hpp")
                    .replace(
                        "class EGP_reload_Node : public EGP_reload_Ancestor {",
                        "class EGP_reload_Node : public Node2D {",
                    )
                    .replace("GDCLASS(EGP_reload_Node, EGP_reload_Ancestor)", "GDCLASS(EGP_reload_Node, Node2D)")
                )
                require(changed_base != original, "Unexpected native base scaffold")
                source_path.write_text(changed_base, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Changed-base build failed")
                time.sleep(2)
                # The editor withholds automatic game notification on NEEDS_RESTART.
                # Force this unsafe change in the fixture to exercise game recovery.
                require(sample("reload-native")["status"] == 4, "Changed-base reload did not report NEEDS_RESTART")
                fallback = sample()
                require(
                    fallback.get("native_unavailable")
                    and fallback["cpp_id"] == previous["cpp_id"]
                    and fallback["base_class"] == "Node"
                    and fallback["parent_ok"],
                    "Rejected native base change lost parent identity",
                )
                diagnostics = command("diagnostics")["diagnostics"]
                require(
                    any("cannot change parent type" in text and "Restart Godot" in text for text in diagnostics),
                    "Changed-base restart diagnostic missing",
                )
                require(sample("reload-native")["status"] == 4, "Rejected-base retry did not report NEEDS_RESTART")
                require(
                    any("changed signature" in text and "Cached method bindings" in text for text in diagnostics),
                    "Changed-signature cached-binding diagnostic missing",
                )
                command("rename-native")
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Original-base repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                require(state["cpp_name"] == "RecoveredNative", "Rejected-base parent property edit was lost")
                previous = state
                changed_parent = original.replace(
                    "class EGP_reload_Node : public EGP_reload_Ancestor {",
                    "class EGP_reload_Node : public EGP_reload_Parent {",
                ).replace(
                    "GDCLASS(EGP_reload_Node, EGP_reload_Ancestor)", "GDCLASS(EGP_reload_Node, EGP_reload_Parent)"
                )
                require(changed_parent != original, "Unexpected extension-parent scaffold")
                source_path.write_text(changed_parent, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Changed extension-parent build failed")
                time.sleep(2)
                require(sample("reload-native")["status"] == 4, "Extension-parent change bypassed rejection")
                fallback = sample()
                require(
                    fallback.get("native_unavailable")
                    and fallback["cpp_id"] == previous["cpp_id"]
                    and fallback["base_class"] == "Node"
                    and fallback["parent_ok"],
                    "Rejected extension-parent change lost original native parent",
                )
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Extension-parent repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                previous = state
                changed_ancestor = original.replace(
                    "class EGP_reload_Ancestor : public Node {", "class EGP_reload_Ancestor : public Node2D {"
                ).replace("GDCLASS(EGP_reload_Ancestor, Node)", "GDCLASS(EGP_reload_Ancestor, Node2D)")
                require(changed_ancestor != original, "Unexpected extension ancestor scaffold")
                source_path.write_text(changed_ancestor, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Changed ancestor build failed")
                time.sleep(2)
                require(sample("reload-native")["status"] == 4, "Rejected ancestor did not block descendant reload")
                fallback = sample()
                require(
                    fallback.get("native_unavailable")
                    and fallback["cpp_id"] == previous["cpp_id"]
                    and fallback["base_class"] == "Node"
                    and fallback["parent_ok"],
                    "Rejected ancestor lost descendant's native parent",
                )
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Ancestor repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                previous = state
                # Removing a class with a live object must also allow restoring it.
                removed_class = original.replace("GDREGISTER_CLASS(EGP_reload_Node);", "/* class removed */")
                require(removed_class != original, "Unexpected class registration scaffold")
                source_path.write_text(removed_class, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Removed-class build failed")
                time.sleep(2)
                require(sample("reload-native")["status"] == 4, "Removed-class reload did not report NEEDS_RESTART")
                fallback = sample()
                require(
                    fallback.get("native_unavailable")
                    and fallback["cpp_id"] == previous["cpp_id"]
                    and fallback["parent_ok"],
                    "Removed class lost native parent identity",
                )
                require(sample("reload-native")["status"] == 4, "Removed-class retry did not report NEEDS_RESTART")
                source_path.write_text(original, encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Removed-class repair build failed")
                time.sleep(2)
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                require(sample("reload-native")["status"] == 0, "Compatible native retry did not report OK")
                previous = state
                state = sample()
                verify(state, 3, previous, cs_version=5 if args.unload_recovery else 4)
                previous = state
                receipt["native_abi_recovery"] = {
                    "passed": True,
                    "native_builds": 11,
                    "rejected_explicit_retries": 2,
                    "method_changes": ["argument-count", "return-type"],
                    "base_change": "Node to Node2D rejected; Node repair retains state and identity",
                    "extension_parent_change": "Node to extension-derived Node2D rejected; compatible repair retains state",
                    "ancestor_change": "Rejected ancestor blocks descendant registration; compatible repair retains state",
                    "class_removal": "Live parent and state retained until original class is restored",
                    "diagnostics": diagnostics,
                    "scope": "Dynamic methods and Callable lookup; cached raw MethodBind pointers and arbitrary ABI changes remain open",
                }
            receipt["reloads"] = 3 + int(args.assembly_recovery) + int(args.unload_recovery) + int(args.native_recovery)
            if args.network_recovery:
                proofs = [sample("network-start"), sample("network-fault")]
                require(all(p["passed"] for p in proofs), "Network fault setup failed")
                # Rebuild both live language objects while the authority remains stopped.
                # Only a later explicit fresh-token admission may restart it.
                (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", "6"), encoding="utf-8")
                run("managed-network-recovery", ["dotnet", "build", "--nologo", "-v", "minimal"])
                source_path.write_text(source.replace("VERSION", "4"), encoding="utf-8")
                require(command("build", 900)["build_result"] == 0, "Network fault reload build failed")
                time.sleep(2)
                state = sample()
                verify(state, 4, previous, cs_version=6)
                require(
                    state["after_count"] > previous["after_count"], "Network fault reload skipped managed lifecycle"
                )
                proofs.extend([sample("network-stopped"), sample("network-recover")])
                failure = network_recovery_failure(proofs)
                require(failure is None, failure or "Network reload proof failed")
                if args.network_physics:
                    failure = network_physics_failure(proofs, live=False)
                    require(failure is None, failure or "Physics checkpoint reload proof failed")
                if args.network_csharp_facade:
                    failure = facade_failure(proofs, live=False)
                    require(failure is None, failure or "Managed facade recovery proof failed")
                receipt["network_recovery"] = {
                    "passed": True,
                    "proofs": proofs,
                    "cpp_version": 4,
                    "cs_version": 6,
                    "physics": args.network_physics,
                    "csharp_facade": args.network_csharp_facade,
                    "scope": "Windows Debug editor/game, one authenticated local client; native session references in serialized dictionaries and dynamic signal callbacks. Explicit admission after a stopped-authority fault; no physics checkpoint, concurrent reload or exported-runtime claim.",
                }
            if args.network_live_reload:
                proofs = [sample("network-live-start")]
                require(proofs[0]["passed"], "Live network setup failed")
                cs_version = 5 if args.unload_recovery else 4
                before_descriptor = digest(descriptor)
                (project / "ReloadProbe.cs").write_text(probe_source.replace("VERSION", "invalid!"), encoding="utf-8")
                run("managed-live-invalid", ["dotnet", "build", "--nologo", "-v", "minimal"], expected_success=False)
                state = sample()
                verify(state, 3, previous, cs_version=cs_version)
                previous = state
                proofs.append(sample("network-live-check"))
                source_path.write_text(
                    source.replace("VERSION", "3") + "\n#error EGP deliberate live network diagnostic\n",
                    encoding="utf-8",
                )
                require(command("build", 900)["build_result"] != 0, "Invalid live C++ unexpectedly compiled")
                require(digest(descriptor) == before_descriptor, "Failed live build published a descriptor")
                state = sample()
                verify(state, 3, previous, cs_version=cs_version)
                previous = state
                proofs.append(sample("network-live-check"))
                for phase, cpp_version, managed_version in (("csharp", 3, 6), ("cpp", 4, 6), ("combined", 5, 7)):
                    before_after_count = previous["after_count"]
                    if phase != "cpp":
                        (project / "ReloadProbe.cs").write_text(
                            probe_source.replace("VERSION", str(managed_version)), encoding="utf-8"
                        )
                        run("managed-live-" + phase, ["dotnet", "build", "--nologo", "-v", "minimal"])
                    if phase == "csharp":
                        command("reload")
                    else:
                        source_path.write_text(source.replace("VERSION", str(cpp_version)), encoding="utf-8")
                        require(command("build", 900)["build_result"] == 0, "Live native reload build failed")
                    time.sleep(2)
                    state = sample()
                    verify(state, cpp_version, previous, cs_version=managed_version)
                    require(
                        state["after_count"] == before_after_count
                        if phase == "cpp"
                        else state["after_count"] > before_after_count,
                        "Unexpected managed reload lifecycle during " + phase,
                    )
                    previous = state
                    proofs.append(sample("network-live-check"))
                failure = network_live_failure(proofs, simulation)
                require(failure is None, failure or "Live network reload proof failed")
                if args.network_csharp_facade:
                    failure = facade_failure(proofs, live=True)
                    require(failure is None, failure or "Managed facade live reload proof failed")
                if args.network_physics:
                    failure = network_physics_failure(proofs, live=True)
                    require(failure is None, failure or "Live physics reload proof failed")
                receipt["network_live_reload"] = {
                    "passed": True,
                    "physics": args.network_physics,
                    "csharp_facade": args.network_csharp_facade,
                    "proofs": proofs,
                    "phases": [
                        "initial",
                        "managed-compile-failure",
                        "native-compile-failure",
                        "csharp-reload",
                        "cpp-reload",
                        "combined-reload",
                    ],
                    "scope": "Windows Debug editor and separate game; one authenticated authority/client pair shares game process. Both outbound simulators configured; no reconnect, checkpoints/handles/native identities retained. Configured loss does not quantify actual dropped packets or real WAN performance.",
                }
        command("close")
        require(process.wait(timeout=60) == 0, "Editor/game teardown failed")
        game_log = (output / "game.log").read_text(encoding="utf-8")
        for diagnostic in (
            'Parameter "delegate_handle.value" is null',
            "ManagedCallableMiddleman::",
            "Return type is not bool",
            "SCRIPT ERROR:",
        ):
            require(diagnostic not in game_log, "Unexpected game diagnostic: " + diagnostic)
        receipt["game_diagnostics_checked"] = True
        if args.native_abi_recovery:
            stream.flush()
            log = (output / "editor.log").read_text(encoding="utf-8")
            for diagnostic in (
                "Attempt to unregister unexisting extension class",
                'Parameter "_extension" is null',
                "Cannot call invalid GDExtension method bind",
                "SCRIPT ERROR:",
            ):
                require(diagnostic not in log, "Unexpected reload diagnostic: " + diagnostic)
        require(digest(engine) == receipt["engine_sha256"], "Input engine changed during validation")
        require(
            all(digest(Path(path)) == expected for path, expected in receipt["managed_runtime_sha256"].items()),
            "Managed runtime changed during validation",
        )
        require(
            all(digest(ROOT / path) == expected for path, expected in receipt["fixture_sha256"].items()),
            "Fixture source changed during validation",
        )
        receipt["passed"] = True
    except (OSError, RuntimeError, KeyError, subprocess.TimeoutExpired) as error:
        receipt["error"] = str(error)
    finally:
        (project / "release-root").write_text("release", encoding="utf-8")
        if process is not None:
            terminate(process)
        if stream is not None:
            stream.close()
        receipt["generated_sha256"] = {
            str(p.relative_to(project)): digest(p)
            for p in project.rglob("*")
            if p.is_file()
            and (
                p.name
                in (
                    "ReloadFixture.dll",
                    "extension.cpp",
                    "ReloadProbe.cs",
                    "NetApi.cs",
                    "NetSessionSignals.cs",
                    "reload.gdextension",
                    "physics_checkpoint.bin",
                )
                or (p.suffix == ".dll" and "extensions" in p.relative_to(project).parts)
            )
        }
        receipt["logs_sha256"] = {str(p): digest(p) for p in output.glob("*.log")}
        save()
    print("PASS" if receipt["passed"] else "FAIL", output / "receipt.json", receipt.get("error", ""), flush=True)
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
