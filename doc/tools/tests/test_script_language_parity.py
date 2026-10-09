"""Check language parity without confusing shared data or shaders with scripts."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

GENERATOR = Path(__file__).resolve().parents[1] / "make_rst.py"


class ScriptLanguageParityTests(unittest.TestCase):
    def generate(self, sample):
        with tempfile.TemporaryDirectory(prefix="egp-doc-parity-") as directory:
            path = Path(directory)
            source = path / "Object.xml"
            source.write_text(
                '<class name="Object"><brief_description>Example.</brief_description>'
                f'<description>{sample}</description></class>',
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, str(GENERATOR), "--verbose", "-o", str(path), str(source)],
                capture_output=True,
                text=True,
                check=True,
            )
            return result.stdout, (path / "class_object.rst").read_text(encoding="utf-8")

    def test_shared_output_has_no_script_parity_notice(self):
        output, rendered = self.generate('[codeblock lang=text]\nRoot\nChild\n[/codeblock]')
        self.assertNotIn("failed parity check", output)
        self.assertIn(".. code:: text", rendered)

    def test_shader_keeps_its_language_and_does_not_require_csharp(self):
        output, rendered = self.generate('[codeblock lang=glsl]\nvec3 result = vec3(1.0);\n[/codeblock]')
        self.assertNotIn("failed parity check", output)
        self.assertIn(".. code:: glsl", rendered)
        self.assertIn("vec3 result", rendered)

    def test_missing_script_translation_is_still_reported(self):
        output, _ = self.generate('[codeblocks]\n[gdscript]\nprint("hello")\n[/gdscript]\n[/codeblocks]')
        self.assertIn("1 code samples failed parity check", output)
        self.assertIn("Only one script language sample", output)

    def test_bare_script_is_still_reported(self):
        output, _ = self.generate('[codeblock]\nprint("hello")\n[/codeblock]')
        self.assertIn("1 code samples failed parity check", output)

    def test_explicit_script_language_does_not_bypass_parity(self):
        for language in ("gdscript", "csharp", "unknown"):
            with self.subTest(language=language):
                output, _ = self.generate(f'[codeblock lang={language}]\nexample()\n[/codeblock]')
                self.assertIn("1 code samples failed parity check", output)

    def test_paired_scripts_render_both_language_tabs(self):
        output, rendered = self.generate(
            '[codeblocks]\n[gdscript]\nprint("hello")\n[/gdscript]\n'
            '[csharp]\nGD.Print("hello");\n[/csharp]\n[/codeblocks]'
        )
        self.assertNotIn("failed parity check", output)
        self.assertIn(".. code-tab:: gdscript", rendered)
        self.assertIn(".. code-tab:: csharp", rendered)


if __name__ == "__main__":
    unittest.main()
