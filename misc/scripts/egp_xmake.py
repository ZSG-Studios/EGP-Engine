"""Small command helpers for genuine xmake qualification projects."""

import os
import shutil
from pathlib import Path


def executable():
    requested = os.environ.get("XMAKE", "xmake")
    resolved = shutil.which(requested)
    if not resolved:
        raise RuntimeError("xmake 3.1.1 or newer is required; put xmake on PATH or set XMAKE to its executable")
    return str(Path(resolved).resolve())


def configure(tool, project, build, configuration, sanitizer="none", toolchain=None):
    command = [
        tool,
        "f",
        "-P",
        str(project),
        "-m",
        configuration.lower(),
        "-o",
        str(build),
        "-y",
        "--sanitizer=" + sanitizer,
    ]
    if toolchain:
        command.append("--toolchain=" + toolchain)
    return command


def add_options(parser):
    parser.add_argument(
        "--sanitizer", choices=("none", "address", "undefined", "address,undefined", "thread", "memory"), default="none"
    )
    parser.add_argument("--toolchain", help="Explicit xmake toolchain, for example clang or clang-cl")


def build(tool, project, parallel, target=None):
    command = [tool, "build", "-P", str(project), "-j", str(parallel), "-v"]
    if target:
        command.append(target)
    return command


def test(tool, project, *patterns):
    # Serial execution prevents shared UDP test ports from colliding. Every case has its own watchdog.
    return [tool, "test", "-P", str(project), "-j", "1", "-v", *patterns]


def binary(build, name):
    return Path(build) / "bin" / (name + (".exe" if os.name == "nt" else ""))


def execution(command, output, default_cwd):
    """Isolate xmake's working-directory project lock and global detection cache."""
    environment = dict(os.environ)
    if "-P" not in command:
        return default_cwd, environment
    project = Path(command[command.index("-P") + 1]).resolve()
    environment["XMAKE_CONFIGDIR"] = str(Path(output).resolve() / "xmake-state")
    environment["XMAKE_GLOBALDIR"] = str(Path(output).resolve() / "xmake-global")
    return project, environment
