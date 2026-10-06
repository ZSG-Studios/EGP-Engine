# EGP network lab

Run separate local clients against a headless dedicated server or a visible listen
host. Each run copies the fixture and current shared helpers into its own evidence
directory, imports them, and retains individual logs and a combined `receipt.json`.
Encrypted admission tokens live only in a temporary trusted local handoff.

```powershell
python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --clients 3 --visible
python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --mode host --clients 3 --visible --preset wan --duration 20 --reconnect-at 8
```

For a packaged Windows game, pass the Mono template as `--engine` and its matching
Mono editor as `--editor`. The tool exports a fresh executable and PCK before
starting the owned processes. `--visible` shows clients and the listen host;
dedicated servers remain headless. The tool observes all requested windows together
without sending input or changing their focus.

```powershell
python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.template_release.x86_64.mono.exe --editor bin/godot.windows.editor.dev.x86_64.mono.exe --clients 3 --visible --preset wan --duration 20 --server-restart-at 6 --server-restart-mode abrupt
```

| Option | Behavior |
| --- | --- |
| `--mode dedicated` / `host` | Headless dedicated server or listen-host fixture; the latter supplies a server-owned test entity, without a local gameplay player |
| `--clients 1..64` | Separate client processes; headless unless `--visible` is selected |
| `--preset local` / `wifi` / `wan` / `poor` | Outgoing latency/jitter/loss: 0/0/0, 40/10/1, 100/25/3 or 200/60/10; milliseconds and loss percent |
| `--latency`, `--jitter`, `--loss` | Override preset values; finite bounded numbers required |
| `--simulate-on both` / `server` / `clients` | Impairment direction; latency on both endpoints increases round-trip time |
| `--reconnect-at SECONDS` | Close and readmit each client once with refreshed admission; requires three seconds afterward |
| `--client-stall-at SECONDS` | Delay one fully admitted client's poll beyond the fixed-clock budget, then verify explicit fresh admission/ownership recovery |
| `--client-stall-ms MILLISECONDS` | Gap from 550 to 5000 ms, default 750; does not change the engine catch-up budget |
| `--client-stall-index INDEX` | Client to stall, default 0; other clients keep their connections |
| `--client-stall-count 1..8` | Number of sequential gaps in that client, default 1; each requires verified recovery before the next |
| `--client-stall-interval SECONDS` | Seconds between scheduled gaps, default 8; repeated gaps must leave at least six seconds after each gap for recovery |
| `--server-restart-at SECONDS` | Replace the dedicated server while keeping the original clients alive |
| `--server-restart-mode graceful` / `abrupt` | Request a final checkpoint and clean exit, or forcibly terminate the owned server after verifying its latest health checkpoint |
| `--server-down-for SECONDS` | Outage before replacement, 0..10 seconds; default 1 |
| `--duration SECONDS` | Client lifetime, 5..100 seconds; processes have an additional bounded watchdog and cleanup |
| `--port PORT` | Loopback UDP endpoint; 0 chooses an available port; replacement retains the initial port |
| `--output DIRECTORY` | Evidence parent, default `.build/egp-network-lab`; each run uses a unique child |

Replacement requires dedicated mode and cannot be combined with `--reconnect-at`.
It requires at least three seconds before replacement and seven seconds after the
requested outage. High impairment can still fail those bounds; a failed run is
retained rather than counted as success. Use `--help` for all numeric limits.

To test recovery after a scheduling stall, use a dedicated server or listen host:

```powershell
python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --clients 3 --visible --preset wan --duration 20 --client-stall-at 4 --client-stall-ms 750 --client-stall-index 0
```

Stalls cannot overlap manual reconnect or server replacement. They require
three seconds before the first injection and seven seconds after the last gap. Injection waits
for the chosen client's authenticated reply and owner-input acknowledgment.
The child deliberately delays its next poll; the engine must return `FAILED`,
emit the catch-up diagnostic, stop its endpoint and clear entities/ticks. Recovery
runs after poll returns, closes/reconfigures the facade and requests a fresh token
from the lab backend. The server waits for the old peer to disconnect, verifies
revoked ownership, removes the abandoned entity and gives the new peer a new owned
entity. Old-entity input is deliberately sent and must not affect the server's
counter. New owner input must be acknowledged exactly once.

The receipt independently checks each gap's stopped state, cleared caches, reset ticks,
different peer/entity identities, both connections' input acknowledgments, the
same authoritative server PID and continued tick/state progress. Other clients
must retain their original connection and receive the advancing authoritative
state. All original process PIDs stay alive; no engine budget or timeout is raised.
This is an explicit application recovery example using a local trusted backend,
not automatic transport reconnect or production authentication. It covers one
selected client per run; server stalls and physics rollback need separate
qualification. Repeated recovery uses immutable admission files for each connection
generation; retired tokens are not reused. The next gap waits for the current
owner-input acknowledgment and advancing authoritative tick/counter state. If
recovery takes longer than the planned interval, injection waits and the run still
must finish within its original duration and watchdog.

```powershell
python misc/scripts/launch_egp_network_lab.py --engine bin/godot.windows.editor.dev.x86_64.mono.exe --clients 3 --visible --preset wan --duration 34 --client-stall-at 4 --client-stall-count 3 --client-stall-interval 8
```

The receipt includes a chronological `stall_proofs` list and per-generation input,
tick, server PID and peer history. Every recovery must introduce a new peer and
entity; successive records must form one continuous client history. Exact server
counter checks reject missing or duplicated owner commands, and all healthy
clients must observe the final counter while retaining their original connections.

The restart fixture checks actual server PID replacement on the same endpoint,
transport disconnect, cleared client entities, fresh encrypted admission and
reconnection. Each client validates a newly replicated owned entity and sends an
owner-authorized input in each server generation. The test server restores an
explicit application counter checkpoint, then applies the new generation's inputs
once. The receipt verifies replicated server PIDs, both generations' input
acknowledgments and tick progress, exact restoration, and advancement on the new
server. Immutable local handoff files avoid replacing files held open on Windows.

Receipts include fixture/helper/launcher hashes, source editor/template hashes,
exported runtime/PCK hashes, process commands/PIDs/exit codes, window observations
and semantic result evidence. Abrupt server termination is accepted only for the
requested original PID after a verified checkpoint; unexpected process failures,
errors and incomplete evidence fail the run. Owned processes and admission files
are cleaned up on completion, failure or interruption.

This is a bounded local fixture. The application checkpoint is explicit test
behavior, rather than automatic engine persistence. Packet loss is stochastic.
Production authentication, remote services, arbitrary game-state restoration,
authoritative physics rollback, scale, soak and performance require separate gates.
