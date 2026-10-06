"""Regression check for SDK packaging inside Godot's shared Python build environment."""

import importlib.util
import struct
import sys
import types
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "native_extension_sdk", ROOT / "editor/settings/gdextension/native_extension_sdk.py")
SDK = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SDK)


class BundledSDKTest(unittest.TestCase):
    def test_packaging_with_conflicting_engine_generator(self):
        old_module = sys.modules.get("make_interface_header")
        engine_generator = types.ModuleType("make_interface_header")
        sys.modules["make_interface_header"] = engine_generator
        try:
            archive = SDK.make_archive(ROOT / "thirdparty/godot-cpp",
                                       ROOT / "editor/settings/gdextension/cpp_sdk")
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
            name = archive[offset:offset + name_size].decode()
            offset += name_size
            self.assertNotIn("..", Path(name).parts)
            self.assertFalse(Path(name).is_absolute())
            self.assertNotIn(name, files)
            files[name] = archive[offset:offset + data_size]
            offset += data_size
        self.assertEqual(offset, len(archive))
        self.assertIn("gen/include/gdextension_interface.h", files)
        self.assertIn("gen/include/godot_cpp/classes/node.hpp", files)
        self.assertIn("LICENSE.md", files)
        self.assertIn(b'compatibility_minimum = "4.7"', files["templates/extension.gdextension.in"])
        self.assertNotIn(b"@BITS@", files["CMakeLists.txt"])
        self.assertNotIn(b"find_package(Python", files["CMakeLists.txt"])


if __name__ == "__main__":
    unittest.main()
