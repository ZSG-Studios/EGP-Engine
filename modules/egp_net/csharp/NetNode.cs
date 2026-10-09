// SPDX-License-Identifier: MIT
using System;
using Godot;
using Array = Godot.Collections.Array;
using Dictionary = Godot.Collections.Dictionary;

namespace EGP.Networking;

/// <summary>High-level AIO node. Uses the same validated codec as GDScript and C++.</summary>
public partial class NetNode : Node, ISerializationListener
{
    private Node? bridge;
    private SignalLinks? links;
    private bool restoreSignals;
    private bool autoPoll = true;
    [Export] public bool AutoPoll { get => autoPoll; set { autoPoll = value; if (GodotObject.IsInstanceValid(bridge)) bridge!.Set("auto_poll", value); } }
    public event Action<string>? StateChanged;
    public event Action<long>? PeerConnected;
    public event Action<long>? PeerDisconnected;
    public event Action<long, int, Dictionary>? EntitySpawned;
    public event Action<long, Dictionary>? EntityChanged;
    public event Action<long>? EntityDespawned;
    public event Action<long, StringName, Array>? MessageReceived;
    public event Action<long, long, Dictionary>? InputReceived;
    public event Action<long, byte[], int, Delivery>? PacketReceived;
    public event Action<long, bool>? SimulationTick;
    public event Action<string>? Diagnostic;
    public Node Bridge
    {
        get
        {
            if (GodotObject.IsInstanceValid(bridge)) return bridge!;
            DisconnectBridgeSignals();
            bridge = (Node)Shared.New("res://addons/egp_net/egp_net.gd");
            bridge.Name = "EGPNetBridge"; bridge.Set("auto_poll", autoPoll);
            AddChild(bridge);
            ConnectBridgeSignals();
            return bridge;
        }
    }
    private void ConnectBridgeSignals()
    {
        if (links != null || !GodotObject.IsInstanceValid(bridge)) return;
        links = new(bridge!);
        try
        {
            links.Add("state_changed", new Callable(this, nameof(ForwardState)));
            links.Add("peer_connected", new Callable(this, nameof(ForwardPeerConnected)));
            links.Add("peer_disconnected", new Callable(this, nameof(ForwardPeerDisconnected)));
            links.Add("entity_spawned", new Callable(this, nameof(ForwardSpawn)));
            links.Add("entity_changed", new Callable(this, nameof(ForwardEntity)));
            links.Add("entity_despawned", new Callable(this, nameof(ForwardDespawn)));
            links.Add("message_received", new Callable(this, nameof(ForwardMessage)));
            links.Add("input_received", new Callable(this, nameof(ForwardInput)));
            links.Add("packet_received", new Callable(this, nameof(ForwardPacket)));
            links.Add("simulation_tick", new Callable(this, nameof(ForwardTick)));
            links.Add("diagnostic", new Callable(this, nameof(ForwardDiagnostic)));
            restoreSignals = true;
        }
        catch { DisconnectBridgeSignals(); throw; }
    }
    private void DisconnectBridgeSignals()
    {
        links?.Dispose();
        links = null;
    }
    // Object/method callables retain stable native identity during managed teardown.
    private void ForwardState(string state) => StateChanged?.Invoke(state);
    private void ForwardPeerConnected(long peer) => PeerConnected?.Invoke(peer);
    private void ForwardPeerDisconnected(long peer) => PeerDisconnected?.Invoke(peer);
    private void ForwardSpawn(long entity, long kind, Dictionary state) => EntitySpawned?.Invoke(entity, (int)kind, state);
    private void ForwardEntity(long entity, Dictionary state) => EntityChanged?.Invoke(entity, state);
    private void ForwardDespawn(long entity) => EntityDespawned?.Invoke(entity);
    private void ForwardMessage(long peer, StringName name, Array args) => MessageReceived?.Invoke(peer, name, args);
    private void ForwardInput(long peer, long entity, Dictionary input) => InputReceived?.Invoke(peer, entity, input);
    private void ForwardPacket(long peer, byte[] data, long channel, long delivery) => PacketReceived?.Invoke(peer, data, (int)channel, (Delivery)delivery);
    private void ForwardTick(long tick, bool server) => SimulationTick?.Invoke(tick, server);
    private void ForwardDiagnostic(string message) => Diagnostic?.Invoke(message);
    public override void _EnterTree() => ConnectBridgeSignals();
    public override void _ExitTree()
    {
        try { Close(); }
        finally { DisconnectBridgeSignals(); restoreSignals = false; }
    }
    /// <summary>Preserves the codec/session; derived overrides must call base.</summary>
    public virtual void OnBeforeSerialize()
    {
        restoreSignals = links != null;
        DisconnectBridgeSignals();
    }
    /// <summary>Restores forwarding. Reattach application event handlers in their owner's reload hook.</summary>
    public virtual void OnAfterDeserialize()
    {
        if (restoreSignals) ConnectBridgeSignals();
    }
    public Error Configure(NetOptions? options = null) => Configure((options ?? new()).ToDictionary());
    public Error Configure(Dictionary options) => Shared.Error(Bridge, "configure", options);
    public Error Host(int port = 10515, string bindAddress = "0.0.0.0") => Shared.Error(Bridge, "host", port, bindAddress);
    public Error JoinLoopback(string address, int port = 10515) => Shared.Error(Bridge, "join", address, port);
    public Error JoinToken(long clientId, byte[] token, string bindAddress = "0.0.0.0") => Shared.Error(Bridge, "join_token", clientId, token, bindAddress);
    public TokenResult IssueToken(long clientId, string publicAddress) => TokenResult.Read(Bridge.Call("issue_token", clientId, publicAddress).AsGodotDictionary());
    public Error Poll() => Shared.Error(Bridge, "poll");
    public void Stop() { if (GodotObject.IsInstanceValid(bridge)) bridge!.Call("stop"); }
    public void Close() { if (GodotObject.IsInstanceValid(bridge)) bridge!.Call("close"); }
    public bool IsServer => Bridge.Call("is_server").AsBool();
    public string State => Bridge.Call("get_state").AsString();
    public Dictionary Statistics => Bridge.Call("get_statistics").AsGodotDictionary();
    public int TickRate => Bridge.Call("get_tick_rate").AsInt32();
    public string SimulationFingerprint => Bridge.Call("get_simulation_fingerprint").AsString();
    public GodotObject? NativeSession => Bridge.Get("session").AsGodotObject();
    public Array GetPeers() => Bridge.Call("get_peers").AsGodotArray();
    public long Spawn(int kind, Dictionary? state = null, long authorityPeer = -1) => Bridge.Call("spawn", kind, state ?? new(), authorityPeer).AsInt64();
    public Error UpdateEntity(long entity, Dictionary state) => Shared.Error(Bridge, "update_entity", entity, state);
    public Error Despawn(long entity) => Shared.Error(Bridge, "despawn", entity);
    public Error SetEntityVisible(long entity, long peer, bool visible) => Shared.Error(Bridge, "set_entity_visible", entity, peer, visible);
    public Array GetEntities() => Bridge.Call("get_entities").AsGodotArray();
    public Dictionary GetEntity(long entity) => Bridge.Call("get_entity", entity).AsGodotDictionary();
    public Error RegisterScene(int kind, PackedScene scene, Node parent) => Shared.Error(Bridge, "register_scene", kind, scene, parent);
    public Error RegisterMessage(StringName name, Callable handler, Sender sender = Sender.Both) => Shared.Error(Bridge, "register_message", name, handler, (int)sender);
    public void UnregisterMessage(StringName name) => Bridge.Call("unregister_message", name);
    public Error SendMessage(long peer, StringName name, Array? arguments = null) => Shared.Error(Bridge, "send_message", peer, name, arguments ?? new());
    public Error BroadcastMessage(StringName name, Array? arguments = null) => Shared.Error(Bridge, "broadcast_message", name, arguments ?? new());
    public Error SendInput(long entity, Dictionary input) => Shared.Error(Bridge, "send_input", entity, input);
    public Error SendPacket(long peer, byte[] payload, int channel = 0, Delivery delivery = Delivery.ReliableOrdered) => Shared.Error(Bridge, "send_packet", peer, payload, channel, (int)delivery);
    public Error BroadcastPacket(byte[] payload, int channel = 0, Delivery delivery = Delivery.ReliableOrdered) => Shared.Error(Bridge, "broadcast_packet", payload, channel, (int)delivery);
    public Error DisconnectPeer(long peer) => Shared.Error(Bridge, "disconnect_peer", peer);
}
