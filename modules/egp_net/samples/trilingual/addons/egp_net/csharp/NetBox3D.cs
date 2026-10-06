// SPDX-License-Identifier: MIT
using System;
using Godot;

namespace EGP.Networking;

/// <summary>Server tick adapter for the explicit EGPBox3DWorld.</summary>
public sealed class NetBox3D : IDisposable
{
    public GodotObject Native { get; } = Shared.New("res://addons/egp_net/egp_net_box3d.gd");
    private readonly SignalLinks links;
    private bool disposed;
    public event Action<long>? BeforeStep;
    public event Action<long>? AfterStep;
    public event Action<Error>? Failed;
    public NetBox3D()
    {
        links = new(Native);
        links.Add("before_step", Callable.From<long>(tick => BeforeStep?.Invoke(tick)));
        links.Add("after_step", Callable.From<long>(tick => AfterStep?.Invoke(tick)));
        links.Add("failed", Callable.From<long>(error => Failed?.Invoke((Error)error)));
    }
    public Error Attach(NetNode net, GodotObject world) => Shared.Error(Native, "attach", net.Bridge, world);
    /// <summary>Map a network entity to a stable body ID; zero uses the entity ID.</summary>
    public Error Track(long entity, long bodyId = 0) => Shared.Error(Native, "track", entity, bodyId);
    public void Untrack(long entity) => Native.Call("untrack", entity);
    public void Detach() => Native.Call("detach");
    public void Dispose() { if (disposed) return; disposed = true; links.Dispose(); Detach(); Native.Dispose(); }
}
