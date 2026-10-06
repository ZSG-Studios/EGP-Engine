# Native Box3D scene adapter

Adapted from [godot-box3d](https://github.com/bearlikelion/godot-box3d),
revision `dd7964f7091d8c74fb21fdd47d6a536eccbcc20f`, MIT, Mark Arneman 2026.
`UPSTREAM.json` records the original source hashes. These files are modified;
the Box3D solver in `thirdparty/box3d` remains unchanged.

This port uses EGP's native PhysicsServer3D API and RID registries, without
GDExtension or a second copy of godot-cpp/Box3D. Native query and motion wrappers
carry exclusion sets through the query callbacks. Per-shape query and contact
indices map through public shape handles. All engine entries share the explicit
world's process guard. Bodies, areas and spaces are iterated in RID order rather
than address-hash order. The solver uses four substeps and the immutable engine
tick rate, one worker by default, and the same precise float compiler profile.

The native adapter is mandatory and default whenever EGP 3D physics is enabled.
This adapter is experimental. Its scene RIDs and eager scene creation order are
not authoritative network entity IDs. It does not yet expose the explicit
world's full solver rollback contract for ordinary Godot scene nodes.

Remaining parity work includes separation rays, ConeTwist/6DOF constraints,
soft bodies, penetration-depth queries, back-face semantics, concave
sensor visitors, full geometry/scaling fixtures, deterministic network lifecycle
ordering and scene rollback. The user authorized removal of the overlapping solvers; those sources are removed.
This cutover does not qualify full scene/network determinism or feature parity.
