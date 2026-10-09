// SPDX-License-Identifier: MIT
using System;
using EGP.Networking;
using LiteEntitySystem;
using Dictionary = Godot.Collections.Dictionary;

namespace Godot;

/// <summary>Retain this object for the lifetime of the native session facade.
/// C#, GDScript and native callers all use the same LiteSession.</summary>
public sealed class EGPLiteRuntime : IDisposable
{
    public LiteSession Session { get; }
    public GodotObject Bridge { get; }
    private bool _disposed;

    public EGPLiteRuntime(EntityTypesMap types, SessionOptions options = null)
    {
        if (!ClassDB.ClassExists("EGPLiteSession"))
            throw new InvalidOperationException("Build EGP with module_litenet_enabled=yes and module_mono_enabled=yes.");
        Session = new LiteSession(types, options);
        LiteEntitySystem.Logger.LoggerImpl ??= new GodotLiteLogger();
        Bridge = ClassDB.Instantiate("EGPLiteSession").AsGodotObject();
        var dispatcher = Callable.From<StringName, Dictionary, Variant>(Dispatch);
        Error error = (Error)Bridge.Call("attach_runtime", dispatcher).AsInt32();
        if (error != Error.Ok) { Session.Dispose(); Bridge.Dispose(); throw new InvalidOperationException(error.ToString()); }
        Session.StateChanged += state => Bridge.EmitSignal("state_changed", state.ToString());
        Session.PlayerJoined += (_, player) => Bridge.EmitSignal("player_joined", player.Id);
        Session.PlayerLeft += player => Bridge.EmitSignal("player_left", player);
        Session.ApplicationReceived += (peer, payload) => Bridge.EmitSignal("application_received", peer, payload);
        Session.Diagnostic += message => { GD.PushWarning(message); Bridge.EmitSignal("diagnostic", message); };
    }

    private Variant Dispatch(StringName operation, Dictionary arguments)
    {
        try
        {
            switch (operation.ToString())
            {
                case "listen": Session.Listen(arguments["port"].AsInt32(), arguments["bind_address"].AsString()); break;
                case "connect": Session.Connect(arguments["host"].AsString(), arguments["port"].AsInt32()); break;
                case "poll": Session.Pump(); break;
                case "stop": Session.Stop(); break;
                case "send_application": Session.SendApplication(arguments["peer"].AsInt32(), arguments["payload"].AsByteArray()); break;
                case "state": return Session.State.ToString();
                case "statistics":
                    var stats = Session.Statistics;
                    return new Dictionary
                    {
                        ["peers"] = stats.Peers, ["received_packets"] = stats.ReceivedPackets,
                        ["received_bytes"] = stats.ReceivedBytes, ["rejected_packets"] = stats.RejectedPackets,
                        ["latency_ms"] = stats.LatencyMs, ["logic_tick"] = stats.LogicTick,
                        ["server_tick"] = stats.ServerTick, ["buffered_states"] = stats.BufferedStates,
                        ["local_port"] = Session.LocalPort, ["compatibility_fingerprint"] = Session.CompatibilityFingerprint
                    };
                default: return (int)Error.InvalidParameter;
            }
            return (int)Error.Ok;
        }
        catch (Exception error)
        {
            GD.PushError($"EGP networking {operation}: {error.Message}");
            // Fail closed on a broken simulation/rollback. Never continue with stale state.
            if (operation.ToString() == "poll") Session.Stop();
            return (int)Error.Failed;
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        Bridge.Call("detach_runtime");
        Session.Dispose();
        Bridge.Dispose();
        _disposed = true;
    }
}

internal sealed class GodotLiteLogger : LiteEntitySystem.ILogger
{
    public void Log(string message) => GD.Print(message);
    public void LogWarning(string message) => GD.PushWarning(message);
    public void LogError(string message) => GD.PushError(message);
}

/// <summary>Dynamic facade keeps managed bindings independent of native API generation order.
/// Full solver snapshots are for trusted local rollback only.</summary>
public sealed class EGPBox3DPhysicsWorld : IFixedPhysicsWorld
{
    public GodotObject NativeWorld { get; }
    public EGPBox3DPhysicsWorld(GodotObject nativeWorld)
    {
        if (nativeWorld == null || !nativeWorld.IsClass("EGPBox3DWorld"))
            throw new ArgumentException("Expected EGPBox3DWorld.", nameof(nativeWorld));
        NativeWorld = nativeWorld;
    }
    public ulong NextTick => checked((ulong)NativeWorld.Call("get_tick").AsInt64() + 1);
    public string SimulationFingerprint => NativeWorld.Call("get_simulation_fingerprint").AsString();
    public int TickRate
    {
        get
        {
            foreach (string part in SimulationFingerprint.Split(':'))
                if (part.StartsWith("hz", StringComparison.Ordinal) && int.TryParse(part.AsSpan(2), out int rate)) return rate;
            throw new InvalidOperationException("Box3D simulation fingerprint is missing its fixed tick rate.");
        }
    }
    public byte[] CaptureSnapshot()
    {
        byte[] data = NativeWorld.Call("capture_snapshot").AsByteArray();
        if (data.Length == 0) throw new InvalidOperationException("Box3D snapshot failed; flush pending commands at a simulation tick boundary.");
        return data;
    }
    public void RestoreSnapshot(byte[] snapshot) => RequireOk(NativeWorld.Call("restore_snapshot", snapshot), "restore_snapshot");
    public void StepTick(ulong expectedTick) => RequireOk(NativeWorld.Call("step_tick", checked((long)expectedTick)), "step_tick");
    private static void RequireOk(Variant result, string operation)
    {
        Error error = (Error)result.AsInt32();
        if (error != Error.Ok) throw new InvalidOperationException($"Box3D {operation} failed: {error}");
    }
}
