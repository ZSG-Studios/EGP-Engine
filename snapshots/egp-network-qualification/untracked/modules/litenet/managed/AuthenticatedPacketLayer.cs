using System.Net;
using System.Security.Cryptography;
using LiteNetLib.Layers;

namespace EGP.Networking;

/// <summary>Optional AES-256-GCM protection over entire LiteNetLib datagrams.
/// Provision a distinct out-of-band key per match; this is not account authentication.</summary>
public sealed class AuthenticatedPacketLayer : PacketLayerBase, IDisposable
{
    private const int NonceSize = 12, TagSize = 16;
    private readonly AesGcm _encrypt, _decrypt;
    private readonly object _sendLock = new(), _receiveLock = new();

    public AuthenticatedPacketLayer(byte[] key) : base(NonceSize + TagSize)
    {
        if (key == null || key.Length != 32) throw new ArgumentException("Use a 32-byte match transport key.");
        _encrypt = new AesGcm(key, TagSize);
        _decrypt = new AesGcm(key, TagSize);
    }

    public override void ProcessOutBoundPacket(ref IPEndPoint endpoint, ref byte[] data, ref int offset, ref int length)
    {
        byte[] packet = new byte[length + NonceSize + TagSize];
        RandomNumberGenerator.Fill(packet.AsSpan(0, NonceSize));
        lock (_sendLock)
            _encrypt.Encrypt(packet.AsSpan(0, NonceSize), data.AsSpan(offset, length),
                packet.AsSpan(NonceSize, length), packet.AsSpan(NonceSize + length, TagSize));
        data = packet;
        offset = 0;
        length = packet.Length;
    }

    public override void ProcessInboundPacket(ref IPEndPoint endpoint, ref byte[] data, ref int length)
    {
        if (length < NonceSize + TagSize) { length = 0; return; }
        int plaintextLength = length - NonceSize - TagSize;
        byte[] plaintext = new byte[plaintextLength];
        try
        {
            lock (_receiveLock)
                _decrypt.Decrypt(data.AsSpan(0, NonceSize), data.AsSpan(NonceSize, plaintextLength),
                    data.AsSpan(NonceSize + plaintextLength, TagSize), plaintext);
            // Preserve the transport's rented receive buffer and ownership.
            plaintext.CopyTo(data, 0);
            length = plaintextLength;
        }
        catch (CryptographicException) { length = 0; }
        finally { CryptographicOperations.ZeroMemory(plaintext); }
    }

    public void Dispose() { _encrypt.Dispose(); _decrypt.Dispose(); }
}
