#!/usr/bin/env python3
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from validate_chart01_source import audit, parse_points, FIXTURE, DATA

HEADER = "year,temperature_anomaly_c_1951_1980\n"


class SourceAuditTest(unittest.TestCase):
    def test_retained_source_is_not_runtime_authority(self):
        result = audit()
        self.assertEqual(result["point_count"], 146)
        self.assertFalse(result["native_verified_series_available"])
        self.assertFalse(result["runtime_adapter_enabled"])
        self.assertFalse(result["raw_reproduction_verified"])

    def test_decimal_values_preserved_without_float_round_trip(self):
        points = parse_points(HEADER + "2000,-0.0000\n2001,0.1234\n")
        self.assertEqual(points[0]["value_decimal"], "-0.0000")
        self.assertEqual(points[1]["value_decimal"], "0.1234")

    def test_invalid_order_and_gaps_rejected(self):
        for years in [(2000, 2000), (2001, 2000), (2000, 2002)]:
            with self.subTest(years=years), self.assertRaisesRegex(ValueError, "YEAR_ORDER_OR_GAP"):
                parse_points(HEADER + f"{years[0]},1.0000\n{years[1]},2.0000\n")

    def test_missing_nonfinite_and_altered_encoding_rejected(self):
        for value in ["", "NaN", "Inf", "-Infinity", "null", "1e2", "1.0", " 1.0000"]:
            with self.subTest(value=value), self.assertRaisesRegex(ValueError, "NUMERIC_ENCODING"):
                parse_points(HEADER + "2000," + value + "\n")

    def test_empty_extra_column_and_wrong_unit_header_rejected(self):
        for text in [HEADER, HEADER + "2000,1.0000,extra\n",
                     "year,persistent_pollution\n2000,1.0000\n"]:
            with self.subTest(text=text), self.assertRaises(ValueError):
                parse_points(text)

    def test_source_tampering_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "fixture"
            shutil.copytree(FIXTURE, root)
            path = root / "ewd" / DATA
            path.write_text(path.read_text().replace("-0.1700", "-0.1800", 1))
            with self.assertRaisesRegex(ValueError, "SOURCE_DIGEST"):
                audit(root)

    def test_snapshot_substitution_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "fixture"
            shutil.copytree(FIXTURE, root)
            path = root / "source-lock.json"
            lock = json.loads(path.read_text())
            lock["snapshot_sha"] = "0" * 40
            path.write_text(json.dumps(lock))
            with self.assertRaisesRegex(ValueError, "SNAPSHOT_IDENTITY"):
                audit(root)


if __name__ == "__main__":
    unittest.main()
