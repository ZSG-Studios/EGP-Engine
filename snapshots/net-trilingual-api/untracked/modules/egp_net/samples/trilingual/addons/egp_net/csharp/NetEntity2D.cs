// SPDX-License-Identifier: MIT
using System;
using Godot;
using Dictionary = Godot.Collections.Dictionary;
namespace EGP.Networking;

public partial class NetEntity2D : Node2D
{
    [Export] public float SmoothingSpeed { get; set; } = 15;
    public Dictionary NetworkState { get; private set; } = new();
    public event Action<Dictionary>? StateApplied;
    private Transform2D target;
    private bool hasTarget;
    public void apply_network_state(Dictionary state) => ApplyNetworkState(state);
    public void ApplyNetworkState(Dictionary state)
    {
        NetworkState = state.Duplicate(true);
        if (state.TryGetValue("transform", out var transform) && transform.VariantType == Variant.Type.Transform2D)
        {
            target = transform.AsTransform2D();
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
