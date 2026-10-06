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
