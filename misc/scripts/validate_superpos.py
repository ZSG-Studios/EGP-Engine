#!/usr/bin/env python3
"""Validate the actual Superpos-only engine API, canonical state and loopback UDP."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import socket
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
LAB = ROOT / "misc/egp/superpos_lab"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pair(family):
    kind = socket.AF_INET6 if family == "ipv6" else socket.AF_INET
    address = "::1" if family == "ipv6" else "127.0.0.1"
    for port in range(32000, 55000, 13):
        with socket.socket(kind, socket.SOCK_DGRAM) as a, socket.socket(kind, socket.SOCK_DGRAM) as b:
            try:
                a.bind((address, port))
                b.bind((address, port + 1))
                return port
            except OSError:
                pass
    raise RuntimeError("No available loopback UDP port pair")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--pack", type=Path, help="Test an exported probe pack with an export template; editor API dump is verified separately")
    parser.add_argument("--embedded-project", action="store_true", help="Test a normal exported executable with its embedded project, preserving shipping path-override restrictions")
    parser.add_argument("--output", type=Path, default=ROOT / ".build/diagnostics/superpos")
    args = parser.parse_args()
    if args.pack and args.embedded_project:
        parser.error("Choose --pack or --embedded-project")
    engine, output = args.engine.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    pack = args.pack.resolve() if args.pack else None
    packaged = pack is not None or args.embedded_project
    project_args = ["--main-pack", str(pack)] if pack else ([] if args.embedded_project else ["--path", str(LAB)])
    def script_args(name):
        if args.embedded_project:
            return []
        return ["--script", "res://" + name] if packaged else ["--script", str(LAB / name)]
    flags = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
    receipt = {"engine": str(engine), "engine_sha256": sha(engine), "passed": False,
               "scope": "current_full_native_engine_API_canonical_state_owner_lifecycle_loopback_UDP_only",
               "fixtures": {p.name: sha(p) for p in LAB.glob("*.gd")}, "checks": []}

    if pack:
        receipt["pack_sha256"] = sha(pack)
    if packaged:
        receipt["scope"] = "exported_project_canonical_state_owner_lifecycle_loopback_UDP_only;editor_API_verified_separately"

    def execute(name, arguments, marker=None):
        log = output / (name + ".log")
        with log.open("w", encoding="utf-8") as stream:
            result = subprocess.run([str(engine), *arguments], cwd=output, stdout=stream,
                                    stderr=subprocess.STDOUT, timeout=55, **flags)
        text = log.read_text(encoding="utf-8", errors="replace")
        passed = result.returncode == 0 and "ERROR:" not in text and (not marker or marker in text)
        receipt["checks"].append({"name": name, "passed": passed, "exit_code": result.returncode,
                                  "log_sha256": sha(log)})
        if not passed:
            raise RuntimeError(f"Failed {name}; see {log}")

    try:
        if not packaged:
            execute("api", ["--headless", "--dump-extension-api"])
            api = json.loads((output / "extension_api.json").read_text())
            names = {row["name"] for row in api["classes"]}
            required = {"SuperposSession", "SuperposWorld", "SuperposSchema", "SuperposField", "SuperposUInt64", "SuperposSimulationProvider"}
            retired = {n for n in names if n.startswith(("EGPNet", "Superposition"))}
            if not required <= names or retired:
                raise RuntimeError(f"Incorrect network API: missing {sorted(required - names)}, retired {sorted(retired)}")
            receipt["api_sha256"] = sha(output / "extension_api.json")
            receipt["retired_classes"] = sorted(retired)
        execute("canonical", ["--headless", "--max-fps", "60", *project_args,
                              *script_args("runtime.gd"), "--",
                              "--superpos-permanent-retirement"], "SUPERPOS_EGP_RUNTIME_OK")
        for family, scenario in (("ipv4", "accepted"), ("ipv6", "accepted"),
                                 ("ipv4", "peer_closed"), ("ipv4", "schema_mismatch"), ("ipv4", "key_mismatch")):
            port, processes, streams = pair(family), [], []
            name = family + "-" + scenario
            try:
                for role in ("server", "client"):
                    stream = (output / (name + "-" + role + ".log")).open("w", encoding="utf-8")
                    streams.append(stream)
                    processes.append(subprocess.Popen([str(engine), "--headless", "--max-fps", "60",
                        *project_args, *script_args("udp.gd"), "--", role,
                        str(port), family, scenario], cwd=output, stdout=stream, stderr=subprocess.STDOUT, **flags))
                    if role == "server":
                        time.sleep(0.15)
                deadline = time.monotonic() + 35
                for process in processes:
                    process.wait(timeout=max(0.01, deadline - time.monotonic()))
            finally:
                for process in processes:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
                for stream in streams:
                    stream.close()
            passed = len(processes) == 2
            for role, process in zip(("server", "client"), processes):
                text = (output / (name + "-" + role + ".log")).read_text(errors="replace")
                expected = "SUPERPOS_EGP_UDP_REJECTED" if scenario in ("schema_mismatch", "key_mismatch") else "SUPERPOS_EGP_UDP_OK"
                passed &= process.returncode == 0 and "ERROR:" not in text and expected in text
                if scenario == "peer_closed" and role == "client":
                    failure = re.search(r"SUPERPOS_EGP_UDP_FAILED_PEER[^\r\n]* accepted_probes=(\d+) failure_elapsed_ms=(\d+)", text)
                    passed &= failure is not None and 0 < int(failure.group(2)) < 22000
            receipt["checks"].append({"name": name, "passed": bool(passed)})
            if not passed:
                raise RuntimeError(f"Failed {name}; see {output}")
        receipt["passed"] = True
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        receipt["failure"] = str(error)
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n")
    print(json.dumps({"passed": receipt["passed"], "receipt": str(output / "receipt.json"),
                      "failure": receipt.get("failure")}))
    return 0 if receipt["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
