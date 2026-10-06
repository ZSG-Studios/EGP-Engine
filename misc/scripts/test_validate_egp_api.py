"""Regression checks for reflection coverage and unsafe API audit exemptions."""

import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from validate_egp_api import snake


class ExposureAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.api = {"header": {"precision": "single"}, "classes": []}
        self.reflection = {"classes": {}}
        self.changes = {"required": {"PhysicsServer3D": {"properties": ["spring/enabled"]}},
                        "property_types": {"PhysicsServer3D": {"spring/enabled": 1}}}
        generated = self.root / "managed/Core/Generated"
        generated.mkdir(parents=True)
        self.headers = self.root / "sdk/gen/include/godot_cpp/classes"
        self.headers.mkdir(parents=True)
        includes = []
        for name in ("PhysicsServer2D", "PhysicsServer3D", "EGPBox3DWorld", "EGPNetSession"):
            self.api["classes"].append({"name": name, "methods": [{"name": "configure", "is_virtual": False}]})
            self.reflection["classes"][name] = {"properties": [], "signals": []}
            path = generated / (name + ".cs")
            path.write_text(f'[GodotClassName("{name}")]\nStringName Configure = "configure";')
            includes.append(f'<Compile Include="Generated/{name}.cs" />')
            (self.headers / (snake(name) + ".hpp")).write_text("void configure();")
        (generated / "GeneratedIncludes.props").write_text("<Project>" + "".join(includes) + "</Project>")
        self.reflection["classes"]["PhysicsServer3D"]["properties"] = [{"name": "spring/enabled", "type": 1}]
        engine = self.root / "engine.exe"
        engine.write_bytes(b"fixture binary identity")
        self.reflection.update(engine=str(engine), engine_sha256=hashlib.sha256(engine.read_bytes()).hexdigest())

    def run_audit(self, reflection=True):
        api_path = self.root / "api.json"
        api_path.write_text(json.dumps(self.api))
        fingerprint = hashlib.sha256(api_path.read_bytes()).hexdigest()
        self.reflection.setdefault("extension_api_sha256", fingerprint)
        (self.root / "sdk/sdk.json").write_text(json.dumps({"api_sha256": fingerprint, "precision": "single"}))
        (self.root / "classdb.json").write_text(json.dumps(self.reflection))
        (self.root / "changes.json").write_text(json.dumps(self.changes))
        command = [sys.executable, str(Path(__file__).with_name("validate_egp_api.py")),
                   "--api", str(api_path), "--sdk", str(self.root / "sdk"),
                   "--managed", str(self.root / "managed"), "--changes", str(self.root / "changes.json"),
                   "--output", str(self.root / "receipt.json")]
        if reflection:
            command += ["--classdb", str(self.root / "classdb.json")]
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        return result.returncode, json.loads((self.root / "receipt.json").read_text())

    def test_inspector_path_is_recorded_with_actual_type(self):
        code, receipt = self.run_audit()
        self.assertEqual(code, 0, receipt["failures"])
        self.assertEqual(receipt["inspector_properties"], [{"class": "PhysicsServer3D", "property": "spring/enabled", "type": 1}])

    def test_missing_snapshot_cannot_satisfy_properties(self):
        code, receipt = self.run_audit(False)
        self.assertNotEqual(code, 0)
        self.assertIn("Property acceptance requires --classdb from the actual editor", receipt["failures"])

    def test_wrong_boolean_type_is_rejected(self):
        self.reflection["classes"]["PhysicsServer3D"]["properties"][0]["type"] = 3
        code, receipt = self.run_audit()
        self.assertNotEqual(code, 0)
        self.assertTrue(any("Property type check failed" in failure for failure in receipt["failures"]))

    def test_partial_snapshot_is_rejected(self):
        del self.reflection["classes"]["EGPNetSession"]
        code, receipt = self.run_audit()
        self.assertNotEqual(code, 0)
        self.assertIn("ClassDB snapshot omits audited class: EGPNetSession", receipt["failures"])

    def test_stale_api_and_engine_identity_are_rejected(self):
        self.reflection["extension_api_sha256"] = "wrong"
        self.reflection["engine_sha256"] = "wrong"
        code, receipt = self.run_audit()
        self.assertNotEqual(code, 0)
        self.assertIn("ClassDB snapshot does not record the same actual API hash", receipt["failures"])
        self.assertIn("ClassDB snapshot engine is missing or has changed", receipt["failures"])

    def test_ordinary_method_cannot_be_waived_as_native_only(self):
        self.changes["native_only_hooks"] = {"PhysicsServer3D": ["configure"]}
        code, receipt = self.run_audit()
        self.assertNotEqual(code, 0)
        self.assertIn("Invalid native-only pointer hook declaration: PhysicsServer3D.configure", receipt["failures"])


if __name__ == "__main__":
    unittest.main()
