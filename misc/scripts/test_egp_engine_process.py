import hashlib
import tempfile
import unittest
from pathlib import Path

from egp_engine_process import resolve_engine_process


class EngineProcessTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="egp engine paths ")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()

    def write(self, name, data=b"engine"):
        path = self.root / name
        path.write_bytes(data)
        return path

    def test_console_resolves_direct_engine_and_records_both_hashes(self):
        launcher = self.write("godot.windows.editor.dev.x86_64.mono.console.exe", b"launcher")
        engine = self.write("godot.windows.editor.dev.x86_64.mono.exe")
        actual, proof = resolve_engine_process(launcher, "nt")
        self.assertEqual(actual, engine)
        self.assertTrue(proof["console_launcher_resolved"])
        self.assertEqual(proof["requested"], str(launcher))
        self.assertEqual(proof["effective"], str(engine))
        self.assertEqual(proof["requested_sha256"], hashlib.sha256(b"launcher").hexdigest())
        self.assertEqual(proof["effective_sha256"], hashlib.sha256(b"engine").hexdigest())

    def test_template_console_resolves(self):
        launcher = self.write("godot.windows.template_release.x86_64.mono.console.exe")
        engine = self.write("godot.windows.template_release.x86_64.mono.exe")
        self.assertEqual(resolve_engine_process(launcher, "nt")[0], engine)

    def test_alias_path_records_canonical_identity_and_hashes(self):
        launcher = self.write("godot.windows.editor.dev.x86_64.console.exe", b"launcher")
        engine = self.write("godot.windows.editor.dev.x86_64.exe")
        alias = self.root / "directory alias"
        try:
            alias.symlink_to(self.root, target_is_directory=True)
        except OSError:
            alias.mkdir()
            alias = alias / ".."
        actual, proof = resolve_engine_process(alias / launcher.name, "nt")
        self.assertEqual(actual, engine)
        self.assertEqual(proof["requested"], str(launcher))
        self.assertEqual(proof["effective"], str(engine))
        self.assertEqual(proof["requested_sha256"], hashlib.sha256(b"launcher").hexdigest())
        self.assertEqual(proof["effective_sha256"], hashlib.sha256(b"engine").hexdigest())

    def test_missing_direct_sibling_fails_without_using_launcher(self):
        launcher = self.write("godot.windows.editor.dev.x86_64.console.exe")
        with self.assertRaisesRegex(FileNotFoundError, "direct engine sibling"):
            resolve_engine_process(launcher, "nt")

    def test_missing_requested_engine_fails_even_when_sibling_exists(self):
        self.write("godot.windows.editor.dev.x86_64.exe")
        with self.assertRaisesRegex(FileNotFoundError, "Engine executable does not exist"):
            resolve_engine_process(self.root / "godot.windows.editor.dev.x86_64.console.exe", "nt")

    def test_ordinary_exported_and_other_platform_executables_unchanged(self):
        for name in ("godot.windows.editor.dev.x86_64.exe", "Game.console.exe", "godot.linuxbsd.editor.console.exe"):
            with self.subTest(name=name):
                engine = self.write(name)
                actual, proof = resolve_engine_process(engine, "nt")
                self.assertEqual(actual, engine)
                self.assertFalse(proof["console_launcher_resolved"])
                self.assertEqual(proof["requested_sha256"], proof["effective_sha256"])

    def test_non_windows_host_does_not_reinterpret_console_filename(self):
        launcher = self.write("godot.windows.editor.dev.x86_64.console.exe", b"launcher")
        self.write("godot.windows.editor.dev.x86_64.exe")
        self.assertEqual(resolve_engine_process(launcher, "posix")[0], launcher)


if __name__ == "__main__":
    unittest.main()
