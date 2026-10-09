// SPDX-License-Identifier: MIT
using System;
using Godot;
using Dictionary = Godot.Collections.Dictionary;
namespace EGP.Networking;

/// <summary>Presentation interpolation; authoritative simulation stays in the tick loop.</summary>
public partial class NetEntity3D : Node3D
{
    [Export] public float SmoothingSpeed { get; set; } = 15;
    public Dictionary NetworkState { get; private set; } = new();
    public event Action<Dictionary>? StateApplied;
    private Transform3D target;
    private bool hasTarget;
    // Shared scene factories use this method name in every language.
    public void apply_network_state(Dictionary state) => ApplyNetworkState(state);
    public void ApplyNetworkState(Dictionary state)
    {
        NetworkState = state.Duplicate(true);
        if (state.TryGetValue("transform", out var transform) && transform.VariantType == Variant.Type.Transform3D)
        {
            target = transform.AsTransform3D();
            if (!hasTarget || SmoothingSpeed <= 0) GlobalTransform = target;
            hasTarget = true;
        }
        StateApplied?.Invoke(NetworkState.Duplicate(true));
    }
    public override void _Process(double delta)
    {
        if (hasTarget) GlobalTransform = SmoothingSpeed > 0
            ? GlobalTransform.InterpolateWith(target, (float)(1 - Math.Exp(-Math.Max(SmoothingSpeed, 0) * delta))) : target;
    }
}
