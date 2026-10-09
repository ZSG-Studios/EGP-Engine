#!/usr/bin/env python3
"""Loop exported Superpos UDP probe pairs and fail on any crash or bad exit.

Regression run for the release-template startup race: an exported probe's
dispatch loop (export_loop.gd) defers the fixture's _initialize, so its first
_process may come first. Before udp.gd tolerated that, release templates
called SuperposSession.get_state() on a null instance (release GDScript does
not null-check typed method calls) and crashed in about half of the pairs.

    python release_pairs.py --engine <exported probe> --pairs 20 --output <dir>

The executable must carry the embedded probe project (run/main_loop_type =
SuperposExportLoop with udp.gd and runtime.gd). Both processes of every pair
must exit 0, print SUPERPOS_EGP_UDP_OK and print no ERROR line.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import socket
import subprocess
import sys
import time

# Windows access violation / Unix signals (negative returncode or 128+N).
CRASH_CODES = {0xC0000005, 0xC0000374, 0xC0000409}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def free_pair(start):
    for port in range(start, 60000, 13):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as a, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as b:
            try:
                a.bind(("127.0.0.1", port))
                b.bind(("127.0.0.1", port + 1))
                return port
            except OSError:
                continue
    raise RuntimeError("No available loopback UDP port pair")


def crashed(code):
    return code < 0 or (os.name != "nt" and code >= 128) or (code & 0xFFFFFFFF) in CRASH_CODES


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--pairs", type=int, default=20)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--first-port", type=int, default=41000)
    args = parser.parse_args()
    if args.pairs < 1 or args.pairs > 500:
        parser.error("--pairs must be 1 through 500")
    engine, output = args.engine.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    flags = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    receipt = {"engine": str(engine), "engine_sha256": sha(engine), "machine": platform.node(), "system": platform.system(),
               "fixture_runner_sha256": sha(__file__), "pairs": [], "passed": False,
               "scope": "exported probe, embedded project, loopback IPv4 accepted scenario only"}
    port = args.first_port
    for index in range(args.pairs):
        port = free_pair(port + 13)
        # Vary the client start offset so the first frames interleave differently.
        delay = (0.0, 0.15, 0.3)[index % 3]
        pair = {"index": index, "port": port, "client_delay_s": delay}
        processes, streams = [], []
        started = time.monotonic()
        try:
            for role in ("server", "client"):
                log = output / f"pair{index:03d}-{role}.log"
                stream = log.open("w", encoding="utf-8")
                streams.append(stream)
                processes.append(subprocess.Popen([str(engine), "--headless", "--max-fps", "60", "--", role, str(port), "ipv4", "accepted"],
                                                  cwd=output, stdout=stream, stderr=subprocess.STDOUT, **flags))
                if role == "server":
                    time.sleep(delay)
            deadline = time.monotonic() + 45
            for process in processes:
                process.wait(timeout=max(0.01, deadline - time.monotonic()))
        except subprocess.TimeoutExpired:
            pair["timed_out"] = True
        finally:
            for process in processes:
                if process.poll() is None:
                    process.kill()
                    process.wait()
            for stream in streams:
                stream.close()
        pair["elapsed_s"] = round(time.monotonic() - started, 3)
        ok = not pair.get("timed_out")
        for role, process in zip(("server", "client"), processes):
            text = (output / f"pair{index:03d}-{role}.log").read_text(errors="replace")
            code = process.returncode
            pair[role] = {"exit": code, "crashed": crashed(code), "marker": "SUPERPOS_EGP_UDP_OK" in text,
                          "error_lines": sum(1 for line in text.splitlines() if "ERROR:" in line)}
            ok = ok and code == 0 and pair[role]["marker"] and not pair[role]["error_lines"]
        pair["passed"] = bool(ok)
        receipt["pairs"].append(pair)
        print(json.dumps({"pair": index, "passed": pair["passed"], "server": pair["server"]["exit"], "client": pair["client"]["exit"]}), flush=True)
    receipt["crashes"] = sum(int(p[r]["crashed"]) for p in receipt["pairs"] for r in ("server", "client") if r in p)
    receipt["failed_pairs"] = sum(1 for p in receipt["pairs"] if not p["passed"])
    receipt["passed"] = receipt["failed_pairs"] == 0
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": receipt["passed"], "pairs": args.pairs, "failed_pairs": receipt["failed_pairs"],
                      "crashes": receipt["crashes"], "receipt": str(output / "receipt.json")}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
