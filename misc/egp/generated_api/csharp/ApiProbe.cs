using Godot;
using Godot.Collections;
// Compile-only direct Superpos API coverage against matching generated assemblies.
public static class ApiProbe
{
    public static void Verify()
    {
        using var field = new SuperposField { FieldId = 1 };
        using var schema = new SuperposSchema { SchemaId = 73, Fields = new Array<SuperposField> { field } };
        using var session = new SuperposSession();
        Error configured = session.Configure(new Array<SuperposSchema> { schema }, 4, 1, 0, 4096);
        ulong handle = session.SpawnObject(73, 0, SuperposUInt64.ToBytes(1));
        Dictionary observed = session.ReadObject(handle);
        Dictionary tick = session.ReadTick();
        session.Close();
        _ = (configured, observed, tick);
    }
}
