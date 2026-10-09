// SPDX-License-Identifier: MIT
using System;
using Godot;

namespace EGP.Networking;

/// <summary>Server tick adapter for the explicit EGPBox3DWorld.</summary>
public sealed class NetBox3D : IDisposable
{
    private const string ReloadTokenMetadata = "_egp_csharp_box3d_reload_token";
    private readonly GodotObject native;
    private readonly SignalLinks links;
    private readonly NetBox3DSignals bridge;
    private bool disposed;
    public GodotObject Native => !disposed ? native : throw new ObjectDisposedException(nameof(NetBox3D));
    public event Action<long>? BeforeStep;
    public event Action<long>? AfterStep;
    public event Action<Error>? Failed;
    public NetBox3D() : this(Shared.New("res://addons/egp_net/egp_net_box3d.gd"), true) { }
    private NetBox3D(GodotObject value, bool created)
    {
        native = value;
        links = new(native);
        bridge = new() { Owner = this };
        try
        {
            links.Add("before_step", new Callable(bridge, nameof(NetBox3DSignals.OnBeforeStep)));
            links.Add("after_step", new Callable(bridge, nameof(NetBox3DSignals.OnAfterStep)));
            links.Add("failed", new Callable(bridge, nameof(NetBox3DSignals.OnFailed)));
        }
        catch
        {
            links.Dispose(); bridge.Owner = null; bridge.Dispose();
            if (created) native.Dispose();
            throw;
        }
    }
    /// <summary>Transfer this adapter, its attached world and body mappings to a reload capsule.</summary>
    /// <remarks>
    /// Save the returned dictionary in OnBeforeSerialize. The GDScript adapter remains
    /// attached to its network clock; this wrapper becomes disposed. Disposing the old
    /// wrapper again does not detach the transferred adapter. Use on its Godot thread.
    /// </remarks>
    public Godot.Collections.Dictionary DetachForReload()
    {
        var instance = Native;
        string token = Guid.NewGuid().ToString("N");
        var state = new Godot.Collections.Dictionary { ["version"] = 1, ["adapter"] = instance, ["token"] = token };
        instance.SetMeta(ReloadTokenMetadata, token);
        disposed = true;
        links.Dispose(); bridge.Owner = null; bridge.Dispose();
        BeforeStep = null; AfterStep = null; Failed = null;
        return state;
    }
    /// <summary>Consume a reload capsule and reconnect managed adapter events.</summary>
    /// <remarks>
    /// Call in OnAfterDeserialize on the original Godot thread, then resubscribe
    /// application handlers. Invalid, foreign or consumed capsules throw ArgumentException
    /// and remain unchanged. Copies cannot reclaim ownership. Capsules contain local
    /// object references; they are not a disk or network format or client rollback.
    /// </remarks>
    public static NetBox3D ResumeAfterReload(Godot.Collections.Dictionary state)
    {
        ArgumentNullException.ThrowIfNull(state);
        if (!state.ContainsKey("version") || state["version"].VariantType != Variant.Type.Int || state["version"].AsInt64() != 1
            || !state.ContainsKey("adapter") || state["adapter"].VariantType != Variant.Type.Object
            || !state.ContainsKey("token") || state["token"].VariantType != Variant.Type.String)
            throw new ArgumentException("Expected an unconsumed NetBox3D reload capsule.", nameof(state));
        var instance = state["adapter"].AsGodotObject();
        string token = state["token"].AsString();
        var script = ResourceLoader.Load<Script>("res://addons/egp_net/egp_net_box3d.gd");
        if (!GodotObject.IsInstanceValid(instance) || script == null || instance.GetScript().AsGodotObject() != script
            || string.IsNullOrEmpty(token) || !instance.HasMeta(ReloadTokenMetadata)
            || instance.GetMeta(ReloadTokenMetadata).AsString() != token)
            throw new ArgumentException("The adapter is invalid or this reload capsule was already consumed.", nameof(state));
        var result = new NetBox3D(instance, false);
        instance.RemoveMeta(ReloadTokenMetadata);
        state.Clear();
        return result;
    }
    internal void ReceiveBeforeStep(long tick) => BeforeStep?.Invoke(tick);
    internal void ReceiveAfterStep(long tick) => AfterStep?.Invoke(tick);
    internal void ReceiveFailed(long error) => Failed?.Invoke((Error)error);
    public Error Attach(NetNode net, GodotObject world) => Shared.Error(Native, "attach", net.Bridge, world);
    /// <summary>Map a network entity to a stable body ID; zero uses the entity ID.</summary>
    public Error Track(long entity, long bodyId = 0) => Shared.Error(Native, "track", entity, bodyId);
    public void Untrack(long entity) => Native.Call("untrack", entity);
    public void Detach() => Native.Call("detach");
    public void Dispose()
    {
        if (disposed) return;
        disposed = true;
        links.Dispose(); bridge.Owner = null; bridge.Dispose();
        native.Call("detach"); native.Dispose();
    }
}
