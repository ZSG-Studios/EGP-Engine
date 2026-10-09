using System.Diagnostics;
using System.Text.Json;
using System.Net;
using System.Security.Cryptography;
using EGP.Networking;
using LiteEntitySystem;
using LiteNetLib;
using LiteNetLib.Utils;

var checks = new List<string>();
void Check(bool condition, string name)
{
    if (!condition) throw new Exception("FAILED: " + name);
    checks.Add(name);
}
void Until(Func<bool> done, int timeoutMs, params LiteSession[] sessions)
{
    var timer = Stopwatch.StartNew();
    while (!done() && timer.ElapsedMilliseconds < timeoutMs)
    {
        foreach (var session in sessions) session.Pump();
        Thread.Sleep(2);
    }
    if (!done()) throw new Exception("Timed out: " + string.Join(",", sessions.Select(s => s.State)));
}

Logger.LoggerImpl = new TestLogger();
RpcDecoderChecks.Run(Check);
using (var schemaA = new LiteSession(new EntityTypesMap<TypeId>().Register(TypeId.Marker, p => new TestMarker(p))))
using (var schemaB = new LiteSession(new EntityTypesMap<TypeId>().Register(TypeId.Marker, p => new AlternateMarker(p))))
    Check(schemaA.CompatibilityFingerprint != schemaB.CompatibilityFingerprint, "same-sized fields on different entity schemas have distinct fingerprints");
Check(new TickSequence().Observe(65535) == 65535, "tick sequence initial");
var sequence = new TickSequence();
sequence.Observe(65534);
Check(sequence.Observe(65535) == 65535 && sequence.Observe(0) == 65536 && sequence.Observe(1) == 65537,
    "ushort wrap expands forward");
Check(sequence.Observe(65535) == 65535, "rollback crosses wrap backward");
try { new LiteSession(Types.Build(), new SessionOptions { MaxPlayers = 256 }); throw new Exception("Accepted invalid limit"); }
catch (ArgumentException) { checks.Add("player limit validates"); }
using (var invalidBind = new LiteSession(Types.Build()))
{
    try { invalidBind.Listen(0, "invalid-address"); throw new Exception("Invalid bind accepted"); }
    catch (FormatException) { }
    Check(invalidBind.Server == null && invalidBind.State == SessionState.Stopped && invalidBind.LocalPort == 0,
        "invalid bind does not leave a partially started session");
}

using var server = new LiteSession(Types.Build(), new SessionOptions { MaxPlayers = 2 });
server.PlayerJoined += (manager, player) =>
{
    var pawn = manager.AddEntity<TestPawn>();
    manager.AddController<TestController>(player, pawn);
};
server.Listen(0, "127.0.0.1");
try
{
    using var invalidTimeline = new PhysicsTimeline(server.Server!, new TestPhysicsWorld { TickRate = 30 }, () => { }, () => { });
    throw new Exception("Accepted mismatched physics tick rate");
}
catch (ArgumentException) { checks.Add("physics and replication tick rate mismatch rejected"); }
var marker = server.Server!.AddEntity<TestMarker>();
marker.Value.Value = 42;
int serverSteps = 0;
server.Server.SimulationTickCompleted += (_, _) => serverSteps++;
using var first = new LiteSession(Types.Build());
using var second = new LiteSession(Types.Build());
int predictionSteps = 0, replaySteps = 0;
var physics = new TestPhysicsWorld();
PhysicsTimeline? physicsTimeline = null;
first.ClientCreated += manager => manager.SimulationTickCompleted += (_, mode) =>
{ if (mode == UpdateMode.PredictionRollback) replaySteps++; else predictionSteps++; };
first.ClientCreated += manager => physicsTimeline = new PhysicsTimeline(manager, physics, () => { }, () => { });
first.Connect("127.0.0.1", server.LocalPort);
second.Connect("127.0.0.1", server.LocalPort);
Until(() => first.State == SessionState.Connected && second.State == SessionState.Connected &&
    first.Client!.GetEntities<TestMarker>().Count == 1 && second.Client!.GetEntities<TestMarker>().Count == 1,
    6000, server, first, second);
Check(server.Server.PlayersCount == 2, "two clients admitted");
Check(first.Client!.GetEntities<TestMarker>().First().Value.Value == 42, "baseline replicates SyncVar");
marker.Value.Value = 99;
Until(() => first.Client!.GetEntities<TestMarker>().First().Value.Value == 99 &&
    second.Client!.GetEntities<TestMarker>().First().Value.Value == 99, 4000, server, first, second);
Check(true, "delta replicates to both clients");
Until(() => server.Server.GetEntities<TestPawn>().All(p => p.Position.Value > 0.5f), 5000, server, first, second);
Check(predictionSteps > 0 && replaySteps > 0 && serverSteps > 0, "prediction and reconciliation tick hooks execute");
Check(physics.Steps == predictionSteps + replaySteps && physics.Restores > 0 &&
    physicsTimeline!.StoredSnapshots <= 128, "physics steps once per prediction or replay tick with bounded solver history");
Check(server.Server.GetEntities<TestPawn>().All(p => p.Position.Value < 100), "server movement remains input bounded");

var firstMarker = first.Client!.GetEntities<TestMarker>().First();
var secondMarker = second.Client!.GetEntities<TestMarker>().First();
server.Server.ToggleSyncGroup(first.Client.LocalPlayer.Id, marker, SyncGroup.SyncGroup1, false);
Until(() => !firstMarker.IsSyncGroupEnabled(SyncGroup.SyncGroup1), 3000, server, first, second);
marker.Value.Value = 77;
marker.Notify(71);
Until(() => secondMarker.Value.Value == 77 && secondMarker.LastNotice == 71, 3000, server, first, second);
Check(firstMarker.Value.Value == 99 && firstMarker.LastNotice == 0,
    "per-player sync group filters fields and typed RPCs");
server.Server.ToggleSyncGroup(first.Client.LocalPlayer.Id, marker, SyncGroup.SyncGroup1, true);
Until(() => firstMarker.Value.Value == 77, 3000, server, first, second);
Check(firstMarker.IsSyncGroupEnabled(SyncGroup.SyncGroup1), "interest reentry receives current authoritative fields");
marker.Notify(72);
Until(() => firstMarker.LastNotice == 72 && secondMarker.LastNotice == 72, 3000, server, first, second);
Check(true, "typed entity RPC delivers to both enabled clients");
var remotePawn = server.Server.GetEntities<TestPawn>().First(p => p.OwnerId != first.Client.LocalPlayer.Id);
float currentPosition = remotePawn.Position.Value;
server.Server.EnableLagCompensation(server.Server.GetPlayer(first.Client.LocalPlayer.Id));
Check(server.Server.IsLagCompensationEnabled && remotePawn.Position.Value < currentPosition,
    "lag compensation rewinds non-owned synchronized history");
server.Server.DisableLagCompensation();
Check(!server.Server.IsLagCompensationEnabled &&
    BitConverter.SingleToInt32Bits(remotePawn.Position.Value) == BitConverter.SingleToInt32Bits(currentPosition),
    "lag compensation restores live authoritative position exactly");

bool applicationDelivered = false;
server.ApplicationReceived += (peer, payload) => { applicationDelivered = payload.SequenceEqual(new byte[] { 4, 5, 6 }); server.SendApplication(peer, payload); };
bool echo = false;
first.ApplicationReceived += (_, payload) => echo = payload.SequenceEqual(new byte[] { 4, 5, 6 });
first.SendApplication(0, new byte[] { 4, 5, 6 });
Until(() => applicationDelivered && echo, 3000, server, first, second);
Check(true, "reliable application channel round trip");

using var mismatch = new LiteSession(Types.Build(), new SessionOptions { GameProtocol = "wrong" });
mismatch.Connect("127.0.0.1", server.LocalPort);
Until(() => mismatch.State == SessionState.Disconnected, 4000, server, first, second, mismatch);
Check(server.Server.PlayersCount == 2, "incompatible or excess clients rejected");

second.Stop();
Until(() => server.Server.PlayersCount == 1, 3000, server, first);
Check(server.Server.GetEntities<TestPawn>().Count == 1, "disconnect destroys controller and pawn");
mismatch.Stop();
mismatch.Connect("127.0.0.1", server.LocalPort);
Until(() => mismatch.State == SessionState.Disconnected, 4000, server, first, mismatch);
Check(server.Server.PlayersCount == 1, "protocol mismatch rejected with spare server capacity");
second.Connect("127.0.0.1", server.LocalPort);
Until(() => second.State == SessionState.Connected && second.Client!.GetEntities<TestMarker>().Count == 1,
    5000, server, first, second);
Check(second.Client!.GetEntities<TestMarker>().First().Value.Value == 77, "reconnect gets fresh baseline");
marker.Destroy();
Until(() => first.Client!.GetEntities<TestMarker>().Count == 0 && second.Client!.GetEntities<TestMarker>().Count == 0,
    4000, server, first, second);
Check(true, "despawn replicates");

bool rejectedThread = false;
var worker = new Thread(() => { try { first.Pump(); } catch (InvalidOperationException) { rejectedThread = true; } });
worker.Start(); worker.Join();
Check(rejectedThread, "game API rejects wrong thread");

using var impairedServer = new LiteSession(Types.ReplicationOnly(), new SessionOptions
{ SimulatedLossPercent = 15, SimulatedLatencyMinMs = 20, SimulatedLatencyMaxMs = 60 });
impairedServer.Listen(0, "127.0.0.1");
var impairedMarker = impairedServer.Server!.AddEntity<TestMarker>();
impairedMarker.Value.Value = 7;
using var impairedClient = new LiteSession(Types.ReplicationOnly(), new SessionOptions
{ SimulatedLossPercent = 15, SimulatedLatencyMinMs = 20, SimulatedLatencyMaxMs = 60 });
impairedClient.Connect("127.0.0.1", impairedServer.LocalPort);
Until(() => impairedClient.State == SessionState.Connected && impairedClient.Client!.GetEntities<TestMarker>().Count == 1,
    10000, impairedServer, impairedClient);
impairedMarker.Value.Value = 1234;
Until(() => impairedClient.Client!.GetEntities<TestMarker>().First().Value.Value == 1234,
    10000, impairedServer, impairedClient);
Check(true, "baseline and deltas converge with 15 percent loss and 20-60 ms latency per endpoint");
Check(impairedClient.Client!.GetControllers<HumanControllerLogic>().Count == 0,
    "controller-free replication schema runs without input controllers");

// Feed malformed transport traffic only after a valid compatibility handshake.
using var malformedServer = new LiteSession(Types.Build());
malformedServer.Listen(0, "127.0.0.1");
var rawEvents = new EventBasedNetListener();
var raw = new NetManager(rawEvents) { AutoRecycle = true };
raw.Start();
var handshake = new byte[38];
"EGP1"u8.CopyTo(handshake);
Convert.FromHexString(malformedServer.CompatibilityFingerprint).CopyTo(handshake, 4);
var writer = new NetDataWriter(); writer.Put(handshake);
raw.Connect("127.0.0.1", malformedServer.LocalPort, writer);
var deadline = Stopwatch.StartNew();
while (raw.FirstPeer?.ConnectionState != ConnectionState.Connected && deadline.ElapsedMilliseconds < 3000)
{ malformedServer.Pump(); raw.PollEvents(); Thread.Sleep(2); }
raw.FirstPeer!.Send(new byte[] { LiteSession.EntityPacketHeader }, DeliveryMethod.Unreliable);
Until(() => malformedServer.Statistics.RejectedPackets > 0, 3000, malformedServer);
raw.Stop();
Check(true, "truncated packet rejected without taking server down");
var key = RandomNumberGenerator.GetBytes(32);
using (var layer = new AuthenticatedPacketLayer(key))
{
    var endpoint = new IPEndPoint(IPAddress.Loopback, 1);
    byte[] message = new byte[] { 1, 2, 3 };
    int offset = 0, length = message.Length;
    layer.ProcessOutBoundPacket(ref endpoint, ref message, ref offset, ref length);
    var corrupted = (byte[])message.Clone(); corrupted[^1] ^= 1;
    int corruptedLength = length;
    layer.ProcessInboundPacket(ref endpoint, ref corrupted, ref corruptedLength);
    Check(corruptedLength == 0, "authenticated transport rejects modified ciphertext");
    layer.ProcessInboundPacket(ref endpoint, ref message, ref length);
    Check(length == 3 && message.AsSpan(0, length).SequenceEqual(new byte[] { 1, 2, 3 }),
        "authenticated transport decrypts original datagram");
}
using (var encryptedServer = new LiteSession(Types.Build(), new SessionOptions { TransportKey = key, AccessToken = "fixture-token" }))
using (var encryptedClient = new LiteSession(Types.Build(), new SessionOptions { TransportKey = key, AccessToken = "fixture-token" }))
{
    encryptedServer.Listen(0, "127.0.0.1");
    encryptedServer.Server!.AddEntity<TestMarker>().Value.Value = 5;
    encryptedClient.Connect("127.0.0.1", encryptedServer.LocalPort);
    Until(() => encryptedClient.State == SessionState.Connected && encryptedClient.Client!.GetEntities<TestMarker>().Count == 1,
        5000, encryptedServer, encryptedClient);
    Check(encryptedClient.Client!.GetEntities<TestMarker>().First().Value.Value == 5,
        "encrypted handshake and replication baseline round trip");
}
if (args.Contains("--native-physics"))
{
    using var nativeWorld = new NativePhysicsWorld();
    nativeWorld.Spawn(1, 10);
    var nativeOptions = new SessionOptions { SimulationFingerprint = nativeWorld.SimulationFingerprint };
    using var physicsServer = new LiteSession(Types.Build(), nativeOptions);
    using var physicsClient = new LiteSession(Types.Build(), nativeOptions);
    physicsServer.Listen(0, "127.0.0.1");
    var physicsEntity = physicsServer.Server!.AddEntity<TestPawn>();
    using var nativeTimeline = new PhysicsTimeline(physicsServer.Server, nativeWorld, () => { },
        () => physicsEntity.Position.Value = nativeWorld.Height(1));
    physicsClient.Connect("127.0.0.1", physicsServer.LocalPort);
    Until(() => nativeWorld.NextTick >= 60 && physicsClient.Client?.GetEntities<TestPawn>().Count == 1,
        5000, physicsServer, physicsClient);
    float authoritativeHeight = nativeWorld.Height(1);
    Check(authoritativeHeight < 6 && authoritativeHeight > 0, "real Box3D runs inside LES fixed ticks");
    Check(Math.Abs(physicsClient.Client!.GetEntities<TestPawn>().First().Position.Value - authoritativeHeight) < 2,
        "real Box3D authoritative position replicates over LiteNetLib");
    byte[] solverSnapshot = nativeWorld.CaptureSnapshot();
    ulong solverTick = nativeWorld.NextTick;
    for (int i = 0; i < 30; i++) nativeWorld.StepTick(nativeWorld.NextTick);
    float futureHeight = nativeWorld.Height(1);
    nativeWorld.RestoreSnapshot(solverSnapshot);
    Check(nativeWorld.NextTick == solverTick, "managed bridge restores full Box3D solver tick");
    for (int i = 0; i < 30; i++) nativeWorld.StepTick(nativeWorld.NextTick);
    Check(BitConverter.SingleToInt32Bits(nativeWorld.Height(1)) == BitConverter.SingleToInt32Bits(futureHeight),
        "managed Box3D snapshot restore reproduces continuation bits");
}
physicsTimeline?.Dispose();
var oldServer = server.Server!;
first.Stop(); second.Stop(); server.Stop();
Check(oldServer.EntitiesCount == 0 && oldServer.GetEntities<TestPawn>().Count == 0,
    "shutdown clears retained destroyed entity slots");

var report = new { passed = true, checks, serverSteps, predictionSteps, replaySteps,
    libraries = new { LiteNetLib = "ddacf9b7a3e821cc052e90c30daf82f0702a6db4", LiteEntitySystem = "d7448a8ac1353259a6feb07c9612d4bfc733f329" } };
Console.WriteLine(JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }));

enum TypeId : ushort { Marker, Pawn, Controller }
static class Types
{
    public static EntityTypesMap ReplicationOnly() => new EntityTypesMap<TypeId>()
        .Register(TypeId.Marker, p => new TestMarker(p));
    public static EntityTypesMap Build() => new EntityTypesMap<TypeId>()
        .Register(TypeId.Marker, p => new TestMarker(p))
        .Register(TypeId.Pawn, p => new TestPawn(p))
        .Register(TypeId.Controller, p => new TestController(p));
}
[EntityFlags(EntityFlags.Updateable)]
class TestMarker(EntityParams p) : EntityLogic(p)
{
    [SyncVarFlags(SyncFlags.SyncGroup1)] public SyncVar<int> Value;
    private static RemoteCall<int> Notice;
    public int LastNotice;
    public void Notify(int value) => ExecuteRPC(Notice, value);
    protected override void RegisterRPC(ref RPCRegistrator r)
    {
        base.RegisterRPC(ref r);
        r.CreateRPCAction<TestMarker, int>((entity, value) => entity.LastNotice = value, ref Notice,
            ExecuteFlags.SendToAll | ExecuteFlags.SyncGroup1);
    }
}
class AlternateMarker : EntityLogic
{
    public SyncVar<int> OtherValue;
    public AlternateMarker(EntityParams p) : base(p) { OtherValue.Value = 0; }
}
struct TestInput { public sbyte Move; }
[EntityFlags(EntityFlags.Updateable)]
class TestPawn(EntityParams p) : PawnLogic(p)
{
    [SyncVarFlags(SyncFlags.Interpolated | SyncFlags.LagCompensated)] public SyncVar<float> Position;
    public sbyte Move;
    protected override void Update()
    { base.Update(); Position.Value += Math.Clamp((int)Move, -1, 1) * 4 * EntityManager.DeltaTimeF; }
}
class TestController(EntityParams p) : HumanControllerLogic<TestInput, TestPawn>(p)
{
    protected override void VisualUpdate() { ModifyPendingInput().Move = 1; }
    protected override void BeforeControlledUpdate() { if (ControlledEntity != null) ControlledEntity.Move = CurrentInput.Move; }
}
class TestLogger : ILogger
{
    public void Log(string message) { }
    public void LogWarning(string message) => Console.Error.WriteLine(message);
    public void LogError(string message) => Console.Error.WriteLine(message);
}
class TestPhysicsWorld : IFixedPhysicsWorld
{
    public ulong NextTick { get; private set; }
    public string SimulationFingerprint => "test-solver-v1";
    public int TickRate { get; init; } = 60;
    public int Steps, Restores;
    public byte[] CaptureSnapshot() => BitConverter.GetBytes(NextTick);
    public void RestoreSnapshot(byte[] data) { NextTick = BitConverter.ToUInt64(data); Restores++; }
    public void StepTick(ulong expectedTick)
    {
        if (expectedTick != NextTick) throw new Exception("Wrong physics tick");
        NextTick++; Steps++;
    }
}
