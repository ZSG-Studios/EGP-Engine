using System.Buffers.Binary;
using System.Security.Cryptography;
using System.Text;
using System.Reflection;
using LiteEntitySystem;

namespace EGP.Networking;

internal static class Compatibility
{
    // LES's own hash omits field names, class names, RPC method names and game semantics.
    // Combine the game's explicit protocol version with type identities and the LES field hash.
    internal static byte[] Fingerprint(EntityTypesMap types, SessionOptions options)
    {
        var classes = types.RegisteredTypes.OrderBy(t => t.Value.ClassId)
            .Select(t => $"{t.Value.ClassId}:{DescribeEntity(t.Key)}");
        return SHA256.HashData(Encoding.UTF8.GetBytes(string.Join("\n",
            "egp-wire-1|litenetlib-ddacf9b|liteentitysystem-d7448a8|hooks-1",
            options.GameProtocol, options.SimulationFingerprint,
            options.TickRate.ToString(), ((byte)options.SendRate).ToString(),
            types.EvaluateEntityClassDataHash().ToString(), string.Join("\n", classes))));
    }

    private static string DescribeEntity(Type type)
    {
        var result = new StringBuilder();
        for (var current = type; current != null && current != typeof(object); current = current.BaseType)
        {
            result.Append(current.FullName).Append('|');
            foreach (var attribute in current.GetCustomAttributesData()
                .Where(a => a.AttributeType.Namespace == "LiteEntitySystem").OrderBy(a => a.AttributeType.FullName))
                result.Append(attribute).Append('|');
            foreach (var argument in current.GenericTypeArguments) result.Append(DescribeValue(argument)).Append('|');
            foreach (var field in current.GetFields(BindingFlags.Instance | BindingFlags.Static |
                BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.DeclaredOnly).OrderBy(f => f.MetadataToken))
            {
                bool syncVar = field.FieldType.IsGenericType && field.FieldType.GetGenericTypeDefinition() == typeof(SyncVar<>);
                bool syncable = typeof(SyncableField).IsAssignableFrom(field.FieldType);
                bool rpc = LiteEntitySystem.Utils.IsRemoteCallType(field.FieldType);
                if (!syncVar && !syncable && !rpc) continue;
                result.Append(field.Name).Append(':').Append(field.FieldType.FullName).Append(':');
                var flags = field.GetCustomAttribute<SyncVarFlags>();
                result.Append(flags == null ? "default" : ((int)flags.Flags).ToString()).Append(':');
                foreach (var argument in field.FieldType.GenericTypeArguments) result.Append(DescribeValue(argument));
                if (syncable) result.Append(DescribeEntity(field.FieldType));
                result.Append('|');
            }
        }
        return result.ToString();
    }

    private static string DescribeValue(Type type)
    {
        if (!type.IsValueType || type.IsPrimitive || type.IsEnum) return type.FullName;
        return type.FullName + ":" + type.StructLayoutAttribute?.Value + ":" + type.StructLayoutAttribute?.Pack +
            ":" + type.StructLayoutAttribute?.Size + "{" + string.Join(",", type.GetFields(BindingFlags.Public | BindingFlags.NonPublic |
            BindingFlags.Instance).OrderBy(f => f.MetadataToken).Select(f => f.Name + ":" + DescribeValue(f.FieldType))) + "}";
    }

    internal static byte[] Encode(byte[] fingerprint, string token)
    {
        byte[] secret = Encoding.UTF8.GetBytes(token);
        byte[] data = new byte[38 + secret.Length];
        "EGP1"u8.CopyTo(data);
        fingerprint.CopyTo(data, 4);
        BinaryPrimitives.WriteUInt16LittleEndian(data.AsSpan(36), (ushort)secret.Length);
        secret.CopyTo(data, 38);
        return data;
    }

    internal static bool Validate(ReadOnlySpan<byte> data, byte[] fingerprint, string token)
    {
        if (data.Length is < 38 or > 550 || !data[..4].SequenceEqual("EGP1"u8) ||
            !CryptographicOperations.FixedTimeEquals(data.Slice(4, 32), fingerprint) ||
            BinaryPrimitives.ReadUInt16LittleEndian(data[36..]) != data.Length - 38)
            return false;
        return CryptographicOperations.FixedTimeEquals(SHA256.HashData(data[38..]),
            SHA256.HashData(Encoding.UTF8.GetBytes(token)));
    }
}
