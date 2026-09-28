#!/usr/bin/env python3
"""Offline CHART-01 source audit. This does NOT mint a VerifiedSeries."""
import csv
from decimal import Decimal, InvalidOperation
import hashlib
import io
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / "tests/fixtures/chart01"
DATA = "science/data/processed/nasa_gistemp_global_2026.csv"
PROVENANCE = "science/data/processed/nasa_gistemp_global_2026.provenance.json"
MANIFEST = "science/data/input_manifest.json"
REGISTRY = "science/data/registry.csv"
SNAPSHOT = "d9e249339663015f6d1c05752338a955bf64ad0b"


def require(condition, reason):
    if not condition:
        raise ValueError(reason)


def parse_points(text):
    """Source-specific annual, exact-decimal adapter; no sorting/interpolation."""
    rows = csv.DictReader(io.StringIO(text))
    require(rows.fieldnames == ["year", "temperature_anomaly_c_1951_1980"],
            "SOURCE_COLUMNS")
    points = []
    previous = None
    for row in rows:
        require(None not in row and all(v is not None for v in row.values()),
                "ROW_SHAPE")
        year = row["year"]
        require(re.fullmatch(r"[0-9]{4}", year) is not None, "YEAR_ENCODING")
        year = int(year)
        require(previous is None or year == previous + 1, "YEAR_ORDER_OR_GAP")
        token = row["temperature_anomaly_c_1951_1980"]
        require(re.fullmatch(r"-?(?:0|[1-9][0-9]*)\.[0-9]{4}", token) is not None,
                "NUMERIC_ENCODING")
        try:
            value = Decimal(token)
        except InvalidOperation as error:
            raise ValueError("NUMERIC_ENCODING") from error
        require(value.is_finite(), "NONFINITE")
        # Preserve source decimal spelling; never pass via binary floating point.
        points.append({"year": year, "value_decimal": token})
        previous = year
    require(len(points) > 0, "EMPTY_SERIES")
    return points


def audit(fixture=FIXTURE):
    lock = json.loads((fixture / "source-lock.json").read_text())
    require(lock["repository"] == "LaurentiuStaicu/empirical-world3-dynamics"
            and lock["snapshot_sha"] == SNAPSHOT, "SNAPSHOT_IDENTITY")
    require(set(lock["files"]) == {DATA, PROVENANCE, MANIFEST, REGISTRY},
            "SOURCE_SET")
    source = fixture / "ewd"
    contents = {}
    for path, digest in lock["files"].items():
        blob = (source / path).read_bytes()
        require(len(blob) <= 65536, "SOURCE_SIZE")
        require(hashlib.sha256(blob).hexdigest() == digest, "SOURCE_DIGEST:" + path)
        contents[path] = blob.decode("utf-8")
    manifest = json.loads(contents[MANIFEST])
    for path in (DATA, PROVENANCE):
        require(manifest["files"][path] == lock["files"][path],
                "UPSTREAM_MANIFEST:" + path)
    provenance = json.loads(contents[PROVENANCE])
    require(provenance["coverage"] == {"start_year": 1880, "end_year": 2025},
            "COVERAGE")
    require(provenance["unit"] == "degrees Celsius relative to the 1951-1980 mean",
            "UNIT_OR_BASELINE")
    require(provenance["selection"] == "Global annual J-D Land-Ocean Temperature Index",
            "AGGREGATION")
    entries = [r for r in csv.DictReader(io.StringIO(contents[REGISTRY]))
               if r["series_id"] == "nasa_gistemp_global"]
    require(len(entries) == 1, "REGISTRY_IDENTITY")
    entry = entries[0]
    require(entry["observation_type"] == "empirical" and entry["frequency"] == "annual"
            and entry["unit"] == "degrees Celsius relative to 1951-1980",
            "REGISTRY_SEMANTICS")
    require("not interchangeable with World3 persistent pollution" in entry["notes"],
            "SCIENTIFIC_BOUNDARY")
    points = parse_points(contents[DATA])
    require(len(points) == 146 and points[0]["year"] == 1880
            and points[-1]["year"] == 2025, "POINT_COVERAGE")
    return {
        "schema": "atm-chart01-source-audit/1",
        "status": "PINNED_PROCESSED_SOURCE_AUDITED",
        "repository": lock["repository"], "snapshot_sha": SNAPSHOT,
        "source_sha256": lock["files"],
        "series_id": entry["series_id"],
        "subject": "global_surface_temperature",
        "attribute": "annual_temperature_anomaly",
        "x_semantics": "calendar_year", "frequency": "annual",
        "y_unit": "degree_Celsius_anomaly", "reference_period": "1951-1980",
        "epistemic_status": "empirical", "scenario": "not_applicable",
        "point_count": len(points), "start_year": 1880, "end_year": 2025,
        "missing_count": 0, "gap_count": 0,
        "numeric_encoding": "source_decimal_text_4_places",
        "scientific_boundary": entry["notes"],
        "raw_reproduction_verified": False,
        "native_verified_series_available": False,
        "runtime_adapter_enabled": False,
        "limitations": [
            "Checks pinned processed data and metadata, not raw NASA reconstruction.",
            "No current AtM semantic profile admits this dataset as a numeric series.",
            "This report is not a VerifiedSeries or authority to render a chart."
        ]
    }


if __name__ == "__main__":
    try:
        result = audit()
        if "--check" in sys.argv:
            retained = json.loads((ROOT / "qualification/chart01-source-audit-v1.json").read_text())
            require(result == retained, "RETAINED_AUDIT_MISMATCH")
            print("CHART-01 source audit passed: 146 pinned annual decimal observations; runtime remains disabled")
        else:
            print(json.dumps(result, indent=2, ensure_ascii=False))
    except (ValueError, KeyError, OSError) as error:
        raise SystemExit(f"CHART-01 source audit failed: {error}")
