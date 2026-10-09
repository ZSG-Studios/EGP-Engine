// SPDX-License-Identifier: MIT
using System;
using Godot;

namespace EGP.Networking;

/// <summary>Bounded prediction, authoritative correction and input replay.</summary>
public sealed class NetPrediction : IDisposable
{
    public GodotObject Native { get; } = Shared.New("res://addons/egp_net/egp_net_prediction.gd");
    private readonly SignalLinks links;
    private bool disposed;
    public event Action<long, int>? Corrected;
    public event Action<Error>? ResyncRequired;
    public NetPrediction()
    {
        links = new(Native);
        links.Add("corrected", Callable.From<long, long>((tick, replayed) => Corrected?.Invoke(tick, (int)replayed)));
        links.Add("resync_required", Callable.From<long>(error => ResyncRequired?.Invoke((Error)error)));
    }
    public Error Configure(Func<byte[]> capture, Func<byte[], Error> restore, Func<long, byte[], bool, Error> simulate,
        long initialTick = 0, int maxTicks = 128, int maxStateBytes = 65536, int maxHistoryBytes = 8388608)
        => Shared.Error(Native, "configure", Callable.From(capture), Callable.From<byte[], int>(bytes => (int)restore(bytes)),
            Callable.From<long, byte[], bool, int>((tick, input, replay) => (int)simulate(tick, input, replay)),
            initialTick, maxTicks, maxStateBytes, maxHistoryBytes);
    public Error Predict(long tick, byte[] input) => Shared.Error(Native, "predict", tick, input);
    public Error Reconcile(long acknowledgedTick, byte[] state) => Shared.Error(Native, "reconcile", acknowledgedTick, state);
    public Error Reset(long tick, byte[] state) => Shared.Error(Native, "reset", tick, state);
    public int PendingTicks => Native.Call("get_pending_ticks").AsInt32();
    public int HistoryBytes => Native.Call("get_history_bytes").AsInt32();
    public void Dispose() { if (disposed) return; disposed = true; links.Dispose(); Native.Dispose(); }
}
