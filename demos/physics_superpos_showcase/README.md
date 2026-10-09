# EGP Physics / Superpos showcase

Run launch.ps1 with the current EGP Mono editor. Drag with the right mouse button to orbit; use the wheel to zoom. All four buttons send application commands through the second native Superpos UDP association.

The left arena is a native EGPBox3DWorld stepped at 60 Hz. Poses are sent at 10 Hz with at most two frames in flight to leave capacity for bidirectional control and receipts. The right arena is a separate viewport showing poses received and explicitly applied through a Superpos canonical arena object, with visual interpolation. Admission uses an ephemeral random key held only in memory, two numeric loopback endpoints, a bounded canonical arena schema with an explicitly validated float pose layout and bounded packets. No key is logged or saved.

The arena has 52 dynamic bodies, a reusable cannonball, shockwaves and reset. Rewind restores a trusted local native Box3D snapshot; no solver snapshot is sent over the network. Native schema publication validates each complete pose batch before application projection. Sender tickets are retired after explicit application acknowledgements, with bounded in-flight queues.

This demonstrates real authenticated UDP in one process with two independent native associations, server-authoritative application physics and explicit scene projection. It does not claim separate-process/WAN qualification, native Superpos Box3D prediction integration, automatic RPC, scene replication or portable solver recovery.

Run verify.ps1 for headless and Vulkan checks of gravity, canonical receive integrity, acknowledged poses, all four network controls and the exact local rewind hash. The --smoke user argument runs a bounded 8-second gameplay/network check. --capture=ABSOLUTE_PATH additionally captures the rendered arena. Evidence is kept in the engine's fixed .build/diagnostics/physics-superpos-showcase directory.

The default UDP carrier exhibited control starvation under continuous 20 Hz fragmented pose traffic during qualification. This showcase uses the verified 10 Hz workload; it does not certify higher-rate full-duplex traffic. The launch and verification scripts limit the process to four local logical CPUs and 60 FPS. The process-specific ReShade disable environment variable from the installed layer manifest is supplied without changing system configuration.
