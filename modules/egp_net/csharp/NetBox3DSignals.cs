// SPDX-License-Identifier: MIT
using Godot;

namespace EGP.Networking;

// Released by the wrapper before assembly unload; no delegate callable survives it.
internal partial class NetBox3DSignals : RefCounted
{
    internal NetBox3D? Owner;
    public void OnBeforeStep(long tick) => Owner?.ReceiveBeforeStep(tick);
    public void OnAfterStep(long tick) => Owner?.ReceiveAfterStep(tick);
    public void OnFailed(long error) => Owner?.ReceiveFailed(error);
}
