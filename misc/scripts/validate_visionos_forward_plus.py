"""Run the external stereo fixture with a hard deadline and retained evidence."""

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--driver", choices=["vulkan", "metal"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    engine = args.engine.resolve()
    env = os.environ.copy()
    env["EGP_STEREO_OUTPUT"] = str(output)
    env["APPDATA" if os.name == "nt" else "XDG_DATA_HOME"] = str(output / "userdata")
    # Exclude third-party implicit Vulkan injection from the graphics fixture.
    if args.driver == "vulkan":
        env["VK_LOADER_LAYERS_DISABLE"] = "~implicit~"
    startup = None
    if os.name == "nt":
        startup = subprocess.STARTUPINFO()
        startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
    command = [
        str(engine),
        "--path",
        str(root / "misc/egp/visionos_forward_plus"),
        "--rendering-method",
        "forward_plus",
        "--rendering-driver",
        args.driver,
        "--audio-driver",
        "Dummy",
        "--max-fps",
        "60",
        "--resolution",
        "256x256",
    ]
    # Keep local Windows tests hidden. A hosted Mac needs an on-screen drawable
    # for CAMetalLayer; an off-screen window can block before the scene starts.
    if os.name == "nt":
        command.extend(["--position", "-32000,-32000"])
    command.extend(["--", "--mock-xr"])
    log_path = output / "stereo.log"
    timed_out = False
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT, startupinfo=startup)
        try:
            exit_code = process.wait(timeout=55)
        except subprocess.TimeoutExpired:
            timed_out = True
            exit_code = -1
            if sys.platform == "darwin":
                try:
                    subprocess.run(
                        ["sample", str(process.pid), "1", "-file", str(output / "hang-sample.txt")],
                        capture_output=True,
                        timeout=5,
                        check=False,
                    )
                except subprocess.TimeoutExpired:
                    pass
            process.kill()
            process.wait()
    text = log_path.read_text(encoding="utf-8", errors="replace")
    marker = "EGP_EXTERNAL_STEREO_PASS "
    passes = [line.split(marker, 1)[1] for line in text.splitlines() if marker in line]
    graphics_errors = any(token in text for token in ("SCRIPT ERROR:", "ERROR:", "EGP_EXTERNAL_STEREO_FAIL"))
    passed = exit_code == 0 and len(passes) == 1 and not graphics_errors
    receipt = {
        "status": "PASS" if passed else "FAIL",
        "exit_code": exit_code,
        "timed_out": timed_out,
        "driver": args.driver,
        "device_validated": False,
        "scope": "External stereo colour/depth and resize only; no Apple compositor or headset validation",
        "engine": str(engine),
        "engine_sha256": hashlib.sha256(engine.read_bytes()).hexdigest(),
        "log_sha256": hashlib.sha256(log_path.read_bytes()).hexdigest(),
    }
    if passes:
        receipt["stereo"] = json.loads(passes[-1])
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")
    print(text[-10000:])
    print(json.dumps(receipt, indent=2))
    return 0 if passed else 1


if __name__ == "__main__":
    sys.exit(main())
