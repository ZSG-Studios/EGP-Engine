#!/usr/bin/env python3
"""Verification only: qualify native Lua/host-tool generated engine data."""

import argparse
import hashlib
import json
import re
import subprocess
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def tokens(text):
    return re.findall(r"\w+|[^\s]", re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xmake", required=True)
    parser.add_argument("--compressor", required=True)
    parser.add_argument("--reference-dir", type=Path)
    args = parser.parse_args()
    process = subprocess.run(
        [args.xmake, "lua", "tests/build/test_xmake_generators.lua", args.compressor],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
    )
    assert "XMAKE_NATIVE_GENERATORS_PASS 17" in process.stdout, process.stdout + process.stderr
    folder = ROOT / ".build/xmake-generator-tests"
    expected = "hello\0UTF8: café\n".encode() + b"compressible\n" * 1000
    assert zlib.decompress((folder / "compression.zlib").read_bytes()) == expected
    crc_input = folder / "crc-input.bin"
    crc_input.write_bytes(expected)
    crc = subprocess.check_output([args.compressor, "--input", str(crc_input), "--crc32"], text=True).strip()
    assert int(crc, 16) == zlib.crc32(expected)
    raw_output = folder / "raw.deflate"
    subprocess.run(
        [args.compressor, "--input", str(crc_input), "--output", str(raw_output), "--format", "raw"], check=True
    )
    assert zlib.decompress(raw_output.read_bytes(), -15) == expected
    interface = (folder / "core.extension.make_interface_header.run.gen.h").read_text(encoding="utf-8")
    checks = {"lua_checks": 17, "binary_zlib_roundtrip": True, "binary_raw_deflate_roundtrip": True, "crc32": crc}
    checks["interface_tokens_sha256"] = hashlib.sha256(json.dumps(tokens(interface)).encode()).hexdigest()
    if args.reference_dir:
        reference = (args.reference_dir / "gdextension_interface.gen.h").read_text(encoding="utf-8")
        assert tokens(reference) == tokens(interface), "Current engine interface declarations changed"
        checks["interface_token_parity"] = len(tokens(interface))

        def macros(text):
            return {
                match.group(1): tokens(match.group(0).strip())
                for match in re.finditer(r"^#define (\w+)[^\n]*(?:\\\n[^\n]*)*", text, re.M)
            }

        reference = (args.reference_dir / "ext_wrappers.gen.h").read_text(encoding="utf-8")
        generated = (folder / "core.extension.make_wrappers.run.gen.h").read_text(encoding="utf-8")
        assert macros(reference) == macros(generated), "Native engine binding macros changed"
        checks["wrapper_macro_parity"] = len(macros(generated))
    (folder / "qualification.json").write_text(json.dumps(checks, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(checks))


if __name__ == "__main__":
    main()
