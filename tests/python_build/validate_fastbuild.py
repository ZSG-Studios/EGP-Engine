#!/usr/bin/env python3
"""Compile and run a real MSVC fixture through EGP's SCons FASTBuild backend.

Checks generated headers, C/C++, duplicate source basenames, spaces in paths,
incremental rebuilds, changed flags and compiler error propagation. --remote
requires real remote work, with local compilation/racing disabled.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--remote", action="store_true")
    parser.add_argument("--workers", default="10.77.64.1")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    fixture = root / ".build" / "fastbuild validation"
    fixture.mkdir(parents=True, exist_ok=True)
    (fixture / "msvc-env.json").unlink(missing_ok=True)
    for directory in ("left", "right", "include files", "local module", "objects/left", "objects/right"):
        (fixture / directory).mkdir(parents=True, exist_ok=True)
    (fixture / "value.in").write_text("42", encoding="utf-8")
    (fixture / "left/private.h").write_text("#define LEFT_PRIVATE_VALUE 0\n", encoding="utf-8")
    (fixture / "left/value.cpp").write_text(
        '#include "generated.h"\n#include "private.h"\nint left() { return VALUE + LEFT_PRIVATE_VALUE; }\n',
        encoding="utf-8",
    )
    (fixture / "right/value.cpp").write_text(
        '#include "generated.h"\nint right() { return VALUE; }\n', encoding="utf-8"
    )
    (fixture / "value.c").write_text("#include CONFIG_HEADER\nint c_value(void) { return VALUE; }\n", encoding="utf-8")
    (fixture / "flags.cpp").write_text(
        'static_assert(FLAVOR == EXPECTED_FLAVOR, "Compiler batch mixed environment flags");\n',
        encoding="utf-8",
    )
    (fixture / "main.cpp").write_text(
        '#include CONFIG_HEADER\n#include <cstdio>\nextern "C" int c_value(void);\n'
        'int left(); int right();\nint main() { std::printf("%d %d\\n", VALUE, FLAVOR); '
        "return left() == VALUE && right() == VALUE && c_value() == VALUE ? 0 : 1; }\n",
        encoding="utf-8",
    )
    header_define = r"\"generated.h\""
    (fixture / "local module/private.h").write_text("#define PRIVATE_VALUE 99\n", encoding="utf-8")
    (fixture / "module-user.c").write_text(
        '#include "private.h"\nint private_value(void) { return PRIVATE_VALUE; }\n', encoding="utf-8"
    )
    (fixture / "local module/SCsub").write_text(
        "Import('env')\nlocal_env = env.Clone(CPPPATH=['.', '#include files'])\n"
        "obj = local_env.Object('#objects/module-user.fixture.obj', '#module-user.c')[0]\nReturn('obj')\n",
        encoding="utf-8",
    )
    (fixture / "SConstruct").write_text(
        f"""
import sys
import json
from pathlib import Path
sys.path.insert(0, {str(root)!r})
from misc.utility.fastbuild import configure
settings_file = Path('msvc-env.json')
settings = {{'MSVC_USE_SETTINGS': json.loads(settings_file.read_text())}} if settings_file.exists() else {{}}
env = Environment(TARGET_ARCH='amd64', tools=['msvc', 'mslink', 'mslib'], **settings)
settings_file.write_text(json.dumps({{k: env['ENV'][k] for k in ('PATH', 'INCLUDE', 'LIB', 'LIBPATH') if k in env['ENV']}}))
env.Replace(platform='windows', arch='x86_64', fastbuild_exe={str(root / ".build/fastbuild/tool/FBuild.exe")!r},
    fastbuild_workers={args.workers!r}, fastbuild_dist={args.remote!r}, fastbuild_distverbose={args.remote!r},
    fastbuild_forceremote={args.remote!r}, CPPPATH=['include files'],
    CPPDEFINES=[('FLAVOR', int(ARGUMENTS.get('flavor', '7'))), ('CONFIG_HEADER', {header_define!r})], CCFLAGS=['/nologo', '/Z7', '/MT'])
configure(env)
env.Append(CPPDEFINES=[('EXPECTED_FLAVOR', int(ARGUMENTS.get('flavor', '7')))])
other_env = env.Clone(CPPDEFINES=[('FLAVOR', 29), ('EXPECTED_FLAVOR', 29)])
def generate(target, source, env):
    Path(str(target[0])).write_text('#define VALUE ' + Path(str(source[0])).read_text() + '\\n')
header = env.Command('include files/generated.h', 'value.in', generate)
objects = [env.Object(target=target, source=source)[0] for target, source in [
    ('objects/left/value.fixture.obj', 'left/value.cpp'),
    ('objects/right/value.fixture.obj', 'right/value.cpp'),
    ('objects/value.fixture.obj', 'value.c'), ('objects/main.fixture.obj', 'main.cpp'),
    ('objects/value_8.fixture.obj', 'value.c')]]
objects.append(SConscript('local module/SCsub', exports={{'env': env}}))
objects.append(env.Object('objects/flags-base.fixture.obj', 'flags.cpp')[0])
objects.append(other_env.Object('objects/flags-other.fixture.obj', 'flags.cpp')[0])
env.Depends(objects, header)
Default([env.Program('validation', objects[:4]), objects[4:]])
""",
        encoding="utf-8",
    )

    def build(*extra, failure=False):
        result = subprocess.run(
            [sys.executable, "-m", "SCons", "-j4", *extra], cwd=fixture, capture_output=True, text=True, timeout=240
        )
        print(result.stdout, end="", flush=True)
        print(result.stderr, end="", flush=True)
        if failure:
            assert result.returncode != 0, "Compiler errors must fail the SCons build"
        else:
            assert result.returncode == 0, "Fixture build failed"
        return result.stdout + result.stderr

    build("-c")
    cold_log = build()
    assert "FASTBuild" in cold_log
    if args.remote:
        assert "<REMOTE: " in cold_log, "No remote compilation evidence in the build output"
    assert subprocess.check_output([str(fixture / "validation.exe")], text=True).strip() == "42 7"
    outputs = sorted((fixture / "objects").rglob("*.obj"))
    assert len(outputs) == 8, (
        f"Expected eight distinct objects, including variants, environment flags and local module, got {outputs}"
    )
    previous = {file: file.stat().st_mtime_ns for file in outputs}
    build()
    assert previous == {file: file.stat().st_mtime_ns for file in outputs}, "No-op build recompiled objects"
    # A private header must not leak into a different directory's implicit
    # dependencies through the shared SCons compiler batch.
    (fixture / "left/private.h").write_text("#define LEFT_PRIVATE_VALUE 0\n// Private header edit.\n", encoding="utf-8")
    build()
    assert all((file.stat().st_mtime_ns != previous[file]) == (file.parent.name == "left") for file in outputs), (
        "A directory-private header rebuilt unrelated objects or missed its dependent object"
    )
    previous = {file: file.stat().st_mtime_ns for file in outputs}
    (fixture / "value.in").write_text("43", encoding="utf-8")
    build()
    assert all(file.stat().st_mtime_ns != previous[file] for file in outputs), (
        "Generated header did not invalidate objects"
    )
    assert subprocess.check_output([str(fixture / "validation.exe")], text=True).strip() == "43 7"
    build("flavor=9")
    assert subprocess.check_output([str(fixture / "validation.exe")], text=True).strip() == "43 9"
    previous = {file: file.stat().st_mtime_ns for file in outputs}
    left_source = fixture / "left/value.cpp"
    original = left_source.read_text()
    left_source.write_text(original + "\n#error EGP_FASTBUILD_EXPECTED_ERROR\n", encoding="utf-8")
    try:
        error_log = build("flavor=9", failure=True)
        assert "EGP_FASTBUILD_EXPECTED_ERROR" in error_log
    finally:
        left_source.write_text(original + "\n// Rebuild after the diagnostic check.\n", encoding="utf-8")
    build("flavor=9")
    assert all(file.stat().st_mtime_ns == previous[file] for file in outputs if file.parent.name != "left"), (
        "Changing one source rebuilt unrelated objects"
    )
    print(
        "PASS: generated/private headers, isolated environment flags, C/C++, spaces, duplicate basenames, incremental builds and failures."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
