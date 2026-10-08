"""Reject changed content and non-text normalization in vendored source checks."""

import hashlib
import tempfile
import unittest
from pathlib import Path

from egp_vendor_manifest import normalization_pins, pinned_digest_match, verify_excluded_files


def digest(data):
    return hashlib.sha256(data).hexdigest()


class PinnedVendorDigestTests(unittest.TestCase):
    def test_explicit_build_omissions_preserve_pins_and_reject_reintroduction(self):
        with tempfile.TemporaryDirectory() as directory:
            vendor = Path(directory)
            entry = {"upstream_sha256": digest(b"upstream build"), "reason": "native xmake replacement"}
            manifest = {"files": {}, "excluded_upstream_files": {"CMakeLists.txt": entry}}
            verify_excluded_files(vendor, manifest)
            (vendor / "CMakeLists.txt").write_bytes(b"upstream build")
            with self.assertRaises(ValueError):
                verify_excluded_files(vendor, manifest)
            (vendor / "CMakeLists.txt").unlink()
            for name in ("../CMakeLists.txt", "src/vendor.c"):
                with self.assertRaises(ValueError):
                    verify_excluded_files(vendor, {"files": {}, "excluded_upstream_files": {name: entry}})
            with self.assertRaises(ValueError):
                verify_excluded_files(
                    vendor,
                    {
                        "files": {"CMakeLists.txt": entry["upstream_sha256"]},
                        "excluded_upstream_files": {"CMakeLists.txt": entry},
                    },
                )

    def test_exact_bytes_including_binary_are_accepted(self):
        for data in (b"", b"abc", b"a\0b\xff\r\n", b"a\nb\r\n", b"lone\rreturn"):
            with self.subTest(data=data):
                result = pinned_digest_match(data, digest(data))
                self.assertEqual(result["comparison"], "raw")
                self.assertEqual(result["raw_sha256"], digest(data))

    def test_uniform_lf_crlf_checkouts_match_only_pinned_text(self):
        for lf in (b"line\n", b"first\nsecond", "UTF-8: café\n".encode(), b"\xef\xbb\xbfBOM\n"):
            crlf = lf.replace(b"\n", b"\r\n")
            for actual, pinned, label in ((lf, crlf, "CRLF"), (crlf, lf, "LF")):
                with self.subTest(actual=actual, label=label):
                    self.assertIsNone(pinned_digest_match(actual, digest(pinned)))
                    result = pinned_digest_match(actual, digest(pinned), normalized_lf_expected=digest(lf))
                    self.assertEqual(result["comparison"], "LF-pinned")
                    self.assertEqual(result["raw_sha256"], digest(actual))
                    self.assertEqual(result["manifest_sha256"], digest(pinned))

    def test_source_whitespace_and_content_are_still_enforced(self):
        pinned = b"int result = 1;\r\nreturn result;\r\n"
        for altered in (
            b"int result = 2;\nreturn result;\n",
            b"int result=1;\nreturn result;\n",
            b"int result = 1;\nreturn result;",
            b"int result = 1;\n\treturn result;\n",
            b"int result = 1;\nreturn result;\n\n",
        ):
            with self.subTest(altered=altered):
                self.assertIsNone(pinned_digest_match(altered, digest(pinned)))

    def test_binary_invalid_utf8_and_mixed_endings_are_not_normalized(self):
        for actual in (b"a\0b\n", b"a\xffb\n", b"a\nb\r\n", b"a\rb\n"):
            pinned = actual.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
            with self.subTest(actual=actual):
                self.assertNotEqual(actual, pinned)
                self.assertIsNone(pinned_digest_match(actual, digest(pinned)))

    def test_incorrect_digest_is_rejected(self):
        self.assertIsNone(pinned_digest_match(b"text\n", "0" * 64))

    def test_explicit_normalized_pin_accounts_for_patched_mixed_endings(self):
        original = b"original\r\npatch\n"
        normalized = original.replace(b"\r\n", b"\n")
        for actual in (normalized, normalized.replace(b"\n", b"\r\n")):
            result = pinned_digest_match(actual, digest(original), normalized_lf_expected=digest(normalized))
            self.assertEqual(result["comparison"], "LF-pinned")
            self.assertEqual(result["raw_sha256"], digest(actual))
        self.assertIsNone(
            pinned_digest_match(b"original\nchanged\n", digest(original), normalized_lf_expected=digest(normalized))
        )
        self.assertIsNone(pinned_digest_match(normalized, digest(original), normalized_lf_expected="0" * 64))

    def test_normalization_policy_requires_complete_fixed_pins(self):
        manifest = {"files": {"source.c": digest(b"source\r\n")}}
        self.assertEqual(normalization_pins(manifest), {})
        good = {"policy": "utf8_lf", "files": {"source.c": digest(b"source\n")}}
        self.assertEqual(normalization_pins(dict(manifest, checkout_normalization=good)), good["files"])
        for malformed in (
            [],
            {},
            {"policy": "trim", "files": good["files"]},
            {"policy": "utf8_lf", "files": {}},
            {"policy": "utf8_lf", "files": {"other.c": "0" * 64}},
            {"policy": "utf8_lf", "files": {"source.c": "x" * 64}},
            {"policy": "utf8_lf", "files": {"source.c": 1}},
        ):
            with self.subTest(malformed=malformed), self.assertRaises(ValueError):
                normalization_pins(dict(manifest, checkout_normalization=malformed))


if __name__ == "__main__":
    unittest.main()
