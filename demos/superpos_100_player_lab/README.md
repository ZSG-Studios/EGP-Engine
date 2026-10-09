# Remote Superpos courier arena

Run `python run_lab.py --duration 1800` from this folder, or run `launch.ps1` for five minutes. The launcher updates only this lab's source in the canonical remote checkout. It uses the existing SSH administrator key and WireGuard tunnel; no VPN configuration or credentials are changed. The remote native editor must already exist in its usual engine bin directory.

One headless native dedicated server runs on the build PC at 10.77.64.1. Four local native client processes each host 25 independent Superpos sessions and independent courier brains. A fifth local native process is your playable client, player 100, for 101 streams total. Local processes share the first four logical CPUs and cap rendering at 60 FPS.

WASD moves relative to the camera, Space jumps, F applies a server-validated physics shockwave to nearby obstacles, right-drag orbits and the wheel zooms. Visit the four coloured depots to deliver parcels. Your avatar is white. Bots avoid nearby players and obstacles using their own delayed network observations.

Keys 1–5 select your own network profile: fibre, broadband, busy WiFi, mobile or poor. O creates a six-second outage on your stream. These controls alter the packet proxy, independently of the bots. The supervisor waits for your requested outage to end before retrying a failed association.

| Bot IDs | Added one-way up/down delay | Jitter | Random loss | Wire rate per direction |
| --- | --- | --- | --- | --- |
| 0–19 fibre | 5 / 7 ms | 1 ms | 0.05% | 1 MB/s |
| 20–39 broadband | 14 / 21 ms | 4 ms | 0.3% | 300 kB/s |
| 40–59 busy WiFi | 25 / 35 ms | 18 ms | 2% | 100 kB/s |
| 60–79 mobile | 75 / 95 ms | 35 ms | 4% | 45 kB/s |
| 80–99 poor | 120 / 150 ms | 65 ms | 7% | 24 kB/s |

Upload rate is 60% of the table rate. Profiles are synthetic incremental impairments on top of the actual VPN/Internet path, not promises of a particular end-to-end RTT. Per-direction seeded jitter is correlated; WiFi/mobile/poor profiles also use two-state burst loss, duplication and expedited packets that cause reordering. Queue capacity and serialization are bounded. The proxy never decrypts gameplay traffic. These impairment categories follow the [NetEm model](https://www.man7.org/linux/man-pages/man8/tc-netem.8.html); this Windows proxy is not Linux NetEm and does not claim a calibrated ISP trace.

At 20–27 seconds, WiFi/mobile bandwidth drops to one quarter. At 30–39 seconds all poor-cohort traffic is dropped. At 55 seconds all bot paths become clean for a measured recovery phase; your chosen profile remains active. Terminal associations retry independently on coordinated fresh epochs with ten-second minimum backoff. Provisioning keys are random per stream and passed through private stdin, never saved to files or printed. Epoch controls and telemetry contain no keys.

The server owns a 60 Hz native EGPBox3DWorld, 101 player colliders and 40 movable obstacles. It validates bound identity, input sequencing, finite bounded movement, jumps and shockwave cooldowns; stale input stops movement. Each stream receives its own nearest-32 interest frame, published through a bounded native Superpos canonical object. Complete received frames are validated before application. One application ticket per direction prevents unbounded obsolete input/state queues. Explicit application receipts retire tickets and drive independent measured-ack pacing. Bots coalesce the latest intent; observation loss slows their decisions. The client extrapolates its avatar and smooths authoritative corrections.

This uses native UDP/DTLS sessions, canonical publication, receipts, fresh-epoch rejection and native physics. Client extrapolation is application code, not a registered Superpos native replay provider. The server uses pre-provisioned connected UDP associations, not public matchmaking, a multi-peer listener or a NAT traversal service. SSH carries bootstrap/supervision and diagnostic telemetry; gameplay crosses the actual UDP/VPN path. Bots never read global server diagnostic positions.

Fixed evidence: `EGP-Engine/.build/diagnostics/superpos-100`. `receipt.json` records the latest completed run; `qualification.json` preserves the compact 80-second qualification summary. All 101 streams passed, all 20 blackout streams recovered, and the proxy exercised 10,174 drops, 537 duplicates and 4,135 reorder events. Remote solver-step p95 was 1.505 ms; this excludes networking/application frame cost and does not establish a 101-player production capacity limit. Each bot travelled at least 305 metres and all five cohorts completed deliveries during that run. Physical human key presses were not automated.

The launcher and native processes have finite duration watchdogs. The remote firewall rule `EGP-Superpos-100-Lab` permits only VPN source 10.77.64.2 to UDP ports 48000–48100 at 10.77.64.1. Keep one WireGuard tunnel active. A short two-bot remote probe is available with `--bots 2 --no-human --duration 15`; full fault/recovery qualification needs at least 65 seconds.
