using System.Reflection;
using LiteEntitySystem;
using LiteEntitySystem.Internal;

static class RpcDecoderChecks
{
    private delegate void Decoder(InternalBaseClass entity, ReadOnlySpan<byte> payload);
    private static Decoder Bind(Type rpcType, Delegate action)
    {
        var factory = rpcType.GetMethod("CreateMCD", BindingFlags.Static | BindingFlags.NonPublic)!;
        var implementation = (Delegate)factory.MakeGenericMethod(typeof(TestMarker)).Invoke(null, new object[] { action })!;
        return (Decoder)Delegate.CreateDelegate(typeof(Decoder), implementation.Target, implementation.Method);
    }
    public static void Run(Action<bool, string> check)
    {
        int calls = 0, value = 0;
        var typed = Bind(typeof(RemoteCall<int>), (Action<TestMarker, int>)((_, input) => { calls++; value = input; }));
        foreach (int size in new[] { 0, 1, 3, 5, 8 })
        {
            try { typed(null!, new byte[size]); throw new Exception("Malformed typed RPC accepted"); }
            catch (InvalidDataException) { }
        }
        check(calls == 0, "typed RPC rejects short and oversized payloads before game callback");
        typed(null!, BitConverter.GetBytes(123));
        check(calls == 1 && value == 123, "typed RPC decoder accepts exact schema payload");
        var span = Bind(typeof(RemoteCallSpan<int>), (SpanAction<TestMarker, int>)((_, input) => calls += input.Length));
        try { span(null!, new byte[5]); throw new Exception("Partial span RPC accepted"); }
        catch (InvalidDataException) { check(calls == 1, "span RPC rejects partial elements before game callback"); }
        var empty = Bind(typeof(RemoteCall), (Action<TestMarker>)(_ => calls++));
        try { empty(null!, new byte[1]); throw new Exception("Parameterless RPC payload accepted"); }
        catch (InvalidDataException) { check(calls == 1, "parameterless RPC rejects unexpected payload"); }
    }
}
