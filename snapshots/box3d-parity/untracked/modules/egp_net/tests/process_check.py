"""Secure admission and replication between separate native processes."""
from pathlib import Path
import subprocess
import sys
import tempfile
import time

with tempfile.TemporaryDirectory(prefix="egp-net-process-") as directory:
    token = Path(directory) / "admission.bin"
    server = subprocess.Popen([sys.argv[1], "server", str(token)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    try:
        deadline = time.monotonic() + 5
        while (not token.is_file() or token.stat().st_size != 2048) and server.poll() is None and time.monotonic() < deadline:
            time.sleep(0.02)
        client = subprocess.run([sys.argv[1], "client", str(token)], capture_output=True, text=True, timeout=12)
        output, _ = server.communicate(timeout=12)
        print(output.strip())
        print(client.stdout.strip())
        if server.returncode or client.returncode or "EGP_PROCESS_SERVER_PASS" not in output or "EGP_PROCESS_CLIENT_PASS" not in client.stdout:
            raise RuntimeError(f"Separate-process exchange failed: server={server.returncode}, client={client.returncode}")
    finally:
        if server.poll() is None:
            server.kill()
            server.wait()
