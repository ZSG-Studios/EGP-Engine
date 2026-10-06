// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using System.Threading.Tasks;
using EGP.Networking;
using Godot;
using Array = Godot.Collections.Array;
using Dictionary = Godot.Collections.Dictionary;

public partial class InteropFixture : Node
{
    private readonly List<string> checks = new();
    private NetNode? server, csPeer;
    private NetSession? lowServer, lowPeer;
    private Node? gdPeer, cppPeer;
    private NetBox3D? physics;
    private string physicsProfile = "";
    private readonly Dictionary clockRecovery = new();
    private void Check(bool condition, string label)
    {
        if (!condition) throw new InvalidOperationException(label);
        checks.Add(label);
    }
    private Error CallError(GodotObject obj, string method, params Variant[] args) => (Error)obj.Call(method, args).AsInt32();
    public override async void _Ready()
    {
        try {
            var args = OS.GetCmdlineUserArgs();
            if (System.Array.IndexOf(args, "--fixture=clock-processes") >= 0) {
                string handoff = "", language = "csharp";
                foreach (var arg in args) { if (arg.StartsWith("--handoff=")) handoff = arg[10..]; if (arg.StartsWith("--language=")) language = arg[11..]; }
                if (System.Array.IndexOf(args, "--role=server") >= 0) { AddChild((Node)ResourceLoader.Load<Script>("res://ClockServer.gd").Call("new").AsGodotObject()); return; }
                await RunClockClient(handoff, language); Cleanup(); GetTree().Quit(); return;
            }
            if (System.Array.IndexOf(args, "--fixture=processes") >= 0) {
                if (System.Array.IndexOf(args, "--role=client") >= 0 && System.Array.IndexOf(args, "--language=gdscript") >= 0) {
                    AddChild((Node)ResourceLoader.Load<Script>("res://ProcessPeer.gd").Call("new").AsGodotObject()); return;
                }
                await RunProcess(args); Cleanup(); GetTree().Quit(); return;
            }
            await Run(); GD.Print("EGP_TRILINGUAL_PASSED " + Json.Stringify(new Dictionary { ["checks"] = checks.Count, ["languages"] = new Array { "csharp", "gdscript", "cpp" }, ["physics_profile"] = physicsProfile, ["clock_recovery"] = clockRecovery })); Cleanup(); GetTree().Quit();
        }
        catch (Exception error) { GD.PushError("EGP_TRILINGUAL_FAILED " + error); Cleanup(); GetTree().Quit(1); }
    }
    private async Task RunProcess(string[] args)
    {
        string role = "", admission = "", language = "csharp";
        foreach (var arg in args) {
            if (arg.StartsWith("--role=")) role = arg[7..];
            if (arg.StartsWith("--admission=")) admission = arg[12..];
            if (arg.StartsWith("--language=")) language = arg[11..];
        }
        Check((role == "server" || role == "client") && admission.Length > 0, "process arguments");
        bool replied = false, sent = false, sawEntity = false; int drain = 0;
        if (language == "cpp") {
            cppPeer = (Node)ClassDB.Instantiate("EGPNetCppProbe").AsGodotObject(); AddChild(cppPeer);
            Check(CallError(cppPeer, "start_process", System.IO.File.ReadAllBytes(admission)) == Error.Ok, "C++ process token admission");
        } else {
            server = new NetNode { AutoPoll = false }; AddChild(server);
            Check(server.Configure(new NetOptions { GameProtocol = "egp-process-fixture-v1", MaxPlayers = 2, MaxEntities = 2 }) == Error.Ok, "C# process configuration");
            if (role == "server") {
                Check(server.RegisterMessage("hello", Callable.From<long, Array>((peer, values) => {
                    var peers = server.GetPeers(); Check(peers.Count == 1 && peers[0].AsGodotDictionary()["client_id"].AsInt64() == 9876
                        && peers[0].AsGodotDictionary()["peer_id"].AsInt64() == peer && values.Count == 1 && values[0].AsInt32() == 77, "authenticated process identity");
                    Check(server.SendMessage(peer, "reply", new Array { 88 }) == Error.Ok, "C# process reply"); drain = 1;
                }), Sender.Client) == Error.Ok, "process client handler");
                server.PeerConnected += peer => Check(server.Spawn(1, new Dictionary { ["stamp"] = 55, ["position"] = new Vector3(1, 2, 3) }, peer) > 0, "process authoritative entity");
                Check(server.Host(0, "127.0.0.1") == Error.Ok, "C# process listener");
                var token = server.IssueToken(9876, $"127.0.0.1:{server.Statistics["local_port"].AsInt32()}");
                Check(token.Error == Error.Ok && token.Token.Length == 2048, "C# process token");
                System.IO.File.WriteAllBytes(admission, token.Token); // Test-only trusted handoff.
            } else {
                server.RegisterMessage("reply", Callable.From<long, Array>((_, values) => replied = values.Count == 1 && values[0].AsInt32() == 88), Sender.Server);
                Check(server.JoinToken(9876, System.IO.File.ReadAllBytes(admission)) == Error.Ok, "C# process token admission");
            }
        }
        ulong started = Time.GetTicksMsec();
        while (Time.GetTicksMsec() - started < 10000) {
            CheckPoll(server?.Poll()); if (cppPeer != null) CheckPoll(CallError(cppPeer, "poll"));
            if (cppPeer != null) { if (cppPeer.Call("status").AsGodotDictionary()["process_passed"].AsBool()) break; }
            else if (role == "server") { if (drain > 0 && ++drain >= 15) break; }
            else {
                var entities = server!.GetEntities();
                if (entities.Count == 1) { var state = server.GetEntity(entities[0].AsInt64())["state"].AsGodotDictionary(); sawEntity = state["stamp"].AsInt32() == 55 && state["position"].AsVector3() == new Vector3(1, 2, 3); }
                if (sawEntity && !sent) { Check(server.SendMessage(0, "hello", new Array { 77 }) == Error.Ok, "C# process request"); sent = true; }
                if (sawEntity && replied) break;
            }
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
        }
        bool passed = role == "server" ? drain >= 15 : cppPeer != null ? cppPeer.Call("status").AsGodotDictionary()["process_passed"].AsBool() : sawEntity && replied;
        Check(passed, "separate process watchdog/result");
        GD.Print("EGP_NETWORK_PROCESS " + Json.Stringify(new Dictionary { ["passed"] = true, ["role"] = role, ["language"] = language, ["message"] = "encrypted admission, identity, baseline and reply" }));
    }
    private async Task RunClockClient(string handoff, string language)
    {
        if (language == "cpp") {
            cppPeer = (Node)ClassDB.Instantiate("EGPNetCppProbe").AsGodotObject(); AddChild(cppPeer);
            Check(CallError(cppPeer, "start_clock_process", handoff, System.IO.File.ReadAllText(System.IO.Path.Combine(handoff, "profile.txt"))) == Error.Ok, "C++ clock process configure");
            ulong deadline = Time.GetTicksMsec() + 30000;
            while (Time.GetTicksMsec() < deadline) {
                var result = cppPeer.Call("clock_process_tick").AsGodotDictionary();
                if (result["done"].AsBool()) { Check(result["passed"].AsBool(), "C++ clock process result: " + result["message"].AsString()); GD.Print("EGP_CLOCK_PROCESS " + Json.Stringify(result)); return; }
                await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
            }
            throw new InvalidOperationException("C++ clock process watchdog");
        }
        server = new NetNode { AutoPoll = false }; AddChild(server);
        var states = new Array(); var polls = new Array(); var epochs = new Array(); var disconnects = new Array();
        server.StateChanged += s => states.Add(s);
        Check(server.Configure(new NetOptions { SimulationFingerprint = System.IO.File.ReadAllText(System.IO.Path.Combine(handoff, "profile.txt")), MaxPlayers = 1, MaxEntities = 2, SimulatedLatencyMs = 20, SimulatedJitterMs = 5 }) == Error.Ok, "C# clock process profile");
        ulong native = server.NativeSession!.GetInstanceId(); int epoch = 1, drain = 0; long entity = 0, retired = 0;
        bool joined = false, sent = false, reply = false; byte[] previous = System.Array.Empty<byte>();
        server.RegisterMessage("reply", Callable.From<long, Array>((_, args) => { Check(args.Count == 1 && args[0].AsInt32() == epoch, "C# epoch reply"); reply = true; }), Sender.Server);
        ulong start = Time.GetTicksMsec();
        while (Time.GetTicksMsec() - start < 30000) {
            polls.Add(DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()); CheckPoll(server.Poll());
            if (server.State == "Disconnected") {
                Check(epoch < 4 && sent && server.GetEntities().Count == 0 && server.SendInput(entity, new()) == Error.Unconfigured, "C# native disconnect clears physics baseline");
                disconnects.Add(new Dictionary { ["epoch"] = epoch, ["entity"] = entity, ["utc_ms"] = DateTimeOffset.UtcNow.ToUnixTimeMilliseconds(), ["cleared"] = true });
                server.Stop(); epoch++; retired = entity; joined = sent = reply = false; drain = 0;
            }
            if (!joined) {
                string path = System.IO.Path.Combine(handoff, $"epoch-{epoch}.bin");
                if (System.IO.File.Exists(path) && new System.IO.FileInfo(path).Length == 2048) {
                    byte[] token = System.IO.File.ReadAllBytes(path);
                    Check(!token.AsSpan().SequenceEqual(previous) && server.JoinToken(9876, token) == Error.Ok && server.NativeSession!.GetInstanceId() == native, "C# fresh process admission retains session");
                    previous = token; joined = true;
                }
            }
            if (server.State == "Connected" && !sent && server.GetEntities().Count == 1) {
                entity = server.GetEntities()[0].AsInt64(); var record = server.GetEntity(entity); var state = record["state"].AsGodotDictionary();
                if (PhysicsTick(record) > 0 && state["epoch"].AsInt32() == epoch) {
                    Check(entity != retired && server.GetEntity(retired).Count == 0 && record["authority_peer"].AsInt64() > 0, "C# fresh owned process physics baseline");
                    Check(server.SendInput(entity, new() { ["epoch"] = epoch }) == Error.Ok && server.SendMessage(0, "ready", new Array { epoch, entity, PhysicsTick(record) }) == Error.Ok, "C# process input/ready");
                    epochs.Add(new Dictionary { ["epoch"] = epoch, ["entity"] = entity, ["physics_tick"] = PhysicsTick(record), ["same_session"] = true, ["fresh_token"] = true, ["retired_absent"] = true }); sent = true;
                }
            }
            if (reply && drain == 0) { Check(server.SendMessage(0, "ack", new Array { epoch }) == Error.Ok, "C# process reply ack"); drain = 1; }
            if (epoch == 4 && drain > 0 && ++drain >= 15) {
                server.Stop();
                GD.Print("EGP_CLOCK_PROCESS " + Json.Stringify(new Dictionary { ["passed"] = true, ["role"] = "client", ["language"] = "csharp", ["pid"] = OS.GetProcessId(), ["epochs"] = epochs, ["disconnects"] = disconnects, ["states"] = states, ["poll_utc_ms"] = polls, ["message"] = "independent authority disconnect and fresh physics baseline" })); return;
            }
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
        }
        throw new InvalidOperationException("C# clock process watchdog");
    }
    private void Cleanup()
    {
        physics?.Dispose(); physics = null;
        server?.Close(); csPeer?.Close();
        gdPeer?.Call("close"); cppPeer?.Call("stop");
        lowServer?.Dispose(); lowPeer?.Dispose();
    }
    private async Task Frames(int count)
    {
        for (int i = 0; i < count; i++)
        {
            CheckPoll(server?.Poll()); CheckPoll(csPeer?.Poll());
            if (gdPeer != null) CheckPoll(CallError(gdPeer, "poll"));
            if (cppPeer != null) CheckPoll(CallError(cppPeer, "poll")); CheckPoll(lowServer?.Poll()); CheckPoll(lowPeer?.Poll());
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
        }
    }
    private static void CheckPoll(Error? error) { if (error.HasValue && error != Error.Ok) throw new InvalidOperationException("Pump: " + error); }
    private static long PhysicsTick(Dictionary record) => record.TryGetValue("state", out var state)
        && state.AsGodotDictionary().TryGetValue("physics_tick", out var tick) ? tick.AsInt64() : 0;
    private async Task Run()
    {
        using (var unconfigured = new NetSession()) {
            Check(unconfigured.Spawn(1, new byte[] { 1 }).Error == Error.Unconfigured && unconfigured.GetEntities().Length == 0 && unconfigured.GetPeers().Length == 0, "C# unconfigured low results");
        }
        var closing = new NetSession();
        Check(closing.Configure() == Error.Ok, "C# callback lifetime configure");
        closing.StateChanged += state => { if (state == "Listening") closing.Dispose(); };
        Check(closing.Listen(0) == Error.Ok, "C# disposal during native callback"); closing.Dispose();
        server = new NetNode { AutoPoll = false }; csPeer = new NetNode { AutoPoll = false };
        AddChild(server); AddChild(csPeer);
        Check(server.Configure() == Error.Ok && csPeer.Configure() == Error.Ok, "C# high configure");
        Check(server.Host(0) == Error.Ok, "C# secure host");
        int port = server.Statistics["local_port"].AsInt32();
        var replies = new HashSet<string>();
        Check(server.RegisterMessage("reply", Callable.From<long, Array>((peer, args) => { if (args.Count == 2 && args[1].AsInt32() == 88) replies.Add(args[0].AsString()); }), Sender.Client) == Error.Ok, "C# registered client sender");
        int csHello = 0, csPackets = 0, lowApplications = 0;
        csPeer.RegisterMessage("hello", Callable.From<long, Array>((peer, args) => {
            if (args.Count == 2 && args[0].AsInt32() == 77 && args[1].AsVector3() == new Vector3(1, 2, 3)) { csHello++; csPeer.SendMessage(peer, "reply", new Array { "csharp", 88 }); }
        }), Sender.Server);
        csPeer.PacketReceived += (_, bytes, channel, delivery) => { if (bytes.Length == 3 && bytes[0] == 4 && channel == 3 && delivery == Delivery.Unreliable) csPackets++; };
        TokenResult Token(NetNode host, long id) { var t = host.IssueToken(id, $"127.0.0.1:{host.Statistics["local_port"].AsInt32()}"); Check(t.Error == Error.Ok && t.Token.Length == 2048, "high token " + id); return t; }
        Check(csPeer.JoinToken(222, Token(server, 222).Token) == Error.Ok, "C# authenticated join");
        gdPeer = (Node)ResourceLoader.Load<Script>("res://GdPeer.gd").Call("new").AsGodotObject(); AddChild(gdPeer);
        Check(CallError(gdPeer, "prepare", Token(server, 111).Token) == Error.Ok, "GDScript authenticated join");
        lowServer = new NetSession(); lowPeer = new NetSession();
        Check(lowServer.Configure() == Error.Ok && lowPeer.Configure() == Error.Ok && lowServer.Listen(0) == Error.Ok, "C# low configure/listen");
        int lowPort = lowServer.Statistics["local_port"].AsInt32();
        var lowToken = lowServer.IssueToken(444, $"127.0.0.1:{lowPort}");
        var csLowToken = lowServer.IssueToken(555, $"127.0.0.1:{lowPort}");
        Check(lowToken.Error == Error.Ok && csLowToken.Error == Error.Ok && lowPeer.ConnectToken(555, csLowToken.Token) == Error.Ok, "C# low token join");
        lowServer.ApplicationReceived += (_, bytes) => { if (bytes.Length == 3 && bytes[0] == 9) lowApplications++; };
        lowPeer.ApplicationReceived += (peer, bytes) => lowPeer.SendApplication(peer, bytes);
        cppPeer = (Node)ClassDB.Instantiate("EGPNetCppProbe").AsGodotObject(); AddChild(cppPeer);
        Check(CallError(cppPeer, "start", Token(server, 333).Token, lowToken.Token) == Error.Ok, "C++ high/low authenticated join");
        Check(cppPeer.Call("prediction_check").AsBool(), "C++ prediction correction/replay/reset");
        var lowSpawn = lowServer.Spawn(7, new byte[] { 1, 2, 3 });
        Check(lowSpawn.Error == Error.Ok && lowSpawn.Entity > 0, "C# low opaque replication");
        long entity = server.Spawn(12, new Dictionary { ["stamp"] = 55, ["position"] = new Vector3(1, 2, 3) });
        Check(entity > 0, "C# high spawn");
        await Frames(40);
        Check(server.GetPeers().Count == 3 && lowServer.GetPeers().Length == 2, "three language admission and native peer identity");
        Check(server.BroadcastMessage("hello", new Array { 77, new Vector3(1, 2, 3) }) == Error.Ok, "cross language named messages");
        Check(server.BroadcastPacket(new byte[] { 4, 5, 6 }, 3, Delivery.Unreliable) == Error.Ok, "cross language unreliable channel 3");
        foreach (var peer in lowServer.GetPeers()) Check(lowServer.SendApplication(peer.PeerId, new byte[] { 9, 8, 7 }) == Error.Ok, "low application " + peer.ClientId);
        await Frames(25);
        Check(replies.SetEquals(new[] { "cpp", "gdscript", "csharp" }) && csHello == 1 && gdPeer.Get("hello_count").AsInt32() == 1, "C#/GDScript/C++ handler codec interoperability");
        Dictionary cppStatus = cppPeer.Call("status").AsGodotDictionary();
        Check(csPackets == 1 && gdPeer.Get("packet_count").AsInt32() == 1 && cppStatus["packets"].AsInt32() == 1, "raw packet parity");
        Check(lowApplications == 2 && cppStatus["applications"].AsInt32() == 1 && lowPeer.GetEntities().Length == 1 && cppStatus["low_entities"].AsGodotArray().Count == 1, "C++/C# low opaque replication and application exchange");
        Check(csPeer.GetEntity(entity)["state"].AsGodotDictionary()["stamp"].AsInt32() == 55
            && gdPeer.Call("get_entity", entity).AsGodotDictionary()["state"].AsGodotDictionary()["position"].AsVector3() == new Vector3(1, 2, 3)
            && cppStatus["record"].AsGodotDictionary()["state"].AsGodotDictionary()["stamp"].AsInt32() == 55, "full baseline state parity");
        Check(server.UpdateEntity(entity, new Dictionary { ["stamp"] = 99 }) == Error.Ok, "state update"); await Frames(15);
        Check(csPeer.GetEntity(entity)["state"].AsGodotDictionary()["stamp"].AsInt32() == 99, "state update delivered");
        long csId = 0; foreach (Variant row in server.GetPeers()) { var p = row.AsGodotDictionary(); if (p["client_id"].AsInt64() == 222) csId = p["peer_id"].AsInt64(); }
        Check(server.SetEntityVisible(entity, csId, false) == Error.Ok, "interest hide"); await Frames(15); Check(csPeer.GetEntity(entity).Count == 0, "interest removed baseline");
        Check(server.SetEntityVisible(entity, csId, true) == Error.Ok, "interest show"); await Frames(15); Check(csPeer.GetEntity(entity).Count > 0, "interest restored baseline");
        long owned = server.Spawn(13, new Dictionary(), csId); int inputs = 0; server.InputReceived += (peer, id, input) => { if (peer == csId && id == owned && input["move"].AsInt32() == 1) inputs++; };
        await Frames(15); Check(csPeer.SendInput(owned, new Dictionary { ["move"] = 1 }) == Error.Ok, "ownership checked input"); await Frames(15); Check(inputs == 1, "owner input delivered");
        var presentationParent = new Node(); AddChild(presentationParent);
        Node[] prototypes = { new NetEntity3D { SmoothingSpeed = 0 }, (Node)ClassDB.Instantiate("EGPNetCppActor").AsGodotObject(),
            (Node)ResourceLoader.Load<Script>("res://addons/egp_net/egp_net_entity_3d.gd").Call("new").AsGodotObject() };
        for (int i = 0; i < prototypes.Length; i++) {
            var packed = new PackedScene(); Check(packed.Pack(prototypes[i]) == Error.Ok && csPeer.RegisterScene(21 + i, packed, presentationParent) == Error.Ok, "scene factory kind " + (21 + i)); prototypes[i].Free();
            Check(server.Spawn(21 + i, new Dictionary { ["transform"] = new Transform3D(Basis.Identity, new Vector3(i + 1, 4, 5)) }) > 0, "presentation spawn " + i);
        }
        await Frames(20);
        Check(presentationParent.GetChildCount() == 3, "C#/C++/GDScript scene factories");
        for (int i = 0; i < 3; i++) Check(presentationParent.GetChild<Node3D>(i).GlobalPosition == new Vector3(i + 1, 4, 5), "presentation transform " + i);
        Check(server.Despawn(entity) == Error.Ok, "despawn"); await Frames(15); Check(csPeer.GetEntity(entity).Count == 0, "despawn delivered");
        int predicted = 0, corrections = 0; using (var prediction = new NetPrediction()) {
            prediction.Corrected += (_, replayed) => corrections += replayed;
            Check(prediction.Configure(() => new byte[] { (byte)predicted }, state => { predicted = state[0]; return Error.Ok; }, (_, input, _) => { predicted += input[0]; return Error.Ok; }, maxStateBytes: 1) == Error.Ok, "C# prediction configure");
            Check(prediction.Predict(1, new byte[] { 1 }) == Error.Ok && prediction.Predict(2, new byte[] { 1 }) == Error.Ok, "C# prediction inputs");
            Check(prediction.Reconcile(1, new byte[] { 5 }) == Error.Ok && predicted == 6 && corrections == 1 && prediction.PendingTicks == 1 && prediction.HistoryBytes == 2, "C# prediction replay and accounting");
            Check(prediction.Reset(10, new byte[] { 7 }) == Error.Ok && predicted == 7 && prediction.PendingTicks == 0, "C# prediction reset");
        }
        // Test component callback name used by the shared scene factory.
        var actor = new NetEntity3D { SmoothingSpeed = 0 }; AddChild(actor);
        actor.Call("apply_network_state", new Dictionary { ["transform"] = new Transform3D(Basis.Identity, new Vector3(2, 3, 4)) });
        Check(actor.GlobalPosition == new Vector3(2, 3, 4), "C# entity presentation callback");
        var actor2D = new NetEntity2D { SmoothingSpeed = 0 }; AddChild(actor2D);
        actor2D.Call("apply_network_state", new Dictionary { ["transform"] = new Transform2D(0, new Vector2(2, 3)) });
        Check(actor2D.GlobalPosition == new Vector2(2, 3), "C# 2D presentation callback");
        // End the first profile before using an explicit deterministic world fingerprint.
        Cleanup(); server = new NetNode { AutoPoll = false }; AddChild(server); csPeer = null; gdPeer = null; cppPeer = null; lowPeer = null; lowServer = null;
        using var world = ClassDB.Instantiate("EGPBox3DWorld").AsGodotObject();
        Check(CallError(world, "configure", 60, 4, 1, new Vector3(0, -9.8f, 0)) == Error.Ok, "C# explicit Box3D world");
        string fingerprint = world.Call("get_simulation_fingerprint").AsString();
        physicsProfile = fingerprint;
        Check(server.Configure(new NetOptions { SimulationFingerprint = fingerprint }) == Error.Ok && server.Host(0) == Error.Ok, "C# physics profile session");
        entity = server.Spawn(1, new Dictionary { ["position"] = new Vector3(0, 10, 0) });
        Check(CallError(world, "queue_create_sphere", 10000, 1, new Vector3(0, 10, 0), 0.5f) == Error.Ok, "C# stable entity body");
        physics = new NetBox3D(); int steps = 0; physics.AfterStep += _ => steps++;
        Check(physics.Attach(server, world) == Error.Ok && physics.Track(entity) == Error.Ok, "C# default body mapping compatibility");
        Check(physics.Track(entity, 10000) == Error.Ok && physics.Track(entity, -1) == Error.InvalidParameter, "C# stable body mapping and rejected invalid replacement");
        cppPeer = (Node)ClassDB.Instantiate("EGPNetCppProbe").AsGodotObject(); AddChild(cppPeer);
        Check(CallError(cppPeer, "start_physics") == Error.Ok, "C++ physics adapter attach/track");
        ulong physicsDeadline = Time.GetTicksMsec() + 3000;
        while (Time.GetTicksMsec() < physicsDeadline && (steps < 30 || cppPeer.Call("status").AsGodotDictionary()["physics_tick"].AsInt64() < 30))
            await Frames(1);
        Check(steps >= 30 && world.Call("get_tick").AsInt64() == server.Statistics["tick"].AsInt64()
            && server.GetEntity(entity)["state"].AsGodotDictionary()["position"].AsVector3().Y < 9.5f, "C# Box3D fixed network clock and state");
        cppStatus = cppPeer.Call("status").AsGodotDictionary();
        Check(cppStatus["physics_tick"].AsInt64() >= 30 && cppStatus["physics_tick"].AsInt64() == cppStatus["network_tick"].AsInt64()
            && cppStatus["body"].AsGodotDictionary()["position"].AsVector3().Y < 9.5f, "C++ Box3D fixed network clock and state");
        cppPeer.Call("stop");
        await CheckClockRecovery(world, entity);
        server.Stop(); physics.Detach();
        var cppRecovery = cppPeer.Call("clock_recovery_check").AsGodotDictionary();
        Check(cppRecovery["passed"].AsBool() && cppRecovery["cycles"].AsGodotArray().Count == 3
            && cppRecovery["low_cycles"].AsGodotArray().Count == 3 && cppRecovery["same_session"].AsBool(), "C++ high/low repeated clock recovery");
        clockRecovery["cpp"] = cppRecovery;
    }

    private async Task CheckClockRecovery(GodotObject world, long entity)
    {
        var native = server!.NativeSession!.GetInstanceId();
        int port = server.Statistics["local_port"].AsInt32();
        var diagnostics = new List<string>(); server.Diagnostic += diagnostics.Add;
        csPeer = new NetNode { AutoPoll = false }; AddChild(csPeer);
        var clientStates = new List<string>(); csPeer.StateChanged += clientStates.Add;
        Check(csPeer.Configure(new NetOptions { SimulationFingerprint = physicsProfile, SimulatedLatencyMs = 20, SimulatedJitterMs = 5 }) == Error.Ok, "C# recovery client profile");
        ulong clientNative = csPeer.NativeSession!.GetInstanceId();
        byte[] previousToken = System.Array.Empty<byte>();
        async Task JoinRecoveryClient()
        {
            var token = server.IssueToken(777, $"127.0.0.1:{port}");
            Check(token.Error == Error.Ok && token.Token.Length == 2048 && !token.Token.AsSpan().SequenceEqual(previousToken)
                && csPeer.JoinToken(777, token.Token) == Error.Ok, "C# fresh recovery admission");
            previousToken = token.Token;
            ulong deadline = Time.GetTicksMsec() + 2000;
            while (Time.GetTicksMsec() < deadline && (csPeer.State != "Connected" || server.GetPeers().Count != 1)) await Frames(1);
            Check(csPeer.State == "Connected" && server.GetPeers().Count == 1 && server.GetPeers()[0].AsGodotDictionary()["client_id"].AsInt64() == 777
                && csPeer.NativeSession!.GetInstanceId() == clientNative, "C# retained client reconnect identity");
        }
        await JoinRecoveryClient();
        ulong baselineDeadline = Time.GetTicksMsec() + 2000;
        while (Time.GetTicksMsec() < baselineDeadline && csPeer.GetEntity(entity).Count == 0) await Frames(1);
        Check(csPeer.GetEntity(entity).Count > 0, "C# initial recovery baseline");
        long expectedEntity = 0, expectedPeer = 0; int expectedCycle = 0, inputs = 0, invalidInputs = 0;
        server.InputReceived += (peer, handle, input) => { if (peer == expectedPeer && handle == expectedEntity && input["cycle"].AsInt32() == expectedCycle) inputs++; else invalidInputs++; };
        var cycles = new Array();
        for (int cycle = 1; cycle <= 3; cycle++) {
            long savedTick = world.Call("get_tick").AsInt64(); string hash = world.Call("get_state_hash").AsString();
            var snapshot = world.Call("capture_snapshot").AsByteArray(); Check(snapshot.Length > 0, "C# clock checkpoint " + cycle);
            float checkpointY = world.Call("get_body_state", 10000).AsGodotDictionary()["position"].AsVector3().Y;
            ulong before = Time.GetTicksMsec(); int livePolls = 0;
            while (Time.GetTicksMsec() - before < 550) { CheckPoll(csPeer.Poll()); livePolls++; await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame); }
            Check(csPeer.State == "Connected" && livePolls > 0, "C# client stays live during server gap " + cycle);
            var error = server.Poll();
            ulong gap = Time.GetTicksMsec() - before;
            Check(error == Error.Failed && diagnostics.Count == cycle && diagnostics[^1] == "Fixed simulation exceeded its catch-up budget; resynchronization required.", "C# clock rejects scheduling debt " + cycle);
            Check(server.State == "Stopped" && server.GetEntities().Count == 0 && server.GetPeers().Count == 0 && server.Statistics["tick"].AsInt64() == 0
                && server.Spawn(1) == 0 && server.UpdateEntity(entity, new()) == Error.Unauthorized, "C# stopped authority " + cycle);
            csPeer.Stop();
            Check(csPeer.State == "Stopped" && csPeer.GetEntities().Count == 0 && csPeer.SendInput(entity, new()) == Error.Unconfigured, "C# explicit client resync clears stale baseline " + cycle);
            physics!.Detach();
            Check(CallError(world, "step_tick", savedTick + 1) == Error.Ok && CallError(world, "restore_snapshot", snapshot) == Error.Ok
                && world.Call("get_tick").AsInt64() == savedTick && world.Call("get_state_hash").AsString() == hash, "C# trusted checkpoint restore " + cycle);
            Check(server.Host(port) == Error.Ok && server.NativeSession!.GetInstanceId() == native && server.GetEntity(entity).Count == 0
                && server.UpdateEntity(entity, new()) == Error.DoesNotExist, "C# retained session/rebind/retired entity " + cycle);
            Check(physics.Attach(server, world) == Error.Ok, "C# recovery world clock attached before admission " + cycle);
            await JoinRecoveryClient();
            expectedPeer = server.GetPeers()[0].AsGodotDictionary()["peer_id"].AsInt64(); expectedCycle = cycle;
            long next = server.Spawn(1, authorityPeer: expectedPeer); expectedEntity = next;
            Check(next != 0 && next != entity && physics.Track(next, 10000) == Error.Ok, "C# fresh handle stable body " + cycle);
            ulong deadline = Time.GetTicksMsec() + 2000;
            while (Time.GetTicksMsec() < deadline && (server.Statistics["tick"].AsInt64() < 8 || csPeer.GetEntity(next).Count == 0
                || PhysicsTick(csPeer.GetEntity(next)) <= savedTick)) await Frames(1);
            Check(server.Statistics["tick"].AsInt64() >= 8 && world.Call("get_tick").AsInt64() == savedTick + server.Statistics["tick"].AsInt64()
                && server.GetEntity(next)["state"].AsGodotDictionary()["physics_tick"].AsInt64() > savedTick, "C# restored world/transport clock offset " + cycle);
            var clientRecord = csPeer.GetEntity(next); var clientState = clientRecord["state"].AsGodotDictionary();
            Check(csPeer.GetEntities().Count == 1 && csPeer.GetEntity(entity).Count == 0 && clientRecord["authority_peer"].AsInt64() == expectedPeer
                && clientState["physics_tick"].AsInt64() > savedTick
                && clientState["position"].AsVector3().Y < checkpointY, "C# recovered owned physics baseline " + cycle);
            long clientTick = clientState["physics_tick"].AsInt64();
            Check(csPeer.SendInput(entity, new() { ["cycle"] = cycle }) == Error.Ok && csPeer.SendInput(next, new() { ["cycle"] = cycle }) == Error.Ok, "C# retired and recovered owner input probes " + cycle);
            deadline = Time.GetTicksMsec() + 2000;
            while (Time.GetTicksMsec() < deadline && inputs < cycle) await Frames(1);
            Check(inputs == cycle && invalidInputs == 0, "C# recovered input delivered once and retired input rejected " + cycle);
            Check(server.SetEntityVisible(next, expectedPeer, false) == Error.Ok, "C# recovered interest hide " + cycle);
            deadline = Time.GetTicksMsec() + 2000;
            while (Time.GetTicksMsec() < deadline && csPeer.GetEntity(next).Count != 0) await Frames(1);
            Check(csPeer.GetEntity(next).Count == 0, "C# recovered interest removes baseline " + cycle);
            Check(server.SetEntityVisible(next, expectedPeer, true) == Error.Ok, "C# recovered interest show " + cycle);
            deadline = Time.GetTicksMsec() + 2000;
            while (Time.GetTicksMsec() < deadline && csPeer.GetEntity(next).Count == 0) await Frames(1);
            Check(csPeer.GetEntity(next).Count > 0, "C# recovered interest restores baseline " + cycle);
            cycles.Add(new Dictionary { ["cycle"] = cycle, ["old_entity"] = entity, ["new_entity"] = next, ["checkpoint_tick"] = savedTick, ["checkpoint_hash"] = hash,
                ["final_physics_tick"] = world.Call("get_tick"), ["final_network_tick"] = server.Statistics["tick"], ["gap_ms"] = gap, ["poll_error"] = (int)error,
                ["client_live_polls"] = livePolls, ["client_id"] = 777, ["client_same_session"] = true, ["fresh_token"] = true,
                ["client_reset_cleared"] = true, ["client_retired_absent"] = true, ["client_physics_tick"] = clientTick, ["owner_input_count"] = inputs,
                ["invalid_input_count"] = invalidInputs, ["interest_roundtrip"] = true });
            entity = next;
        }
        server.Stop(); physics!.Detach(); csPeer.Stop();
        using var low = new NetSession(); Check(low.Configure() == Error.Ok && low.Listen(0) == Error.Ok, "C# low clock host");
        var lowDiagnostics = new List<string>(); low.Diagnostic += lowDiagnostics.Add;
        ulong lowId = low.Native.GetInstanceId(); int lowPort = low.Statistics["local_port"].AsInt32(); long old = low.Spawn(1, new byte[] { 1 }).Entity;
        var lowCycles = new Array();
        for (int cycle = 1; cycle <= 3; cycle++) {
            ulong before = Time.GetTicksMsec(); OS.DelayMsec(550);
            Check(low.Poll() == Error.Failed && low.State == "Stopped" && low.GetEntities().Length == 0 && low.GetPeers().Length == 0
                && low.Spawn(1, new byte[] { 1 }).Error == Error.Unauthorized, "C# low clock fails closed " + cycle);
            Check(low.Listen(lowPort) == Error.Ok && low.Native.GetInstanceId() == lowId, "C# low session retained " + cycle);
            var next = low.Spawn(1, new byte[] { 2 });
            Check(next.Error == Error.Ok && next.Entity != 0 && next.Entity != old && low.UpdateEntity(old, new byte[] { 3 }) == Error.DoesNotExist, "C# low retired handle " + cycle);
            lowCycles.Add(new Dictionary { ["cycle"] = cycle, ["old_entity"] = old, ["new_entity"] = next.Entity, ["poll_error"] = (int)Error.Failed, ["gap_ms"] = Time.GetTicksMsec() - before }); old = next.Entity;
        }
        Check(lowDiagnostics.Count == 3 && lowDiagnostics.TrueForAll(s => s == "Fixed simulation exceeded its catch-up budget; resynchronization required."), "C# low clock diagnostics");
        clockRecovery["csharp"] = new Dictionary { ["passed"] = true, ["same_session"] = true, ["body_id"] = 10000, ["cycles"] = cycles, ["low_cycles"] = lowCycles,
            ["client_states"] = new Array(clientStates.ConvertAll(s => (Variant)s).ToArray()), ["client_latency_ms"] = 20, ["client_jitter_ms"] = 5,
            ["diagnostics"] = new Array(diagnostics.ConvertAll(s => (Variant)s).ToArray()), ["low_diagnostics"] = new Array(lowDiagnostics.ConvertAll(s => (Variant)s).ToArray()) };
    }
}
