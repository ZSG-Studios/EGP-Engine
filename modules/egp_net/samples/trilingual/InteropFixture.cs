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
            if (System.Array.IndexOf(args, "--fixture=processes") >= 0) {
                if (System.Array.IndexOf(args, "--role=client") >= 0 && System.Array.IndexOf(args, "--language=gdscript") >= 0) {
                    AddChild((Node)ResourceLoader.Load<Script>("res://ProcessPeer.gd").Call("new").AsGodotObject()); return;
                }
                await RunProcess(args); Cleanup(); GetTree().Quit(); return;
            }
            await Run(); GD.Print("EGP_TRILINGUAL_PASSED " + Json.Stringify(new Dictionary { ["checks"] = checks.Count, ["languages"] = new Array { "csharp", "gdscript", "cpp" }, ["physics_profile"] = physicsProfile })); Cleanup(); GetTree().Quit();
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
            CheckPoll(server?.Poll()); cppPeer?.Call("poll");
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
            cppPeer?.Call("poll"); CheckPoll(lowServer?.Poll()); CheckPoll(lowPeer?.Poll());
            await ToSignal(GetTree(), SceneTree.SignalName.ProcessFrame);
        }
    }
    private static void CheckPoll(Error? error) { if (error.HasValue && error != Error.Ok) throw new InvalidOperationException("Pump: " + error); }
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
    }
}
