#!/usr/bin/env python3

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATH = ROOT / "benchmarks" / "capacity-v3" / "evidence.json"

SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise SystemExit(
        f"capacity-v3 evidence validation failed: {message}"
    )


def load():
    with PATH.open(encoding="utf-8") as handle:
        return json.load(handle)


def require_ratio(value, context):
    if not isinstance(value, (int, float)) or isinstance(value, bool):
        fail(f"{context} must be numeric")
    if value <= 0 or value > 1:
        fail(f"{context} must remain in (0, 1]")


def main() -> int:
    evidence = load()

    if evidence.get("schema_version") != 1:
        fail("schema_version must remain 1")
    if evidence.get("status") != "qualification-only":
        fail("status must remain qualification-only")
    if evidence.get("production_prediction_selected") is not False:
        fail("production predictor must remain unselected")
    if evidence.get("production_margin_selected") is not False:
        fail("production margin must remain unselected")
    if evidence.get("training_refit_on_holdout") is not False:
        fail("holdout refit must remain forbidden")

    source = evidence.get("source")
    if not isinstance(source, dict):
        fail("source object is missing")

    for key in ("atm_source_commit", "workflow_commit"):
        value = source.get(key)
        if not isinstance(value, str) or SHA40.fullmatch(value) is None:
            fail(f"source.{key} is not a lowercase 40-hex SHA")

    digest = source.get("artifact_sha256")
    if not isinstance(digest, str) or SHA256.fullmatch(digest) is None:
        fail("source.artifact_sha256 is invalid")

    training = evidence.get("training")
    holdout = evidence.get("holdout")

    if training != {
        "corpus": "benchmarks/capacity-v2/corpus.json",
        "samples": 18,
        "per_repository": 6,
    }:
        fail("training identity drifted")

    if holdout != {
        "corpus": "benchmarks/capacity-v3/corpus.json",
        "samples": 12,
        "per_repository": 4,
        "ranks": [4, 12, 20, 40],
    }:
        fail("holdout identity drifted")

    summary = evidence.get("summary")
    if not isinstance(summary, dict):
        fail("summary is missing")

    for rid in ("cbd", "ewd", "rmd"):
        record = summary.get(rid)
        if not isinstance(record, dict):
            fail(f"{rid} summary missing")

        for artifact in ("snapshot", "index"):
            item = record.get(artifact)
            if not isinstance(item, dict):
                fail(f"{rid}.{artifact} missing")
            if item.get("covered") != 4 or item.get("cases") != 4:
                fail(
                    f"{rid}.{artifact} must preserve 4/4 holdout coverage"
                )
            require_ratio(
                item.get("worst_actual_over_prediction"),
                f"{rid}.{artifact}.worst_actual_over_prediction",
            )

    global_summary = evidence.get("global_summary")
    if not isinstance(global_summary, dict):
        fail("global_summary is missing")

    expected_counts = {
        "snapshot_covered": 12,
        "snapshot_cases": 12,
        "index_covered": 12,
        "index_cases": 12,
    }
    for key, expected in expected_counts.items():
        if global_summary.get(key) != expected:
            fail(f"global_summary.{key} must remain {expected}")

    if global_summary.get("limiting_repository") != "ewd":
        fail("limiting repository must remain EWD")
    if global_summary.get("limiting_artifact") != "snapshot":
        fail("limiting artifact must remain snapshot")

    limiting = global_summary.get(
        "limiting_actual_over_prediction"
    )
    require_ratio(
        limiting,
        "global_summary.limiting_actual_over_prediction",
    )

    if not (0.99 < limiting < 1.0):
        fail(
            "limiting ratio must preserve the reviewed small positive slack"
        )

    rules = evidence.get("reviewed_limits")
    if not isinstance(rules, list) or len(rules) < 5:
        fail("reviewed_limits are incomplete")

    joined = "\n".join(str(item) for item in rules).lower()
    for phrase in (
        "not used to fit or refit",
        "does not prove an upper bound",
        "no production predictor",
    ):
        if phrase not in joined:
            fail(f"reviewed limit lost: {phrase}")

    print(
        "capacity-v3 evidence validation passed: "
        "12/12 snapshot + 12/12 index no-refit holdout coverage, "
        "no production policy selected"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
