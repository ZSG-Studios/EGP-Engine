"""Fast routing and packaging regressions; no engine compilation required."""

import hashlib
import importlib.util
import json
import tempfile
import unittest
import zlib
from pathlib import Path
from unittest.mock import patch

import build_egp_cpp_editor as builder


class EditorPackagingTests(unittest.TestCase):
    def test_native_flags_are_preserved(self):
        flags = ["-j4", "platform=windows", "arch=x86_64", "module_mono_enabled=yes", "cache_path=a b"]
        self.assertEqual(builder.host_flags(flags, "Windows", "AMD64"), (flags, False))

    def test_mac_cross_arch_preserves_api_configuration(self):
        flags = ["platform=macos", "arch=x86_64", "metal=no", "precision=double", "dev_mode=yes"]
        host, cross = builder.host_flags(flags, "Darwin", "arm64")
        self.assertTrue(cross)
        self.assertEqual(host, ["platform=macos", "metal=no", "precision=double", "dev_mode=yes", "arch=arm64"])

    def test_unsupported_foreign_editor_fails_before_build(self):
        for flags, system, machine in [
            (["platform=linuxbsd"], "Windows", "AMD64"),
            (["platform=macos", "arch=arm32"], "Darwin", "arm64"),
            (["platform=linuxbsd", "arch=arm64"], "Linux", "x86_64"),
        ]:
            with self.subTest(flags=flags), self.assertRaises(ValueError):
                builder.host_flags(flags, system, machine)

    def test_old_dump_cannot_satisfy_successful_empty_export(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            (folder / "extension_api.json").write_text('{"header": {}}', encoding="utf-8")
            with patch.object(builder.subprocess, "run"), self.assertRaisesRegex(RuntimeError, "without dumping"):
                builder.dump_api(Path("editor"), folder)

    def run_fake_build(self, cross=False, final_api_changes=False, previous_api=False):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "bin").mkdir()
            calls = []
            dumps = []
            embedded_versions = []
            if previous_api:
                api = root / ".build/egp-cpp-api/extension_api.json"
                api.parent.mkdir(parents=True)
                api.write_text('{"header":{"version_major":4,"version_minor":7}}', encoding="utf-8")

            def run(command, cwd, **kwargs):
                calls.append(command)
                if "SCons" in command:
                    api_flag = next((value for value in command if value.startswith("egp_cpp_api=")), None)
                    if api_flag:
                        embedded_versions.append(
                            json.loads(Path(api_flag.split("=", 1)[1]).read_text())["header"]["version_minor"]
                        )
                    arch = next((value.split("=", 1)[1] for value in command if value.startswith("arch=")), "x86_64")
                    suffix = "." + arch
                    (root / "bin" / ("godot" + suffix)).touch()
                    (root / ".scons_env.json").write_text(
                        json.dumps({"PROGSUFFIX": suffix, "arch": arch, "precision": "single"}), encoding="utf-8"
                    )
                else:
                    dumps.append(Path(command[0]).name)
                    header = {"version_major": 4, "version_minor": 8}
                    if final_api_changes and len(dumps) == 2:
                        header["version_minor"] = 9
                    (Path(cwd) / "extension_api.json").write_text(json.dumps({"header": header}), encoding="utf-8")

            flags = ["platform=macos", "arch=x86_64", "cache_path=a b", "redirect_build_objects=no"]
            initial = (
                ["platform=macos", "arch=arm64", "cache_path=a b", "redirect_build_objects=no"] if cross else flags
            )
            with (
                patch.object(builder, "host_flags", return_value=(initial, cross)),
                patch.object(builder.subprocess, "run", side_effect=run),
                patch.object(builder, "validate_sdk", return_value={"api_sha256": "verified"}),
            ):
                builder.build_editor(root, flags)
            builds = [command for command in calls if "SCons" in command]
            self.assertEqual(len(builds), 2)
            self.assertIn("cache_path=a b", builds[0])
            self.assertIn("cache_path=a b", builds[1])
            self.assertEqual(any(value.startswith("egp_cpp_api=") for value in builds[0]), previous_api)
            self.assertTrue(any(value.startswith("egp_cpp_api=") for value in builds[1]))
            self.assertEqual(embedded_versions, [7, 8] if previous_api else [8])
            return dumps

    def test_native_final_editor_redumps_api(self):
        self.assertEqual(self.run_fake_build(), ["godot.x86_64", "godot.x86_64"])

    def test_mac_cross_build_never_executes_foreign_slice(self):
        self.assertEqual(self.run_fake_build(cross=True), ["godot.arm64"])

    def test_repeat_build_bootstraps_existing_api_but_embeds_fresh_dump(self):
        self.assertEqual(self.run_fake_build(previous_api=True), ["godot.x86_64", "godot.x86_64"])

    def test_api_changes_between_passes_are_rejected(self):
        with self.assertRaisesRegex(RuntimeError, "changed during SDK embedding"):
            self.run_fake_build(final_api_changes=True)

    def test_embedded_archive_checks_content_and_hash(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            folder = root / "editor/settings/gdextension"
            folder.mkdir(parents=True)
            api = root / "extension_api.json"
            api.write_text('{"header":{"version_major":4,"version_minor":8}}', encoding="utf-8")
            archive = b"exact generated SDK including metadata and all bindings"
            data = ",".join(str(value) for value in zlib.compress(archive))
            (folder / "native_extension_sdk.gen.h").write_text(
                f'const char *egp_cpp_sdk_hash = "{hashlib.sha256(archive).hexdigest()}";\n'
                f"const int egp_cpp_sdk_size = {len(archive)};\n"
                f"const unsigned char egp_cpp_sdk_data[] = {{{data},}};\n",
                encoding="utf-8",
            )
            sdk = unittest.mock.Mock()
            sdk.make_archive.return_value = archive
            spec = unittest.mock.Mock()
            with (
                patch.object(importlib.util, "spec_from_file_location", return_value=spec),
                patch.object(importlib.util, "module_from_spec", return_value=sdk),
            ):
                receipt = builder.validate_sdk(root, api, {"arch": "arm64", "precision": "double"})
                self.assertEqual(receipt["api_sha256"], hashlib.sha256(api.read_bytes()).hexdigest())
                self.assertEqual(receipt["precision"], "double")
                editor = root / "editor.bin"
                editor.write_bytes(b"executable prefix" + zlib.compress(archive) + b"executable suffix")
                builder.validate_sdk(root, api, {"arch": "arm64", "precision": "double"}, editor)
                editor.write_bytes(b"stale executable does not contain SDK")
                with self.assertRaisesRegex(RuntimeError, "does not contain"):
                    builder.validate_sdk(root, api, {"arch": "arm64", "precision": "double"}, editor)
                sdk.make_archive.return_value = b"stale Godot SDK"
                with self.assertRaisesRegex(RuntimeError, "does not match"):
                    builder.validate_sdk(root, api, {"arch": "arm64", "precision": "double"})
            header = folder / "native_extension_sdk.gen.h"
            header.write_text(header.read_text().replace(hashlib.sha256(archive).hexdigest(), "0" * 64))
            with self.assertRaisesRegex(RuntimeError, "size/hash mismatch"):
                builder.validate_sdk(root, api, {"arch": "arm64", "precision": "double"})

    def test_composite_routes_editors_and_preserves_templates(self):
        action = (Path(__file__).resolve().parents[2] / ".github/actions/godot-build/action.yml").read_text()
        self.assertIn('if [ "${{ inputs.target }}" = "editor" ]; then', action)
        self.assertIn("python misc/scripts/build_egp_cpp_editor.py -- platform=", action)
        self.assertIn("scons platform=${{ inputs.platform }} target=${{ inputs.target }}", action)
        self.assertIn('export BUILD_NAME="gh-${{ github.event.number }}"', action)


if __name__ == "__main__":
    unittest.main()
