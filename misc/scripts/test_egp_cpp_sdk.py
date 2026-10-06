"""Regression check for SDK packaging inside Godot's shared Python build environment."""

import hashlib
import importlib.util
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "native_extension_sdk", ROOT / "editor/settings/gdextension/native_extension_sdk.py"
)
assert SPEC is not None and SPEC.loader is not None
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)


class BundledSDKTest(unittest.TestCase):
    def test_concurrent_cache_publication(self):
        cmake = shutil.which("cmake")
        if not cmake:
            self.skipTest("CMake is required for the publication concurrency check")
        publisher = ROOT / "editor/settings/gdextension/cpp_sdk/tools/publish_cache.cmake"
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            destination = folder / "shared/library.lib"
            payloads = [b"a" * (4 * 1024 * 1024), b"b" * (4 * 1024 * 1024)]
            processes = []
            for index, payload in enumerate(payloads):
                source = folder / f"source-{index}.lib"
                source.write_bytes(payload)
                processes.append(
                    subprocess.Popen(
                        [cmake, f"-DSOURCE_FILE={source}", f"-DDESTINATION={destination}", "-P", str(publisher)],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                    )
                )
            for process in processes:
                output, _ = process.communicate(timeout=30)
                self.assertEqual(process.returncode, 0, output.decode(errors="replace"))
            self.assertIn(destination.read_bytes(), payloads)
            self.assertFalse(destination.with_suffix(".lib.tmp").exists())

    def test_packaging_with_conflicting_engine_generator(self):
        old_module = sys.modules.get("make_interface_header")
        engine_generator = types.ModuleType("make_interface_header")
        sys.modules["make_interface_header"] = engine_generator
        try:
            archive = SDK.make_archive(ROOT / "thirdparty/godot-cpp", ROOT / "editor/settings/gdextension/cpp_sdk")
            self.assertIs(sys.modules["make_interface_header"], engine_generator)
        finally:
            if old_module is None:
                sys.modules.pop("make_interface_header", None)
            else:
                sys.modules["make_interface_header"] = old_module
        files = {}
        offset = 0
        while offset < len(archive):
            name_size, data_size = struct.unpack_from("<II", archive, offset)
            offset += 8
            name = archive[offset : offset + name_size].decode()
            offset += name_size
            self.assertNotIn("..", Path(name).parts)
            self.assertFalse(Path(name).is_absolute())
            self.assertNotIn(name, files)
            files[name] = archive[offset : offset + data_size]
            offset += data_size
        self.assertEqual(offset, len(archive))
        self.assertIn("gen/include/gdextension_interface.h", files)
        self.assertIn("gen/include/godot_cpp/classes/node.hpp", files)
        self.assertIn("LICENSE.md", files)
        self.assertEqual(
            json.loads(files["sdk.json"])["api_sha256"],
            hashlib.sha256((ROOT / "thirdparty/godot-cpp/gdextension/extension_api-4-7.json").read_bytes()).hexdigest(),
        )
        self.assertIn(b'compatibility_minimum = "4.7"', files["templates/extension.gdextension.in"])
        self.assertNotIn(b"@BITS@", files["CMakeLists.txt"])
        self.assertNotIn(b"find_package(Python", files["CMakeLists.txt"])


if __name__ == "__main__":
    unittest.main()
