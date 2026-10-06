"""Regression checks for reflection coverage and unsafe API audit exemptions."""

import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from capture_egp_api import strip_documentation
from validate_egp_api import audit_enum_docs, audit_method_docs, snake


class ExposureAuditTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.api = {"header": {"precision": "single"}, "classes": []}
        self.reflection = {"classes": {}}
        self.changes = {
            "required": {"PhysicsServer3D": {"properties": ["spring/enabled"]}},
            "property_types": {"PhysicsServer3D": {"spring/enabled": 1}},
        }
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
        command = [
            sys.executable,
            str(Path(__file__).with_name("validate_egp_api.py")),
            "--api",
            str(api_path),
            "--sdk",
            str(self.root / "sdk"),
            "--managed",
            str(self.root / "managed"),
            "--changes",
            str(self.root / "changes.json"),
            "--output",
            str(self.root / "receipt.json"),
        ]
        if reflection:
            command += ["--classdb", str(self.root / "classdb.json")]
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        return result.returncode, json.loads((self.root / "receipt.json").read_text())

    def test_inspector_path_is_recorded_with_actual_type(self):
        code, receipt = self.run_audit()
        self.assertEqual(code, 0, receipt["failures"])
        self.assertEqual(
            receipt["inspector_properties"], [{"class": "PhysicsServer3D", "property": "spring/enabled", "type": 1}]
        )

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

    def test_only_named_internal_signals_are_classified(self):
        self.reflection["classes"]["PhysicsServer3D"]["signals"] = [{"name": "_debug_changed"}, {"name": "_unexpected"}]
        code, receipt = self.run_audit()
        self.assertNotEqual(code, 0)
        self.assertEqual(receipt["internal_signals"], [{"class": "PhysicsServer3D", "signal": "_debug_changed"}])
        self.assertIn("C# omits PhysicsServer3D signal _unexpected", receipt["failures"])

    def test_variant_property_cannot_be_filtered_out_as_group(self):
        self.reflection["classes"]["PhysicsServer3D"]["properties"].extend([
            {"name": "data", "type": 0, "usage": 1 << 17},
            {"name": "Inspector category", "type": 0, "usage": 1 << 7},
        ])
        code, receipt = self.run_audit()
        self.assertNotEqual(code, 0)
        self.assertIn("C# omits PhysicsServer3D property data", receipt["failures"])
        self.assertFalse(any("Inspector category" in failure for failure in receipt["failures"]))


class EnumDocumentationTests(unittest.TestCase):
    def test_enum_values_identity_and_removals_are_checked(self):
        row = {
            "name": "PhysicsServer3D",
            "enums": [{"name": "JointType", "values": [{"name": "JOINT_TYPE_MAX", "value": 14}]}],
        }
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "PhysicsServer3D.xml"
            for attributes, expected in (
                ('name="JOINT_TYPE_MAX" value="5" enum="JointType"', "Incorrect documented enum"),
                ('name="JOINT_TYPE_MAX" value="14" enum="OtherType"', "Incorrect documented enum"),
                ('name="RETIRED" value="14" enum="JointType"', "Retired enum constant"),
                ('name="JOINT_TYPE_MAX" value="14" enum="JointType"', None),
            ):
                with self.subTest(attributes=attributes):
                    path.write_text(
                        f"<class><constants><constant {attributes}>Enum sentinel.</constant></constants></class>"
                    )
                    failures, count = audit_enum_docs(row, path)
                    self.assertEqual(count, 1)
                    if expected:
                        self.assertTrue(any(expected in failure for failure in failures), failures)
                    else:
                        self.assertEqual(failures, [])

    def test_missing_and_empty_enum_documentation_are_rejected(self):
        row = {
            "name": "PhysicsServer2D",
            "enums": [{"name": "JointType", "values": [{"name": "JOINT_TYPE_MAX", "value": 10}]}],
        }
        failures, _ = audit_enum_docs(row, None)
        self.assertIn("Missing enum documentation: PhysicsServer2D", failures)
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "PhysicsServer2D.xml"
            path.write_text(
                '<class><constants><constant name="JOINT_TYPE_MAX" value="10" enum="JointType" /></constants></class>'
            )
            failures, _ = audit_enum_docs(row, path)
            self.assertIn("Empty enum documentation: PhysicsServer2D.JOINT_TYPE_MAX", failures)
            path.write_text("<class><constants /></class>")
            failures, _ = audit_enum_docs(row, path)
            self.assertIn("Undocumented enum constant: PhysicsServer2D.JOINT_TYPE_MAX", failures)


class MethodDocumentationTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.path = Path(self.temporary.name) / "PhysicsServer3D.xml"
        self.row = {
            "name": "PhysicsServer3D",
            "methods": [
                {
                    "name": "_configure",
                    "is_virtual": True,
                    "is_required": True,
                    "is_const": True,
                    "return_value": {"type": "enum::Error"},
                    "arguments": [
                        {"name": "options", "type": "Dictionary", "default_value": "{}"},
                        {"name": "exclude", "type": "typedarray::RID", "default_value": "Array[RID]([])"},
                    ],
                }
            ],
        }
        self.xml = (
            '<class><methods><method name="_configure" qualifiers="virtual required const">'
            '<return type="int" enum="Error" />'
            '<param index="0" name="options" type="Dictionary" default="{}" />'
            '<param index="1" name="exclude" type="RID[]" default="Array[RID]([])" />'
            "<description>Configures the backend.</description></method></methods></class>"
        )

    def audit(self, xml=None, row=None, kind="method"):
        self.path.write_text(xml or self.xml)
        return audit_method_docs(row or self.row, self.path, kind)

    def test_exact_virtual_signature_and_typed_default_pass(self):
        self.assertEqual(self.audit(), ([], 1))

    def test_compiled_help_pairing_keeps_defaults_and_required_flags(self):
        compiled = copy.deepcopy(self.row)
        compiled["brief_description"] = "Server."
        compiled["methods"][0]["description"] = "Configures the backend."
        self.assertEqual(strip_documentation(compiled), self.row)
        compiled["methods"][0]["is_required"] = False
        self.assertNotEqual(strip_documentation(compiled), self.row)
        compiled["methods"][0]["is_required"] = True
        compiled["methods"][0]["arguments"][1]["default_value"] = "[]"
        self.assertNotEqual(strip_documentation(compiled), self.row)

    def test_each_signature_dimension_is_checked(self):
        for old, new, expected in (
            ('enum="Error"', 'enum="OtherError"', "return type"),
            ('name="options"', 'name="arguments"', "argument"),
            ('type="Dictionary"', 'type="Array"', "argument"),
            ('index="1"', 'index="0"', "argument"),
            ('default="Array[RID]([])"', 'default="[]"', "argument"),
            ('default="{}"', "", "argument"),
            ("virtual required const", "virtual const", "required"),
            ("virtual required const", "required const", "virtual"),
            ("virtual required const", "virtual required static", "const"),
        ):
            with self.subTest(replacement=new):
                failures, _ = self.audit(self.xml.replace(old, new))
                self.assertTrue(any(expected in failure for failure in failures), failures)

    def test_missing_empty_duplicate_and_retired_methods_are_rejected(self):
        failures, _ = audit_method_docs(self.row, None)
        self.assertIn("Missing method documentation: PhysicsServer3D", failures)
        for xml, expected in (
            ("<class><methods /></class>", "Undocumented method"),
            (self.xml.replace("Configures the backend.", ""), "Empty method documentation"),
            (self.xml.replace('name="_configure"', 'name="retired"'), "Retired method remains documented"),
            (
                self.xml.replace("</methods>", self.xml.split("<methods>")[1].split("</methods>")[0] + "</methods>"),
                "Duplicate method documentation",
            ),
            (
                self.xml.replace('<param index="1" name="exclude" type="RID[]" default="Array[RID]([])" />', ""),
                "argument count",
            ),
        ):
            with self.subTest(expected=expected):
                failures, _ = self.audit(xml)
                self.assertTrue(any(expected in failure for failure in failures), failures)

    def test_property_accessors_can_use_member_documentation(self):
        row = copy.deepcopy(self.row)
        row["properties"] = [{"name": "options", "setter": "_configure"}]
        self.assertEqual(self.audit("<class><members /></class>", row), ([], 0))
        # Explicit method documentation still must have the correct signature.
        self.assertTrue(self.audit(self.xml.replace('enum="Error"', 'enum="Other"'), row)[0])

    def test_signals_check_arguments_and_only_named_internal_signals_are_excluded(self):
        row = {
            "name": "PhysicsServer3D",
            "signals": [
                {"name": "_debug_changed"},
                {"name": "hit", "arguments": [{"name": "force", "type": "Vector3"}]},
            ],
        }
        xml = (
            '<class><signals><signal name="hit"><param index="0" name="force" type="Vector3" />'
            "<description>Reports a hit force.</description></signal></signals></class>"
        )
        self.assertEqual(self.audit(xml, row, "signal"), ([], 1))
        failures, _ = self.audit(xml.replace('type="Vector3"', 'type="float"'), row, "signal")
        self.assertTrue(any("Incorrect documented argument" in failure for failure in failures), failures)
        row["signals"].append({"name": "_unexpected"})
        failures, _ = self.audit(xml, row, "signal")
        self.assertIn("Undocumented signal: PhysicsServer3D._unexpected", failures)


if __name__ == "__main__":
    unittest.main()
