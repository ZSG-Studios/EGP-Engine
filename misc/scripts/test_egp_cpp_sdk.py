"""Verify native Lua SDK packaging and cache publication."""

import hashlib
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class BundledSDKTest(unittest.TestCase):
    def require_xmake(self):
        xmake = os.environ.get("XMAKE") or shutil.which("xmake")
        if not xmake:
            if os.environ.get("CI"):
                self.fail("Native SDK verification requires xmake on CI")
            self.skipTest("xmake is required for native SDK verification")
        return xmake

    def test_concurrent_cache_publication(self):
        xmake = self.require_xmake()
        publisher = ROOT / "editor/settings/gdextension/cpp_sdk/tools/publish_cache.lua"
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
                        [xmake, "lua", str(publisher), str(source), str(destination)],
                        stdout=subprocess.PIPE,
                        stderr=subprocess.STDOUT,
                    )
                )
            completed = [(process, process.communicate(timeout=30)[0]) for process in processes]
            for process, output in completed:
                self.assertEqual(process.returncode, 0, output.decode(errors="replace"))
            self.assertIn(destination.read_bytes(), payloads)
            self.assertFalse(destination.with_suffix(".lib.tmp").exists())

    def test_native_lua_packaging(self):
        xmake = self.require_xmake()
        api = Path(os.environ.get("EGP_CPP_API", ROOT / "thirdparty/godot-cpp/gdextension/extension_api-4-7.json"))
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "sdk"
            probe = Path(directory) / "package_locale.lua"
            probe.write_text(
                """function main(root, output, api)
    local native_os = debug.global("os")
    assert(native_os.setlocale("C", "collate"))
    local canonical = {"src/A", "src/Z", "src/_", "src/a", "src/z"}
    local applied
    for _, locale in ipairs({"en_US.UTF-8", "en_US.utf8", "English_United States.1252"}) do
        applied = native_os.setlocale(locale, "collate")
        if applied then break end
    end
    assert(applied, "A real non-C collation locale is required for the SDK regression")
    local previous = table.clone(canonical)
    table.sort(previous)
    assert(table.concat(previous, "|") ~= table.concat(canonical, "|"), "Non-C negative control must change default Lua filename ordering")
    local extractor = import("misc.scripts.extract_egp_cpp_sdk", {rootdir=root})
    extractor.main("--output", output, "--api", api)
    print("SDK_REAL_NON_C_COLLATION_PASS")
end
""",
                encoding="utf-8",
            )
            subprocess.run(
                [
                    xmake,
                    "lua",
                    str(probe),
                    str(ROOT),
                    str(output),
                    str(api),
                ],
                cwd=ROOT,
                check=True,
                timeout=360,
            )
            files = {
                path.relative_to(output).as_posix(): path.read_bytes() for path in output.rglob("*") if path.is_file()
            }
        self.assertIn("gen/include/gdextension_interface.h", files)
        self.assertIn("gen/include/godot_cpp/classes/node.hpp", files)
        self.assertIn("LICENSE.md", files)
        self.assertEqual(
            json.loads(files["sdk.json"])["api_sha256"],
            hashlib.sha256(api.read_bytes()).hexdigest(),
        )
        header = json.loads(api.read_text())["header"]
        minimum = f'compatibility_minimum = "{header["version_major"]}.{header["version_minor"]}"'.encode()
        self.assertIn(minimum, files["templates/extension.gdextension.in"])
        self.assertNotIn(b"@BITS@", files["xmake.lua"])
        self.assertFalse(any(name.endswith(".py") for name in files))
        self.assertFalse(any(".build" in Path(name).parts or ".xmake" in Path(name).parts for name in files))
        self.assertIn("tools/doc_source_generator.lua", files)
        metadata = json.loads(files["sdk.json"])
        fingerprint = b"".join(
            name.encode() + hashlib.sha256(files[name]).digest() for name in sorted(files) if name != "sdk.json"
        )
        self.assertEqual(metadata["source_sha256"], hashlib.sha256(fingerprint).hexdigest())
        self.assertEqual(metadata["api_major"], header["version_major"])
        self.assertEqual(metadata["api_minor"], header["version_minor"])
        self.assertEqual(str(metadata["bits"]), "64")
        self.assertEqual(metadata["precision"], "single")
        self.assertEqual(metadata["build_system"], "xmake")
        self.assertEqual(metadata["xmake_version"], "3.1.1")
        self.assertEqual(metadata["public_cpp_standard"], "c++17")
        self.assertEqual(metadata["entrypoint"], "xmake.lua")
        self.assertIn(b'set_xmakever("3.1.1")', files["xmake.lua"])
        self.assertIn("tools/xmake.lock.json", files)
        self.assertFalse(any(name.endswith(("CMakeLists.txt", "SConstruct", "SConscript", ".bff")) for name in files))


if __name__ == "__main__":
    unittest.main()
