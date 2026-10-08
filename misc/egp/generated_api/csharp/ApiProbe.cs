using Godot;
using Godot.Collections;

// Compile-only direct generated API coverage, not a network runtime qualification.
public static class ApiProbe
{
    public static void VerifyPrediction(Callable capture, Callable restore, Callable simulate, Callable stateHash)
    {
        using var prediction = new SuperpositionPrediction();
        Error configured = prediction.Configure(capture, restore, simulate, stateHash, 0, 128, 1048576, 33554432, 1);
        Error predicted = prediction.Predict(1, new byte[] { 0, 0, 0, 0, 0, 0 });
        Error reconciled = prediction.Accept(new Array(), 1);
        Error reset = prediction.Reset(2);
        long tick = prediction.GetTick();
        long acknowledged = prediction.GetAcknowledgedTick();
        string hash = prediction.GetAcknowledgedHash();
        long pending = prediction.GetPendingTicks();
        long bytes = prediction.GetHistoryBytes();
        Dictionary statistics = prediction.GetStatistics();
        _ = (configured, predicted, reconciled, reset, tick, acknowledged, hash, pending, bytes, statistics);
    }

    public static void VerifyGeneratedSignatures(Node3D target, EgpNetSession session)
    {
        using var property = new SuperpositionProperty { Enabled = true, Property = "position", ValueType = (int)Variant.Type.Vector3, Quantization = 0.01, Smoothing = true };
        using var config = new SuperpositionConfig { Properties = new Array<SuperpositionProperty> { property }, UpdateRate = 20.0, InterestRadius = 64.0, InterestHysteresis = 2.0, Priority = 2, CaptureMode = 1, DeltaReplication = true };
        var replication = new Superposition { Enabled = true, TargetPath = "..", SessionPath = "", Config = config, ReplicationKey = "probe", EntityKind = 32001 };
        target.AddChild(replication);
        replication.SetSession(session);
        replication.MarkDirty();
        EgpNetSession bound = replication.GetSession();
        byte[] snapshot = replication.CaptureState();
        Error applied = replication.ApplyState(snapshot);
        Error sent = replication.ReplicateNow();
        Error observer = replication.SetObserverPosition(1, Vector3.Zero);
        replication.ClearObserver(1);
        Dictionary stats = replication.GetStatistics();
        using var interpolation = new EgpNetSnapshotInterpolator();
        Error configured = interpolation.Configure(60.0, 0.2, 0.1);
        bool accepted = interpolation.Submit(1, 1, Transform3D.Identity, Vector3.Zero);
        bool resetAccepted = interpolation.Submit(1, 2, Transform3D.Identity, Vector3.Zero, 1, Vector3.Up);
        double clock = interpolation.Advance(1.0 / 60.0);
        Transform3D pose = interpolation.Sample(1);
        Dictionary presentation = interpolation.GetStatistics();
        interpolation.Remove(1);
        interpolation.Clear();
        var world = new SuperpositionWorld { AutoPoll = true, AutoStart = false, Role = 0, Port = 10515, MaxPlayers = 64, MaxEntities = 4096 };
        Error worldConfigured = world.Configure();
        EgpNetSession worldSession = world.GetSession();
        using var prefab = new SuperpositionScene { PrefabId = 1, Scene = new PackedScene() };
        var spawner = new SuperpositionSpawner { Scenes = new Array<SuperpositionScene> { prefab }, SpawnPath = "..", ReplicationKey = "world" };
        spawner.SetSession(session);
        long entity = spawner.Spawn(1);
        Node spawned = spawner.GetSpawnedNode(entity);
        long owner = spawner.GetOwnerClientId(entity);
        Error visibility = spawner.SetVisible(entity, 1, true);
        using var rule = new SuperpositionRpcMethod { MethodId = 1, Method = "activate", Permission = (int)SuperpositionRpcMethod.PermissionEnum.Owner, ArgumentTypes = new int[] { (int)Variant.Type.Bool }, CallsPerSecond = 16 };
        var rpc = new SuperpositionRpc { SpawnerPath = "../Spawner", Methods = new Array<SuperpositionRpcMethod> { rule } };
        Error called = rpc.SendRpc(entity, 1, new Array { true });
        string rpcSchema = rpc.GetMethodFingerprint();
        Error despawned = spawner.Despawn(entity);
        world.Stop();
        world.Free();
        spawner.Free();
        rpc.Free();
        replication.QueueFree();
        _ = (bound, applied, sent, observer, stats, configured, accepted, clock, pose, presentation, resetAccepted, worldConfigured, worldSession, spawned, owner, visibility, called, rpcSchema, despawned);
    }
}
