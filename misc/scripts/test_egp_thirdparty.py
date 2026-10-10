"""Tests for the thirdparty manifest pinner and verifier."""

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import egp_thirdparty as thirdparty
from egp_vendor_manifest import load_upstream_manifest


def digest(text: str) -> str:
    return thirdparty.content_digest(text.encode())


class ContentDigestTest(unittest.TestCase):
    def test_crlf_and_lf_checkouts_hash_identically(self):
        self.assertEqual(thirdparty.content_digest(b"a\r\nb\r\n"), thirdparty.content_digest(b"a\nb\n"))

    def test_lone_carriage_returns_are_content(self):
        self.assertNotEqual(thirdparty.content_digest(b"a\rb"), thirdparty.content_digest(b"a\nb"))


class ClassifyTest(unittest.TestCase):
    def test_identical_patched_and_added_files(self):
        upstream = {"lib/src/a.c": digest("a"), "lib/src/b.c": digest("b"), "README": digest("r")}
        vendored = {"src/a.c": digest("a"), "src/b.c": digest("b-modified"), "patches/0001.patch": digest("p")}
        files, patched, additions = thirdparty.classify(vendored, upstream)
        self.assertEqual(files, {"src/a.c": digest("a")})
        self.assertEqual(patched["src/b.c"]["upstream_path"], "lib/src/b.c")
        self.assertEqual(patched["src/b.c"]["upstream_sha256"], digest("b"))
        self.assertEqual(additions, {"patches/0001.patch": digest("p")})

    def test_counterpart_prefers_longest_shared_suffix(self):
        candidates = ["other/util.h", "lib/core/util.h"]
        self.assertEqual(thirdparty.counterpart("core/util.h", candidates), "lib/core/util.h")
        self.assertIsNone(thirdparty.counterpart("missing.h", candidates))


class VerifyTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        self.library = self.root / "thirdparty" / "demo"
        (self.library / "src").mkdir(parents=True)
        (self.library / "src" / "a.c").write_text("int a;\n", encoding="utf-8")
        (self.library / "src" / "b.c").write_text("int b = 2;\n", encoding="utf-8")
        (self.root / "demo.patch").write_text("patch\n", encoding="utf-8")
        self.manifest = {
            "schema": thirdparty.SCHEMA,
            "name": "demo",
            "license": "MIT",
            "upstream": {"repository": "https://example.invalid/demo", "version": "1.0", "commit": "a" * 40},
            "verification": {"method": "upstream-commit"},
            "files": {"src/a.c": digest("int a;\n")},
            "patched": {
                "src/b.c": {
                    "upstream_path": "src/b.c",
                    "upstream_sha256": digest("int b;\n"),
                    "sha256": digest("int b = 2;\n"),
                    "patch": "demo-v1",
                }
            },
            "additions": {},
            "patches": [{"name": "demo-v1", "path": "demo.patch", "sha256": digest("patch\n")}],
        }
        self.write_manifest()
        patcher = mock.patch.object(thirdparty, "ROOT", self.root)
        patcher.start()
        self.addCleanup(patcher.stop)

    def tearDown(self):
        self.temporary.cleanup()

    def write_manifest(self):
        (self.library / thirdparty.MANIFEST).write_text(json.dumps(self.manifest), encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=self.root, check=True)

    def test_clean_library_passes(self):
        self.assertEqual(thirdparty.check_library(self.library), [])

    def test_modified_file_fails(self):
        (self.library / "src" / "a.c").write_text("int a = 1;\n", encoding="utf-8")
        self.assertIn(
            "demo: src/a.c differs from its pin (re-pin after reviewing the change)",
            thirdparty.check_library(self.library),
        )

    def test_unlisted_and_missing_files_fail(self):
        (self.library / "src" / "c.c").write_text("int c;\n", encoding="utf-8")
        subprocess.run(["git", "add", "-A"], cwd=self.root, check=True)
        self.manifest["files"]["src/gone.c"] = digest("x")
        self.write_manifest()
        errors = thirdparty.check_library(self.library)
        self.assertIn("demo: src/c.c is vendored but not in UPSTREAM.json", errors)
        self.assertIn("demo: src/gone.c is in UPSTREAM.json but not vendored", errors)

    def test_changed_patch_file_fails(self):
        (self.root / "demo.patch").write_text("different\n", encoding="utf-8")
        self.assertTrue(any("patch 'demo-v1'" in error for error in thirdparty.check_library(self.library)))

    def test_patch_created_file_needs_a_patch(self):
        (self.library / "src" / "new.c").write_text("int n;\n", encoding="utf-8")
        entry = {"upstream_path": None, "upstream_sha256": None, "sha256": digest("int n;\n"), "patch": "demo-v1"}
        self.manifest["patched"]["src/new.c"] = entry
        self.write_manifest()
        self.assertEqual(thirdparty.check_library(self.library), [])
        del entry["patch"]
        self.write_manifest()
        self.assertTrue(any("patched[src/new.c]" in error for error in thirdparty.check_library(self.library)))

    def test_upstream_commit_requires_full_commit(self):
        self.manifest["upstream"]["commit"] = "abc123"
        self.write_manifest()
        self.assertTrue(any("40-character commit" in error for error in thirdparty.check_library(self.library)))

    def test_reader_keeps_upstream_pin_and_patch_records(self):
        view = load_upstream_manifest(self.library)
        self.assertEqual(view["files"], {"src/a.c": digest("int a;\n"), "src/b.c": digest("int b;\n")})
        self.assertEqual(view["egp_patches"][0]["files"]["src/b.c"]["patched_sha256"], digest("int b = 2;\n"))


if __name__ == "__main__":
    unittest.main()
