using LiteEntitySystem;

namespace EGP.Networking;

/// <summary>Trusted local solver state. Snapshot bytes must never be deserialized from a peer.</summary>
public interface IFixedPhysicsWorld
{
    ulong NextTick { get; }
    int TickRate { get; }
    string SimulationFingerprint { get; }
    byte[] CaptureSnapshot();
    void RestoreSnapshot(byte[] snapshot);
    void StepTick(ulong expectedTick);
}

/// <summary>Expands a ushort LES sequence within a half-range window; replay can move backward.</summary>
public sealed class TickSequence
{
    private bool _initialized;
    private ushort _last;
    private long _expanded;
    public long Observe(ushort tick)
    {
        if (!_initialized) { _initialized = true; _expanded = tick; }
        else _expanded += Utils.SequenceDiff(tick, _last);
        _last = tick;
        return _expanded;
    }
    public void Reset() { _initialized = false; _last = 0; _expanded = 0; }
}

/// <summary>Connects one physics world to actual simulation ticks. Apply authoritative SyncVars
/// to physics in the correction callback; rendering must use interpolated entity state.</summary>
public sealed class PhysicsTimeline : IDisposable
{
    private readonly EntityManager _manager;
    private readonly IFixedPhysicsWorld _world;
    private readonly Action _applyAuthoritativeState;
    private readonly Action _publishPhysicsState;
    private readonly int _capacity;
    private readonly long _byteBudget;
    private readonly Dictionary<ushort, byte[]> _history = new();
    private readonly Queue<ushort> _order = new();
    private long _historyBytes;
    private bool _disposed;

    public PhysicsTimeline(EntityManager manager, IFixedPhysicsWorld world,
        Action applyAuthoritativeState, Action publishPhysicsState,
        int historyCapacity = 128, long historyByteBudget = 256 * 1024 * 1024)
    {
        if (historyCapacity is < 2 or > 128 || historyByteBudget < 1)
            throw new ArgumentOutOfRangeException(nameof(historyCapacity));
        _manager = manager ?? throw new ArgumentNullException(nameof(manager));
        _world = world ?? throw new ArgumentNullException(nameof(world));
        if (manager.Tickrate != 0 && manager.Tickrate != world.TickRate)
            throw new ArgumentException("Physics tick rate must match LiteEntitySystem tick rate.");
        _applyAuthoritativeState = applyAuthoritativeState ?? throw new ArgumentNullException(nameof(applyAuthoritativeState));
        _publishPhysicsState = publishPhysicsState ?? throw new ArgumentNullException(nameof(publishPhysicsState));
        _capacity = historyCapacity;
        _byteBudget = historyByteBudget;
        _manager.SimulationTickCompleted += OnTick;
        if (manager is ClientEntityManager client)
        {
            client.PhysicsRollbackStarted += OnRollback;
            client.AuthoritativeStateApplied += OnAuthoritative;
            client.BaselineApplied += OnBaseline;
            if (client.Tickrate != 0) OnBaseline();
        }
    }

    public int StoredSnapshots => _history.Count;
    public long StoredBytes => _historyBytes;

    private void OnTick(ushort tick, UpdateMode mode)
    {
        _world.StepTick(_world.NextTick);
        _publishPhysicsState(); // before the server serializes the tick; also updates replayed SyncVars
        if (_manager.IsClient) Store(tick);
    }

    private void OnRollback(ushort processedTick)
    {
        if (!_history.TryGetValue(processedTick, out var snapshot))
            throw new InvalidOperationException($"Physics history lacks processed input tick {processedTick}. Resynchronize the session.");
        _world.RestoreSnapshot(snapshot);
    }

    private void OnAuthoritative(ushort processedTick)
    {
        _applyAuthoritativeState();
        Store(processedTick);
    }

    private void OnBaseline()
    {
        if (_manager.Tickrate != _world.TickRate)
            throw new InvalidOperationException("The authoritative tick rate does not match the configured physics world.");
        _history.Clear();
        _order.Clear();
        _historyBytes = 0;
        _applyAuthoritativeState();
        // A new baseline has no outstanding inputs; LES resets its input history.
        Store(_manager is ClientEntityManager client ? client.LastProcessedTick : (ushort)0);
    }

    private void Store(ushort tick)
    {
        byte[] snapshot = _world.CaptureSnapshot();
        if (snapshot.LongLength > _byteBudget)
            throw new InvalidOperationException("Physics snapshot exceeds the rollback history budget.");
        if (_history.Remove(tick, out var old)) _historyBytes -= old.LongLength;
        else _order.Enqueue(tick);
        while (_history.Count >= _capacity || _historyBytes + snapshot.LongLength > _byteBudget)
        {
            ushort evicted = _order.Dequeue();
            if (evicted == tick) { _order.Enqueue(evicted); continue; }
            if (_history.Remove(evicted, out var removed)) _historyBytes -= removed.LongLength;
        }
        _history[tick] = snapshot;
        _historyBytes += snapshot.LongLength;
    }

    public void Dispose()
    {
        if (_disposed) return;
        _manager.SimulationTickCompleted -= OnTick;
        if (_manager is ClientEntityManager client)
        {
            client.PhysicsRollbackStarted -= OnRollback;
            client.AuthoritativeStateApplied -= OnAuthoritative;
            client.BaselineApplied -= OnBaseline;
        }
        _history.Clear();
        _order.Clear();
        _historyBytes = 0;
        _disposed = true;
    }
}
