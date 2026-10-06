// SPDX-License-Identifier: MIT
using System;
using System.Collections.Generic;
using Godot;
using Array = Godot.Collections.Array;
using Dictionary = Godot.Collections.Dictionary;

namespace EGP.Networking;

public enum Delivery { ReliableOrdered = 2, Unreliable = 4 }
public enum Sender { Server = 1, Client = 2, Both = 3 }

/// <summary>Options shared by every language. PrivateKey is server-only, 32 bytes.</summary>
public sealed class NetOptions
{
    public int TickRate { get; set; } = 60;
    public int MaxPlayers { get; set; } = 32;
    public int MaxEntities { get; set; } = 1024;
    public int MessagesPerSecond { get; set; } = 1000;
    public int BytesPerSecond { get; set; } = 4 * 1024 * 1024;
    public int TimeoutSeconds { get; set; } = 5;
    public int TokenLifetimeSeconds { get; set; } = 30;
    public string GameProtocol { get; set; } = "egp-game-v1";
    public string SimulationFingerprint { get; set; } = "script-state-v1";
    public bool AllowInsecureLoopback { get; set; }
    public byte[]? PrivateKey { get; set; }
    public float SimulatedLoss { get; set; }
    public float SimulatedLatencyMs { get; set; }
    public float SimulatedJitterMs { get; set; }
    public Dictionary ToDictionary()
    {
        var result = new Dictionary {
            ["tick_rate"] = TickRate, ["max_players"] = MaxPlayers, ["max_entities"] = MaxEntities,
            ["messages_per_second"] = MessagesPerSecond, ["bytes_per_second"] = BytesPerSecond,
            ["timeout_seconds"] = TimeoutSeconds, ["token_lifetime_seconds"] = TokenLifetimeSeconds,
            ["game_protocol"] = GameProtocol, ["simulation_fingerprint"] = SimulationFingerprint,
            ["allow_insecure_loopback"] = AllowInsecureLoopback, ["simulated_loss"] = SimulatedLoss,
            ["simulated_latency_ms"] = SimulatedLatencyMs, ["simulated_jitter_ms"] = SimulatedJitterMs
        };
        if (PrivateKey != null) result["private_key"] = PrivateKey;
        return result;
    }
}

public readonly record struct TokenResult(Error Error, byte[] Token)
{
    internal static TokenResult Read(Dictionary value) => new((Error)value["error"].AsInt32(),
        value.ContainsKey("token") ? value["token"].AsByteArray() : System.Array.Empty<byte>());
}
public readonly record struct SpawnResult(Error Error, long Entity);
public readonly record struct PeerInfo(long PeerId, long ClientId, float PingMs);
public readonly record struct RawEntity(long Entity, int Kind, long AuthorityPeer, long Revision, long Tick, byte[] State);

internal sealed class SignalLinks : IDisposable
{
    private readonly GodotObject owner;
    private readonly List<(StringName Name, Callable Callback)> links = new();
    internal SignalLinks(GodotObject value) => owner = value;
    internal void Add(string name, Callable callback)
    {
        var error = owner.Connect(name, callback);
        if (error != Error.Ok) throw new InvalidOperationException($"Cannot connect {name}: {error}");
        links.Add((name, callback));
    }
    public void Dispose()
    {
        if (GodotObject.IsInstanceValid(owner))
            foreach (var link in links)
                if (owner.IsConnected(link.Name, link.Callback)) owner.Disconnect(link.Name, link.Callback);
        links.Clear();
    }
}

internal static class Shared
{
    internal static GodotObject New(string path)
    {
        var script = ResourceLoader.Load<Script>(path);
        if (script == null) throw new InvalidOperationException($"Install EGP networking helpers: missing {path}");
        using var result = script.Call("new");
        return result.AsGodotObject() ?? throw new InvalidOperationException($"Cannot instantiate {path}");
    }
    internal static Error Error(GodotObject instance, string method, params Variant[] args)
    {
        using Variant keepAlive = instance;
        using var result = instance.Call(method, args);
        return (Error)result.AsInt32();
    }
}

/// <summary>Low-level native session: opaque bytes, raw channels, admission and replication.</summary>
/// <remarks>Construct, poll and dispose on the same Godot thread. No GDScript dependency.</remarks>
public sealed class NetSession : IDisposable
{
    private const string ReloadTokenMetadata = "_egp_csharp_session_reload_token";
    private readonly GodotObject native;
    private readonly SignalLinks links;
    private readonly NetSessionSignals bridge;
    private bool disposed;
    public GodotObject Native => !disposed ? native : throw new ObjectDisposedException(nameof(NetSession));
    public event Action<string>? StateChanged;
    public event Action<long>? PeerConnected;
    public event Action<long>? PeerDisconnected;
    public event Action<long, byte[]>? ApplicationReceived;
    public event Action<long, byte[], int, Delivery>? PacketReceived;
    public event Action<long, bool>? SimulationTick;
    public event Action<string>? Diagnostic;
    public NetSession() : this(ClassDB.Instantiate("EGPNetSession").AsGodotObject()
        ?? throw new InvalidOperationException("This EGP build does not include native networking."), true) { }

    private NetSession(GodotObject value, bool created)
    {
        native = value;
        links = new(native);
        bridge = new() { Owner = this };
        try
        {
            links.Add("state_changed", new Callable(bridge, nameof(NetSessionSignals.OnStateChanged)));
            links.Add("peer_connected", new Callable(bridge, nameof(NetSessionSignals.OnPeerConnected)));
            links.Add("peer_disconnected", new Callable(bridge, nameof(NetSessionSignals.OnPeerDisconnected)));
            links.Add("application_received", new Callable(bridge, nameof(NetSessionSignals.OnApplicationReceived)));
            links.Add("packet_received", new Callable(bridge, nameof(NetSessionSignals.OnPacketReceived)));
            links.Add("simulation_tick", new Callable(bridge, nameof(NetSessionSignals.OnSimulationTick)));
            links.Add("diagnostic", new Callable(bridge, nameof(NetSessionSignals.OnDiagnostic)));
        }
        catch
        {
            links.Dispose();
            bridge.Owner = null; bridge.Dispose();
            if (created) native.Dispose();
            throw;
        }
    }

    /// <summary>Disconnect managed handlers and transfer this session to a reload capsule.</summary>
    /// <remarks>
    /// Call from ISerializationListener.OnBeforeSerialize and save the returned dictionary
    /// in an exported property. The native connection keeps running. This wrapper becomes
    /// disposed; disposing it again does not close the transferred session. Use on its Godot thread.
    /// </remarks>
    public Dictionary DetachForReload()
    {
        var instance = Native;
        string token = Guid.NewGuid().ToString("N");
        var state = new Dictionary { ["version"] = 1, ["session"] = instance, ["token"] = token };
        instance.SetMeta(ReloadTokenMetadata, token);
        disposed = true;
        links.Dispose();
        bridge.Owner = null; bridge.Dispose();
        StateChanged = null; PeerConnected = null; PeerDisconnected = null;
        ApplicationReceived = null; PacketReceived = null; SimulationTick = null; Diagnostic = null;
        return state;
    }

    /// <summary>Consume a reload capsule and reconnect the native session's managed signal bridges.</summary>
    /// <remarks>
    /// Call from ISerializationListener.OnAfterDeserialize on the original Godot thread,
    /// then subscribe application event handlers again. Invalid or already consumed capsules
    /// throw ArgumentException and remain unchanged. Copies of a consumed capsule are rejected.
    /// This transfers local ownership only; capsules are not a network or disk format.
    /// </remarks>
    public static NetSession ResumeAfterReload(Dictionary state)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (!state.ContainsKey("version") || state["version"].VariantType != Variant.Type.Int || state["version"].AsInt64() != 1
            || !state.ContainsKey("session") || state["session"].VariantType != Variant.Type.Object
            || !state.ContainsKey("token") || state["token"].VariantType != Variant.Type.String)
            throw new ArgumentException("Expected an unconsumed NetSession reload capsule.", nameof(state));
        var instance = state["session"].AsGodotObject();
        string token = state["token"].AsString();
        if (!GodotObject.IsInstanceValid(instance) || !instance.IsClass("EGPNetSession") || string.IsNullOrEmpty(token)
            || !instance.HasMeta(ReloadTokenMetadata) || instance.GetMeta(ReloadTokenMetadata).AsString() != token)
            throw new ArgumentException("The native session is invalid or this reload capsule was already consumed.", nameof(state));
        var result = new NetSession(instance, false);
        instance.RemoveMeta(ReloadTokenMetadata);
        state.Clear();
        return result;
    }
    internal void ReceiveStateChanged(string state) => StateChanged?.Invoke(state);
    internal void ReceivePeerConnected(long peer) => PeerConnected?.Invoke(peer);
    internal void ReceivePeerDisconnected(long peer) => PeerDisconnected?.Invoke(peer);
    internal void ReceiveApplication(long peer, byte[] data) => ApplicationReceived?.Invoke(peer, data);
    internal void ReceivePacket(long peer, byte[] data, long channel, long delivery) => PacketReceived?.Invoke(peer, data, (int)channel, (Delivery)delivery);
    internal void ReceiveTick(long tick, bool authority) => SimulationTick?.Invoke(tick, authority);
    internal void ReceiveDiagnostic(string message) => Diagnostic?.Invoke(message);
    public Error Configure(NetOptions? options = null) => Configure((options ?? new()).ToDictionary());
    public Error Configure(Dictionary options) => Shared.Error(Native, "configure", options);
    public Error Listen(int port = 10515, string bindAddress = "0.0.0.0") => Shared.Error(Native, "listen", port, bindAddress);
    public Error ConnectLoopback(string address, int port = 10515) => Shared.Error(Native, "connect_to_server", address, port);
    public Error ConnectToken(long clientId, byte[] token, string bindAddress = "0.0.0.0") => Shared.Error(Native, "connect_token", clientId, token, bindAddress);
    public TokenResult IssueToken(long clientId, string publicAddress) => TokenResult.Read(Native.Call("issue_token", clientId, publicAddress).AsGodotDictionary());
    public Error Poll() => Shared.Error(Native, "poll");
    public void Stop() => Native.Call("stop");
    public void Close() => Native.Call("close");
    public string State => Native.Call("get_state").AsString();
    public string Fingerprint => Native.Call("get_fingerprint").AsString();
    public Dictionary Statistics => Native.Call("get_statistics").AsGodotDictionary();
    public Variant Command(StringName operation, Dictionary? arguments = null) => Native.Call("command", operation, arguments ?? new());
    public Error SendApplication(long peer, byte[] payload) => Shared.Error(Native, "send_application", peer, payload);
    public Error SendPacket(long peer, byte[] payload, int channel = 0, Delivery delivery = Delivery.ReliableOrdered)
        => (Error)Command("send_packet", new() { ["peer"] = peer, ["payload"] = payload, ["channel"] = channel, ["delivery"] = (int)delivery }).AsInt32();
    public SpawnResult Spawn(int kind, byte[] state, long authorityPeer = -1)
    {
        var value = Command("spawn", new() { ["kind"] = kind, ["state"] = state, ["authority_peer"] = authorityPeer });
        if (value.VariantType != Variant.Type.Dictionary) return new((Error)value.AsInt32(), 0);
        var row = value.AsGodotDictionary();
        return new((Error)row["error"].AsInt32(), row["entity"].AsInt64());
    }
    public Error UpdateEntity(long entity, byte[] state) => (Error)Command("update_entity", new() { ["entity"] = entity, ["state"] = state }).AsInt32();
    public Error Despawn(long entity) => (Error)Command("despawn", new() { ["entity"] = entity }).AsInt32();
    public Error SetEntityVisible(long entity, long peer, bool visible) => (Error)Command("set_visible", new() { ["entity"] = entity, ["peer"] = peer, ["visible"] = visible }).AsInt32();
    public Error DisconnectPeer(long peer) => (Error)Command("disconnect", new() { ["peer"] = peer }).AsInt32();
    public PeerInfo[] GetPeers()
    {
        var value = Command("peers");
        if (value.VariantType != Variant.Type.Array) return System.Array.Empty<PeerInfo>();
        var rows = value.AsGodotArray(); var result = new PeerInfo[rows.Count];
        for (int i = 0; i < rows.Count; i++) { var row = rows[i].AsGodotDictionary(); result[i] = new(row["peer_id"].AsInt64(), row["client_id"].AsInt64(), row["ping_ms"].AsSingle()); }
        return result;
    }
    public RawEntity[] GetEntities()
    {
        var value = Command("entities");
        if (value.VariantType != Variant.Type.Array) return System.Array.Empty<RawEntity>();
        var rows = value.AsGodotArray(); var result = new RawEntity[rows.Count];
        for (int i = 0; i < rows.Count; i++) { var r = rows[i].AsGodotDictionary(); result[i] = new(r["entity"].AsInt64(), r["kind"].AsInt32(), r["authority_peer"].AsInt64(), r["revision"].AsInt64(), r["tick"].AsInt64(), r["state"].AsByteArray()); }
        return result;
    }
    public void Dispose()
    {
        if (disposed) return;
        // Mark first: a state callback may re-enter Dispose.
        disposed = true;
        links.Dispose(); bridge.Owner = null; bridge.Dispose();
        native.Call("close"); native.Dispose();
    }
}
