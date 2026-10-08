"""Guard qualification configuration isolation and executable selection."""

import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import egp_xmake


class XmakeQualificationTests(unittest.TestCase):
    def test_subprojects_and_receipts_do_not_share_project_or_global_locks(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            outputs = [root / "net/Debug", root / "physics/Release"]
            projects = [root / "net", root / "physics"]
            results = []
            for project, output in zip(projects, outputs):
                command = egp_xmake.configure("xmake", project, output / "native", "Debug")
                cwd, environment = egp_xmake.execution(command, output, root)
                self.assertEqual(cwd, project.resolve())
                self.assertEqual(environment["XMAKE_CONFIGDIR"], str(output.resolve() / "xmake-state"))
                self.assertEqual(environment["XMAKE_GLOBALDIR"], str(output.resolve() / "xmake-global"))
                results.append(environment)
            self.assertNotEqual(results[0]["XMAKE_CONFIGDIR"], results[1]["XMAKE_CONFIGDIR"])
            self.assertNotEqual(results[0]["XMAKE_GLOBALDIR"], results[1]["XMAKE_GLOBALDIR"])

    def test_configure_build_test_reuse_same_isolated_configuration(self):
        project = Path("qualification").resolve()
        output = Path("receipt").resolve()
        commands = [
            egp_xmake.configure("xmake", project, output / "native", "Release"),
            egp_xmake.build("xmake", project, 4),
            egp_xmake.test("xmake", project, "egp_box3d_*/*"),
        ]
        states = [egp_xmake.execution(command, output, Path.cwd()) for command in commands]
        self.assertTrue(all(state == states[0] for state in states))
        self.assertIn("-j", commands[2])
        self.assertEqual(commands[2][commands[2].index("-j") + 1], "1")

    def test_sanitizer_and_toolchain_reach_the_native_configure_step(self):
        command = egp_xmake.configure("xmake", Path.cwd(), Path("native"), "Debug", "address,undefined", "clang")
        self.assertIn("--sanitizer=address,undefined", command)
        self.assertIn("--toolchain=clang", command)

    def test_non_xmake_runtime_keeps_original_working_directory(self):
        root = Path.cwd()
        with patch.dict(os.environ, {}, clear=True):
            cwd, environment = egp_xmake.execution(["engine", "--headless"], root / "receipt", root)
            self.assertEqual(cwd, root)
            self.assertNotIn("XMAKE_CONFIGDIR", environment)

    def test_missing_explicit_tool_fails_instead_of_falling_back_to_old_backend(self):
        with patch.dict(os.environ, {"XMAKE": "missing-xmake"}), patch("shutil.which", return_value=None):
            with self.assertRaisesRegex(RuntimeError, "xmake 3.1.1"):
                egp_xmake.executable()


if __name__ == "__main__":
    unittest.main()
