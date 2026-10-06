"""Keep legacy lookup checks strict outside explicitly retired EGP APIs."""

import pathlib
import tempfile
import unittest
from unittest.mock import patch

import run_compatibility_test as compatibility


class ForkCompatibilityTests(unittest.TestCase):
    def test_removed_classes_and_methods(self):
        self.assertTrue(compatibility.is_retired_api("classes/ENetConnection/methods/create_host"))
        self.assertTrue(compatibility.is_retired_api("classes/Node/methods/rpc_id"))
        self.assertTrue(compatibility.is_retired_api("builtin_classes/Callable/methods/rpc"))

    def test_retained_and_unrecognized_apis_remain_checked(self):
        for path in (
            "classes/Node/methods/call_deferred",
            "classes/Node/methods/rpc_id_new",
            "classes/ENetConnectionNew/methods/create_host",
            "classes/TextServerFallback/methods/get_name",
            "classes/PhysicsServer3D/methods/body_set_state",
            "classes/PhysicsServer3DExtension/methods/_body_test_motion",
            "builtin_classes/Callable/methods/call",
            "builtin_classes/Vector2/methods/length",
            "utility_functions/sin",
        ):
            with self.subTest(path=path):
                self.assertFalse(compatibility.is_retired_api(path))

    def test_lookup_generation_keeps_remaining_hashes(self):
        reference = {
            "classes": [
                {"name": "ENetConnection", "methods": [{"name": "create_host", "hash": 10}]},
                {"name": "Node", "methods": [{"name": "rpc_id", "hash": 20}, {"name": "call_deferred", "hash": 30}]},
                {"name": "FutureClass", "methods": [{"name": "future_method", "hash": 40}]},
            ],
            "builtin_classes": [
                {"name": "Callable", "methods": [{"name": "rpc", "hash": 50}, {"name": "call", "hash": 60}]}
            ],
            "global_enums": [{"name": "Variant.Type", "values": [{"name": "TYPE_CALLABLE", "value": 25}]}],
            "utility_functions": [{"name": "sin", "hash": 70}],
        }
        with tempfile.TemporaryDirectory() as directory:
            folder = pathlib.Path(directory)
            classes = folder / "classes.txt"
            builtins = folder / "builtins.txt"
            utilities = folder / "utilities.txt"
            with (
                patch.object(compatibility, "download_gdextension_api", return_value=reference),
                patch.object(compatibility, "CLASS_METHODS_FILE", classes),
                patch.object(compatibility, "BUILTIN_METHODS_FILE", builtins),
                patch.object(compatibility, "UTILITY_FUNCTIONS_FILE", utilities),
            ):
                compatibility.generate_test_data_files("fixture")
            self.assertEqual(classes.read_text(), "Node call_deferred 30\nFutureClass future_method 40\n")
            self.assertEqual(builtins.read_text(), "25 call 60\n")
            self.assertEqual(utilities.read_text(), "sin 70\n")


if __name__ == "__main__":
    unittest.main()
