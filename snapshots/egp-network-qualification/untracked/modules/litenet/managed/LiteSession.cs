#nullable enable
using System.Diagnostics;
using System.Net;
using LiteEntitySystem;
using LiteEntitySystem.Transport;
using LiteNetLib;
using LiteNetLib.Utils;

namespace EGP.Networking;

public enum SessionState { Stopped, Listening, Connecting, Synchronizing, Connected, Disconnected }
public readonly record struct SessionStatistics(int Peers, long ReceivedPackets, long ReceivedBytes,
    long RejectedPackets, int LatencyMs, ushort LogicTick, ushort ServerTick, int BufferedStates);

/// <summary>Single-threaded game API. Call Pump on the owner thread; transport socket threads never run game code.</summary>
public sealed class LiteSession : IDisposable
{
    public const byte EntityPacketHeader = 0xE1;
    public const byte ApplicationPacketHeader = 0xE2;
    private readonly int _ownerThread = Environment.CurrentManagedThreadId;
    private readonly EntityTypesMap _types;
    private readonly byte[] _fingerprint;
    private readonly Dictionary<int, PeerState> _peers = new();
    private NetManager? _transport;
    private AuthenticatedPacketLayer? _protection;
    private bool _server, _disposed, _pumping;
    private long _connectingSince, _receivedPackets, _receivedBytes, _rejectedPackets;
    private int _latency;

    private sealed class PeerState(NetPeer peer, AbstractNetPeer adapter)
    {
        internal readonly NetPeer Peer = peer;
        internal readonly AbstractNetPeer Adapter = adapter;
        internal NetPlayer? Player;
        internal long Window = Stopwatch.GetTimestamp();
        internal int Packets;
        internal long Bytes;
    }

    public SessionOptions Options { get; }
    public SessionState State { get; private set; }
    public int LocalPort => _transport?.LocalPort ?? 0;
    public ServerEntityManager? Server { get; private set; }
    public ClientEntityManager? Client { get; private set; }
    public string CompatibilityFingerprint => Convert.ToHexString(_fingerprint);
    public string LastDisconnectReason { get; private set; } = "";
    public event Action<ServerEntityManager, NetPlayer>? PlayerJoined;
    public event Action<byte>? PlayerLeft;
    public event Action<ClientEntityManager>? ClientCreated;
    public event Action<SessionState>? StateChanged;
    public event Action<int, byte[]>? ApplicationReceived;
    public event Action<string>? Diagnostic;

    public LiteSession(EntityTypesMap types, SessionOptions? options = null)
    {
        _types = types ?? throw new ArgumentNullException(nameof(types));
        Options = options ?? new SessionOptions();
        Options.Validate();
        if (types.RegisteredTypes.Count == 0) throw new ArgumentException("Register entity types before starting a session.");
        _fingerprint = Compatibility.Fingerprint(types, Options);
    }

    public SessionStatistics Statistics => new(_peers.Count, _receivedPackets, _receivedBytes,
        _rejectedPackets, _latency, Server?.Tick ?? Client?.Tick ?? 0, Client?.ServerTick ?? Server?.Tick ?? 0,
        Client?.LerpBufferCount ?? 0);

    public void Listen(int port = 10515, string bindAddress = "0.0.0.0")
    {
        PrepareStart();
        if (port is < 0 or > 65535) throw new ArgumentOutOfRangeException(nameof(port));
        var binding = IPAddress.Parse(bindAddress);
        if (binding.AddressFamily != System.Net.Sockets.AddressFamily.InterNetwork)
            throw new ArgumentException("Use an IPv4 bind address; wildcard servers also accept IPv6.", nameof(bindAddress));
        _server = true;
        Server = new ServerEntityManager(_types, EntityPacketHeader, Options.TickRate, Options.SendRate, Options.History)
        { SafeEntityUpdate = false };
        _transport = CreateTransport();
        // A loopback or specific-interface bind must not expose an IPv6 wildcard socket.
        bool loopback = IPAddress.IsLoopback(binding);
        _transport.IPv6Enabled = loopback || binding.Equals(IPAddress.Any);
        try
        {
            if (!_transport.Start(binding, loopback ? IPAddress.IPv6Loopback : IPAddress.IPv6Any, port))
                throw new IOException("Could not bind LiteNetLib server socket.");
        }
        catch { Stop(); throw; }
        ChangeState(SessionState.Listening);
    }

    public void Connect(string host, int port = 10515)
    {
        PrepareStart();
        if (port is < 1 or > 65535) throw new ArgumentOutOfRangeException(nameof(port));
        ArgumentException.ThrowIfNullOrWhiteSpace(host);
        _server = false;
        _transport = CreateTransport();
        if (!_transport.Start()) { Stop(); throw new IOException("Could not bind LiteNetLib client socket."); }
        _connectingSince = Stopwatch.GetTimestamp();
        ChangeState(SessionState.Connecting);
        try
        {
            var writer = new NetDataWriter();
            writer.Put(Compatibility.Encode(_fingerprint, Options.AccessToken));
            _transport.Connect(host, port, writer);
        }
        catch { Stop(); throw; }
    }

    public void Pump()
    {
        AssertOwner();
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_transport == null) return;
        if (_pumping) throw new InvalidOperationException("Network Pump is not reentrant.");
        _pumping = true;
        try
        {
            _transport.PollEvents();
            Server?.Update();
            Client?.Update();
            if (State == SessionState.Synchronizing && Client?.Tickrate > 0)
                ChangeState(SessionState.Connected);
            if ((State == SessionState.Connecting || State == SessionState.Synchronizing) &&
                Stopwatch.GetElapsedTime(_connectingSince).TotalMilliseconds > Options.HandshakeTimeoutMs)
            {
                Stop();
                LastDisconnectReason = "Handshake or baseline timed out";
                ChangeState(SessionState.Disconnected);
            }
        }
        finally { _pumping = false; }
    }

    /// <summary>Bounded reliable application channel, separate from LES state traffic. Peer IDs are transport IDs.</summary>
    public void SendApplication(int peerId, ReadOnlySpan<byte> payload)
    {
        AssertOwner();
        if (payload.Length > Options.MaxPacketBytes - 1) throw new ArgumentOutOfRangeException(nameof(payload));
        if (!_peers.TryGetValue(peerId, out var peer)) throw new ArgumentException("Peer is not connected.");
        byte[] packet = new byte[payload.Length + 1];
        packet[0] = ApplicationPacketHeader;
        payload.CopyTo(packet.AsSpan(1));
        peer.Peer.Send(packet, 1, DeliveryMethod.ReliableOrdered);
    }

    public void Disconnect(int peerId)
    {
        AssertOwner();
        if (_peers.TryGetValue(peerId, out var peer)) _transport!.DisconnectPeer(peer.Peer);
    }

    public void Stop()
    {
        AssertOwner();
        _transport?.Stop();
        _transport = null;
        _protection?.Dispose();
        _protection = null;
        Server?.Reset();
        Client?.Reset();
        Server = null;
        Client = null;
        _peers.Clear();
        ChangeState(SessionState.Stopped);
    }

    public void Dispose()
    {
        AssertOwner();
        if (_disposed) return;
        Stop();
        _disposed = true;
    }

    private void PrepareStart()
    {
        AssertOwner();
        ObjectDisposedException.ThrowIf(_disposed, this);
        if (_transport != null) throw new InvalidOperationException("Stop the previous session before starting again.");
        LastDisconnectReason = "";
    }

    private NetManager CreateTransport()
    {
        var listener = new EventBasedNetListener();
        listener.ConnectionRequestEvent += request =>
        {
            if (!_server || _transport!.ConnectedPeersCount >= Options.MaxPlayers ||
                !Compatibility.Validate(request.Data.GetRemainingBytesSpan(), _fingerprint, Options.AccessToken))
            { request.Reject(); return; }
            request.Accept();
        };
        listener.PeerConnectedEvent += OnConnected;
        listener.PeerDisconnectedEvent += OnDisconnected;
        listener.NetworkReceiveEvent += OnReceive;
        listener.NetworkErrorEvent += (_, error) => Diagnostic?.Invoke($"LiteNetLib socket error: {error}");
        listener.NetworkLatencyUpdateEvent += (_, latency) => _latency = latency;
        _protection = Options.TransportKey == null ? null : new AuthenticatedPacketLayer(Options.TransportKey);
        return new NetManager(listener, _protection)
        {
            AutoRecycle = true, ChannelsCount = 2, EnableStatistics = true,
            DisconnectTimeout = Options.DisconnectTimeoutMs,
            UpdateTime = 5, MaxFragmentsCount = (ushort)Math.Min(32768, (Options.MaxPacketBytes + 999) / 1000),
            SimulatePacketLoss = Options.SimulatedLossPercent > 0,
            SimulationPacketLossChance = Options.SimulatedLossPercent,
            SimulateLatency = Options.SimulatedLatencyMaxMs > 0,
            SimulationMinLatency = Options.SimulatedLatencyMinMs,
            SimulationMaxLatency = Options.SimulatedLatencyMaxMs
        };
    }

    private void OnConnected(NetPeer peer)
    {
        var adapter = new LiteNetLibNetPeer(peer, false);
        var state = new PeerState(peer, adapter);
        if (_server)
        {
            if (Server!.PlayersCount >= Options.MaxPlayers || (state.Player = Server.AddPlayer(adapter)) == null)
            { _transport!.DisconnectPeer(peer); return; }
            _peers.Add(peer.Id, state);
            PlayerJoined?.Invoke(Server, state.Player);
        }
        else
        {
            _peers.Add(peer.Id, state);
            Client = new ClientEntityManager(_types, adapter, EntityPacketHeader, Options.History);
            ClientCreated?.Invoke(Client);
            ChangeState(SessionState.Synchronizing);
        }
    }

    private void OnDisconnected(NetPeer peer, DisconnectInfo info)
    {
        if (_peers.Remove(peer.Id, out var state) && state.Player != null)
        {
            Server!.RemovePlayer(state.Player);
            PlayerLeft?.Invoke(state.Player.Id);
        }
        if (!_server)
        {
            Client?.Reset();
            Client = null;
            LastDisconnectReason = info.Reason.ToString();
            ChangeState(SessionState.Disconnected);
        }
    }

    private void OnReceive(NetPeer peer, NetPacketReader reader, byte channel, DeliveryMethod method)
    {
        if (!_peers.TryGetValue(peer.Id, out var state) || !ReferenceEquals(state.Peer, peer) ||
            peer.ConnectionState != ConnectionState.Connected) return;
        var packet = reader.GetRemainingBytesSpan();
        _receivedPackets++;
        _receivedBytes += packet.Length;
        if (Stopwatch.GetElapsedTime(state.Window).TotalSeconds >= 1)
        { state.Window = Stopwatch.GetTimestamp(); state.Bytes = 0; state.Packets = 0; }
        state.Packets++;
        state.Bytes += packet.Length;
        if (packet.Length is < 1 || packet.Length > Options.MaxPacketBytes ||
            state.Packets > Options.PacketsPerSecond || state.Bytes > Options.BytesPerSecond)
        { Reject(peer); return; }
        if (channel == 1 && packet[0] == ApplicationPacketHeader && method == DeliveryMethod.ReliableOrdered)
        { ApplicationReceived?.Invoke(peer.Id, packet[1..].ToArray()); return; }
        if (channel != 0 || packet.Length < 2 || packet[0] != EntityPacketHeader ||
            (method != DeliveryMethod.ReliableOrdered && method != DeliveryMethod.Unreliable))
        { Reject(peer); return; }
        try
        {
            DeserializeResult result = _server ? Server!.Deserialize(state.Player!, packet) : Client!.Deserialize(packet);
            if (result != DeserializeResult.Done) Reject(peer);
        }
        catch (Exception error) when (error is ArgumentException or IndexOutOfRangeException or InvalidOperationException or InvalidDataException)
        {
            Diagnostic?.Invoke($"Malformed LES packet: {error.GetType().Name}");
            Reject(peer);
        }
    }

    private void Reject(NetPeer peer)
    {
        _rejectedPackets++;
        _transport!.DisconnectPeer(peer);
    }

    private void ChangeState(SessionState state)
    {
        if (State == state) return;
        State = state;
        StateChanged?.Invoke(state);
    }

    private void AssertOwner()
    {
        if (Environment.CurrentManagedThreadId != _ownerThread)
            throw new InvalidOperationException("Networking and entity operations must run on the session owner thread.");
    }
}
