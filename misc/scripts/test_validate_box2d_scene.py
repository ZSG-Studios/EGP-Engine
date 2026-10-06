import unittest

from validate_box2d_scene import validate_result


class Box2DDiagnosticGateTests(unittest.TestCase):
    def setUp(self):
        self.expected = "\n".join([
            "RESULT: PASS - invalid inputs",
            *["ERROR: Box2D: Shape cast parameters must not be null."] * 2,
            *["ERROR: Box2D: Rectangle is degenerate or smaller than the configured physics tolerance."] * 4,
        ])

    def test_exact_expected_diagnostics(self):
        self.assertTrue(validate_result(0, self.expected, "invalid_parameters.gd")["passed"])

    def test_missing_diagnostic_is_not_recovery(self):
        self.assertFalse(validate_result(0, self.expected.rsplit("\n", 1)[0], "invalid_parameters.gd")["passed"])

    def test_unexpected_error_is_not_waived(self):
        self.assertFalse(
            validate_result(0, self.expected + "\nERROR: Unrelated failure", "invalid_parameters.gd")["passed"]
        )

    def test_expected_messages_do_not_hide_crash_or_leak(self):
        for marker in [
            "CrashHandlerException",
            "BOX2D ASSERTION",
            "SCRIPT ERROR",
            "RESULT: FAIL",
            "RID allocations of type",
        ]:
            with self.subTest(marker=marker):
                self.assertFalse(validate_result(0, self.expected + "\n" + marker, "invalid_parameters.gd")["passed"])
        self.assertFalse(validate_result(1, self.expected, "invalid_parameters.gd")["passed"])

    def test_other_fixtures_cannot_use_diagnostic_exception(self):
        self.assertTrue(validate_result(0, "RESULT: PASS", "canvas_cast_test.gd")["passed"])
        self.assertFalse(validate_result(0, self.expected, "canvas_cast_test.gd")["passed"])
        self.assertFalse(validate_result(0, "No completion marker", "canvas_cast_test.gd")["passed"])

    def test_wrong_exported_fixture_does_not_satisfy_completion(self):
        self.assertFalse(
            validate_result(
                0,
                "RESULT: PASS - backend activation",
                "canvas_cast_test.gd",
                "RESULT: PASS - native canvas filters and shape casts",
            )["passed"]
        )


if __name__ == "__main__":
    unittest.main()
