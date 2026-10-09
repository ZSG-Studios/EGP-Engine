"""Reject missing, malformed, or contradictory inherited-reference evidence."""

import copy
import unittest

from validate_egp_scene_node_refs import evidence_failure


class SceneReferencesEvidenceTests(unittest.TestCase):
    def rows(self, legacy=False):
        rows = []
        for name in ("plain", "inherited", "layered", "cs_plain", "cs_inherited", "cs_layered"):
            success = not legacy or not ("inherited" in name or "layered" in name)
            rows.append(
                dict(
                    case=name,
                    single=success,
                    items=success,
                    values=success,
                    keys=success,
                    existing=True,
                    typed=success if name.startswith("cs_") else True,
                )
            )
        return rows

    def test_control_is_distinct_from_qualification(self):
        self.assertIsNone(evidence_failure(self.rows()))
        self.assertIsNone(evidence_failure(self.rows(True), True))
        self.assertIsNotNone(evidence_failure(self.rows(True)))
        self.assertIsNotNone(evidence_failure(self.rows(), True))

    def test_complete_ordered_evidence_required(self):
        for rows in (None, {}, [], [None], self.rows()[:-1], list(reversed(self.rows())), self.rows() * 2):
            with self.subTest(rows=rows):
                self.assertIsNotNone(evidence_failure(rows))

    def test_each_flag_must_be_literal_expected_boolean(self):
        for legacy in (False, True):
            rows = self.rows(legacy)
            for index in range(len(rows)):
                for key in ("single", "items", "values", "keys", "existing", "typed"):
                    for invalid in (not rows[index][key], None, 0, 1, "true", [], {}):
                        mutated = copy.deepcopy(rows)
                        mutated[index][key] = invalid
                        with self.subTest(legacy=legacy, case=index, key=key, invalid=invalid):
                            self.assertIsNotNone(evidence_failure(mutated, legacy))
                    missing = copy.deepcopy(rows)
                    del missing[index][key]
                    self.assertIsNotNone(evidence_failure(missing, legacy))


if __name__ == "__main__":
    unittest.main()
