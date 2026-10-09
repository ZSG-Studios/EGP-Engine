"""Check that docs publication selects only version-controlled class XML."""

import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import sync_egp_docs


class TrackedDocumentationTest(unittest.TestCase):
    def test_staged_classes_publish_without_unrelated_untracked_modules(self):
        with tempfile.TemporaryDirectory(prefix="egp docs inputs ") as directory:
            root = Path(directory)
            subprocess.run(["git", "init", "--quiet", str(root)], check=True)
            expected = [
                "doc/classes/Node.xml",
                "modules/superpos/doc_classes/SuperposWorld.xml",
                "platform/windows/doc_classes/EditorExportPlatformWindows.xml",
            ]
            unrelated = [
                "modules/other_chat/doc_classes/Unfinished.xml",
                "modules/superpos/configuration.xml",
            ]
            for name in expected + unrelated:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('<class name="Example"/>', encoding="utf-8")
            subprocess.run(["git", "add", "--", *expected, unrelated[1]], cwd=root, check=True)
            with patch.object(sync_egp_docs, "ROOT", root):
                self.assertEqual(sync_egp_docs.tracked_xml_sources(), sorted(root / name for name in expected))


if __name__ == "__main__":
    unittest.main()
