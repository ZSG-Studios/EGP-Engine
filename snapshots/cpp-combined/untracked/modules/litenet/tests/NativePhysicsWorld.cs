using System.Runtime.InteropServices;
using EGP.Networking;

sealed class NativePhysicsWorld : IFixedPhysicsWorld, IDisposable
{
    private IntPtr _handle = Create();
    public ulong NextTick => checked(Tick(_handle) + 1);
    public string SimulationFingerprint => "box3d-test-e77352c";
    public int TickRate => 60;
    public void Spawn(ulong entity, float height)
    {
        RequireOk(SpawnNative(_handle, entity, height));
        RequireOk(Flush(_handle));
    }
    public float Height(ulong entity) => HeightNative(_handle, entity);
    public byte[] CaptureSnapshot()
    {
        int size = Snapshot(_handle, null, 0);
        if (size < 1 || size > 64 * 1024 * 1024) throw new Exception("Native snapshot size is invalid.");
        var bytes = new byte[size];
        if (Snapshot(_handle, bytes, size) != size) throw new Exception("Native snapshot failed.");
        return bytes;
    }
    public void RestoreSnapshot(byte[] snapshot) => RequireOk(Restore(_handle, snapshot, snapshot.Length));
    public void StepTick(ulong expectedTick) => RequireOk(Step(_handle, expectedTick));
    public void Dispose() { if (_handle != IntPtr.Zero) Destroy(_handle); _handle = IntPtr.Zero; }
    private static void RequireOk(int error) { if (error != 0) throw new Exception("Native Box3D error " + error); }
    private const string Library = "egp_network_physics";
    [DllImport(Library, EntryPoint = "egp_test_create", CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr Create();
    [DllImport(Library, EntryPoint = "egp_test_destroy", CallingConvention = CallingConvention.Cdecl)] private static extern void Destroy(IntPtr world);
    [DllImport(Library, EntryPoint = "egp_test_tick", CallingConvention = CallingConvention.Cdecl)] private static extern ulong Tick(IntPtr world);
    [DllImport(Library, EntryPoint = "egp_test_step", CallingConvention = CallingConvention.Cdecl)] private static extern int Step(IntPtr world, ulong tick);
    [DllImport(Library, EntryPoint = "egp_test_spawn", CallingConvention = CallingConvention.Cdecl)] private static extern int SpawnNative(IntPtr world, ulong entity, float height);
    [DllImport(Library, EntryPoint = "egp_test_flush", CallingConvention = CallingConvention.Cdecl)] private static extern int Flush(IntPtr world);
    [DllImport(Library, EntryPoint = "egp_test_height", CallingConvention = CallingConvention.Cdecl)] private static extern float HeightNative(IntPtr world, ulong entity);
    [DllImport(Library, EntryPoint = "egp_test_snapshot", CallingConvention = CallingConvention.Cdecl)] private static extern int Snapshot(IntPtr world, byte[]? buffer, int capacity);
    [DllImport(Library, EntryPoint = "egp_test_restore", CallingConvention = CallingConvention.Cdecl)] private static extern int Restore(IntPtr world, byte[] buffer, int size);
}
