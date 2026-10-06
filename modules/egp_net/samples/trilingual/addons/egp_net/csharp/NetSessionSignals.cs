// SPDX-License-Identifier: MIT
using Godot;

namespace EGP.Networking;

// Named object methods keep native signal identity stable during delegate teardown.
// The owning NetSession disconnects and releases this bridge before assembly unload.
internal partial class NetSessionSignals : RefCounted
{
    internal NetSession? Owner;
    public void OnStateChanged(string state) => Owner?.ReceiveStateChanged(state);
    public void OnPeerConnected(long peer) => Owner?.ReceivePeerConnected(peer);
    public void OnPeerDisconnected(long peer) => Owner?.ReceivePeerDisconnected(peer);
    public void OnApplicationReceived(long peer, byte[] data) => Owner?.ReceiveApplication(peer, data);
    public void OnPacketReceived(long peer, byte[] data, long channel, long delivery) => Owner?.ReceivePacket(peer, data, channel, delivery);
    public void OnSimulationTick(long tick, bool authority) => Owner?.ReceiveTick(tick, authority);
    public void OnDiagnostic(string message) => Owner?.ReceiveDiagnostic(message);
}
