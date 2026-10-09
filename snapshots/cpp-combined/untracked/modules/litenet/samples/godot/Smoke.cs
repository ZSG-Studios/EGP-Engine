// SPDX-License-Identifier: MIT
using System;
using System.Diagnostics;
using System.Collections.Generic;
using System.Linq;
using System.Text.Json;
using EGP.Networking;
using Godot;
using LiteEntitySystem;

public partial class Smoke : Node
{
    private EGPLiteRuntime _server, _client;
    private GodotObject _nativeWorld;
    private EGPBox3DPhysicsWorld _world;
    private PhysicsTimeline _physics;
    private readonly Stopwatch _deadline = Stopwatch.StartNew();
    private bool _finished, _echo;
    private readonly Dictionary<ushort, float> _serverHeights = new();

    public override void _Ready()
    {
        try
        {
            Require(typeof(LiteSession).Assembly == typeof(EGPLiteRuntime).GetProperty("Session").PropertyType.Assembly,
                "Game and engine loaded separate networking assemblies");
            foreach (string oldClass in new[] { "ENetMultiplayerPeer", "SceneMultiplayer", "MultiplayerSpawner",
                "MultiplayerSynchronizer", "WebRTCMultiplayerPeer", "WebSocketMultiplayerPeer" })
                Require(!ClassDB.ClassExists(oldClass), "Legacy networking class remains registered: " + oldClass);
            Require(ClassDB.ClassExists("WebSocketPeer"), "Generic WebSocket/debugger API must remain available");
            _nativeWorld = ClassDB.Instantiate("EGPBox3DWorld").AsGodotObject();
            Require(_nativeWorld.Call("configure", 60, 4, 1).AsInt32() == 0, "Box3D configure failed");
            Require(_nativeWorld.Call("queue_create_box", 1, 0, new Vector3(0, 10, 0), new Vector3(0.5f, 0.5f, 0.5f)).AsInt32() == 0,
                "Box3D spawn queue failed");
            Require(_nativeWorld.Call("apply_queued_commands").AsInt32() == 0, "Box3D initial command flush failed");
            _world = new EGPBox3DPhysicsWorld(_nativeWorld);
            var options = new SessionOptions { SimulationFingerprint = _world.SimulationFingerprint, GameProtocol = "egp-smoke-v1" };
            _server = new EGPLiteRuntime(BuildTypes(), options);
            _client = new EGPLiteRuntime(BuildTypes(), options);
            Require(_server.Bridge.Call("listen", 0, "127.0.0.1").AsInt32() == 0, "Native server bridge failed");
            var body = _server.Session.Server.AddEntity<SmokeBody>();
            body.Height.Value = 10;
            _physics = new PhysicsTimeline(_server.Session.Server, _world, () => { }, () =>
            {
                body.Height.Value = BodyHeight();
                _serverHeights[_server.Session.Server.Tick] = body.Height.Value;
            });
            _server.Session.ApplicationReceived += (peer, payload) => _server.Session.SendApplication(peer, payload);
            _client.Session.ApplicationReceived += (_, payload) => _echo = payload.SequenceEqual(new byte[] { 1, 2, 3 });
            _client.Session.StateChanged += state =>
            {
                if (state == SessionState.Connected) _client.Bridge.Call("send_application", 0, new byte[] { 1, 2, 3 });
            };
            Require(_client.Bridge.Call("connect_to_server", "127.0.0.1", _server.Session.LocalPort).AsInt32() == 0,
                "Native client bridge failed");
        }
        catch (Exception error) { Finish(false, error.ToString()); }
    }

    public override void _Process(double delta)
    {
        if (_finished) return;
        try
        {
            Require(_server.Bridge.Call("poll").AsInt32() == 0, "Native server pump failed");
            Require(_client.Bridge.Call("poll").AsInt32() == 0, "Native client pump failed");
            if (_deadline.Elapsed.TotalSeconds > 15) throw new Exception("Networking smoke timed out");
            if (_world.NextTick < 121 || _client.Session.Client?.GetEntities<SmokeBody>().Count != 1 ||
                _client.Session.Client.RawServerTick < 60) return;
            Require(_echo, "Application channel round trip failed");
            float serverHeight = BodyHeight();
            float clientHeight = _client.Session.Client.GetEntities<SmokeBody>().First().Height.Value;
            // Compare the raw replicated value to the matching server state tick.
            // Comparing a buffered client state with the current moving server pose
            // incorrectly turns ordinary jitter into a replication failure.
            ushort stateTick = _client.Session.Client.RawServerTick;
            Require(_serverHeights.TryGetValue(stateTick, out float publishedHeight), "Server state history missing");
            Require(serverHeight < 0 && BitConverter.SingleToInt32Bits(clientHeight) == BitConverter.SingleToInt32Bits(publishedHeight),
                $"Native Box3D state mismatch at tick {stateTick}: received {clientHeight}, published {publishedHeight}");
            Require((ulong)_server.Session.Server.Tick == _world.NextTick - 1, "Physics and LES tick ownership diverged");
            byte[] snapshot = _world.CaptureSnapshot();
            ulong boundary = _world.NextTick;
            for (int i = 0; i < 10; i++) _world.StepTick(_world.NextTick);
            int futureBits = BitConverter.SingleToInt32Bits(BodyHeight());
            _world.RestoreSnapshot(snapshot);
            Require(_world.NextTick == boundary, "Native solver tick restore failed");
            for (int i = 0; i < 10; i++) _world.StepTick(_world.NextTick);
            Require(BitConverter.SingleToInt32Bits(BodyHeight()) == futureBits, "Native solver continuation changed after restore");
            Finish(true, "Native bridge, managed replication, legacy exclusion, fixed physics ticks and solver continuation passed");
        }
        catch (Exception error) { Finish(false, error.ToString()); }
    }

    private float BodyHeight() => _nativeWorld.Call("get_body_state", 1).AsGodotDictionary()["position"].AsVector3().Y;
    private static void Require(bool condition, string message) { if (!condition) throw new Exception(message); }
    private static EntityTypesMap BuildTypes() => new EntityTypesMap<SmokeType>().Register(SmokeType.Body, p => new SmokeBody(p));
    private void Finish(bool passed, string message)
    {
        _finished = true;
        string report = JsonSerializer.Serialize(new { passed, message, ticks = _world?.NextTick,
            networking = _server?.Session.Statistics, client = _client?.Session.Statistics,
            replicated_state_tick = _client?.Session.Client?.RawServerTick,
            fingerprint = _server?.Session.CompatibilityFingerprint });
        GD.Print("EGP_LITENET_SMOKE " + report);
        foreach (string argument in OS.GetCmdlineUserArgs())
            if (argument.StartsWith("--report=")) System.IO.File.WriteAllText(argument[9..], report);
        GetTree().Quit(passed ? 0 : 1);
    }

    public override void _ExitTree()
    {
        _physics?.Dispose();
        _client?.Dispose();
        _server?.Dispose();
        _nativeWorld?.Dispose();
    }
}

public enum SmokeType : ushort { Body }
public class SmokeBody : EntityLogic
{
    [SyncVarFlags(SyncFlags.Interpolated)] public SyncVar<float> Height;
    public SmokeBody(EntityParams parameters) : base(parameters) { }
}
