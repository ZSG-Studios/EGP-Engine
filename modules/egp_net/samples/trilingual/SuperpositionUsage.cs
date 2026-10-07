// SPDX-License-Identifier: MIT
using Godot;

/// Optional native C# construction. The Inspector workflow needs none of this.
public static class SuperpositionUsage
{
	public static Superposition Attach(Node3D target, EGPNetSession session, string stableKey)
	{
		var visible = new SuperpositionProperty
		{
			Property = "visible",
			ValueType = (int)Variant.Type.Bool,
			Quantization = 0
		};
		var configuration = new SuperpositionConfig
		{
			Properties = new Godot.Collections.Array<SuperpositionProperty> { visible },
			UpdateRate = 10,
			Priority = 8,
			CaptureMode = 1 // Pushed; Inspector also offers Automatic.
		};
		var replication = new Superposition { Config = configuration, ReplicationKey = stableKey };
		target.AddChild(replication);
		replication.SetSession(session); // Authentication and session polling belong to the game.
		return replication;
	}

	public static void SetVisible(Node3D target, Superposition replication, bool visible)
	{
		target.Visible = visible;
		replication.MarkDirty(); // Coalesces until the next configured capture interval.
	}
}
