using Godot;
using Godot.Collections;

// Compile-only direct generated API coverage, not a network runtime qualification.
public static class ApiProbe
{
    public static void VerifyGeneratedSignatures(Node3D target, EgpNetSession session)
    {
        using var property = new SuperpositionProperty { Enabled = true, Property = "position", ValueType = (int)Variant.Type.Vector3, Quantization = 0.01, Smoothing = true };
        using var config = new SuperpositionConfig { Properties = new Array<SuperpositionProperty> { property }, UpdateRate = 20.0, InterestRadius = 64.0, InterestHysteresis = 2.0, Priority = 2, CaptureMode = 1 };
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
        double clock = interpolation.Advance(1.0 / 60.0);
        Transform3D pose = interpolation.Sample(1);
        Dictionary presentation = interpolation.GetStatistics();
        interpolation.Remove(1);
        interpolation.Clear();
        replication.QueueFree();
        _ = (bound, applied, sent, observer, stats, configured, accepted, clock, pose, presentation);
    }
}
