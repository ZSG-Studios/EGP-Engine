using LiteEntitySystem;

namespace EGP.Networking;

public sealed record SessionOptions
{
    public byte TickRate { get; init; } = 60;
    public ServerSendRate SendRate { get; init; } = ServerSendRate.EqualToFPS;
    public MaxHistorySize History { get; init; } = MaxHistorySize.Size128;
    public int MaxPlayers { get; init; } = 32;
    public int MaxPacketBytes { get; init; } = 1024 * 1024;
    public int PacketsPerSecond { get; init; } = 1000;
    public int BytesPerSecond { get; init; } = 4 * 1024 * 1024;
    public int DisconnectTimeoutMs { get; init; } = 5000;
    public int HandshakeTimeoutMs { get; init; } = 5000;
    public string GameProtocol { get; init; } = "egp-game-v1";
    public string SimulationFingerprint { get; init; } = "kinematic-v1";
    public string AccessToken { get; init; } = "";
    public byte[] TransportKey { get; init; }
    public int SimulatedLossPercent { get; init; }
    public int SimulatedLatencyMinMs { get; init; }
    public int SimulatedLatencyMaxMs { get; init; }

    internal void Validate()
    {
        if (TickRate == 0 || MaxPlayers is < 1 or > 255 ||
            !Enum.IsDefined(SendRate) || !Enum.IsDefined(History) ||
            MaxPacketBytes is < 1200 or > 16 * 1024 * 1024 ||
            PacketsPerSecond < 1 || BytesPerSecond < MaxPacketBytes ||
            DisconnectTimeoutMs < 500 || HandshakeTimeoutMs < 500 ||
            string.IsNullOrWhiteSpace(GameProtocol) || string.IsNullOrWhiteSpace(SimulationFingerprint) ||
            System.Text.Encoding.UTF8.GetByteCount(AccessToken) > 512 ||
            (TransportKey != null && TransportKey.Length != 32) ||
            SimulatedLossPercent is < 0 or > 100 || SimulatedLatencyMinMs < 0 ||
            SimulatedLatencyMaxMs < SimulatedLatencyMinMs || SimulatedLatencyMaxMs > 5000)
            throw new ArgumentException("Invalid networking session options.");
    }
}
