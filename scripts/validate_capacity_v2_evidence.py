#!/usr/bin/env python3

import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "benchmarks" / "capacity-v2" / "evidence.json"
CORPUS_PATH = ROOT / "benchmarks" / "capacity-v2" / "corpus.json"

SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise SystemExit(
        f"capacity-v2 evidence validation failed: {message}"
    )


def load(path: pathlib.Path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def require_sha(value, context):
    if not isinstance(value, str) or SHA40.fullmatch(value) is None:
        fail(f"{context} must be lowercase 40-hex")


def require_digest(value, context):
    if not isinstance(value, str) or SHA256.fullmatch(value) is None:
        fail(f"{context} must be lowercase 64-hex")


def require_positive_int(value, context):
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        fail(f"{context} must be a positive integer")


def validate_loo(model, name):
    loo = model.get("leave_one_out")
    if not isinstance(loo, dict) or set(loo) != {"cbd", "ewd", "rmd"}:
        fail(f"{name}.leave_one_out repository set drifted")

    for rid, record in loo.items():
        if not isinstance(record, dict):
            fail(f"{name}.{rid} must be an object")
        if record.get("covered") != 6 or record.get("cases") != 6:
            fail(
                f"{name}.{rid} must preserve 6/6 reviewed coverage"
            )
        ratio = record.get("worst_actual_over_prediction")
        if not isinstance(ratio, (int, float)) or isinstance(ratio, bool):
            fail(f"{name}.{rid} ratio is invalid")
        if ratio <= 0 or ratio > 1:
            fail(
                f"{name}.{rid} reviewed hybrid coverage was exceeded"
            )


def main() -> int:
    evidence = load(EVIDENCE_PATH)
    corpus = load(CORPUS_PATH)

    if evidence.get("schema_version") != 1:
        fail("schema_version must remain 1")
    if evidence.get("status") != "qualification-only":
        fail("status must remain qualification-only")
    if evidence.get("production_prediction_selected") is not False:
        fail("production predictor must not be selected")
    if evidence.get("production_margin_selected") is not False:
        fail("production margin must not be selected")

    source = evidence.get("source")
    if not isinstance(source, dict):
        fail("source provenance is missing")

    require_positive_int(
        source.get("actions_run_id"),
        "source.actions_run_id",
    )
    require_positive_int(
        source.get("artifact_id"),
        "source.artifact_id",
    )
    require_digest(
        source.get("artifact_sha256"),
        "source.artifact_sha256",
    )
    require_sha(
        source.get("atm_source_commit"),
        "source.atm_source_commit",
    )
    require_sha(
        source.get("workflow_commit"),
        "source.workflow_commit",
    )

    corpus_summary = evidence.get("corpus")
    if not isinstance(corpus_summary, dict):
        fail("corpus summary is missing")
    if corpus_summary.get("repository_count") != 3:
        fail("repository_count must remain 3")
    if corpus_summary.get("samples_per_repository") != 6:
        fail("samples_per_repository must remain 6")
    if corpus_summary.get("total_samples") != 18:
        fail("total_samples must remain 18")
    if corpus_summary.get("path") != (
        "benchmarks/capacity-v2/corpus.json"
    ):
        fail("corpus path drifted")

    actual_total = sum(
        len(repository["samples"])
        for repository in corpus["repositories"]
    )
    if actual_total != 18:
        fail("evidence total no longer matches corpus")

    models = evidence.get("candidate_models")
    if not isinstance(models, dict):
        fail("candidate_models is missing")

    expected_models = {
        "snapshot_hybrid_with_historical_floor",
        "index_hybrid_with_historical_floor",
    }
    if set(models) != expected_models:
        fail("candidate model set drifted")

    for name in sorted(expected_models):
        model = models[name]
        if not isinstance(model, dict):
            fail(f"{name} must be an object")
        definition = model.get("definition")
        if not isinstance(definition, str) or not definition.startswith("max("):
            fail(f"{name} definition is missing")
        validate_loo(model, name)

    ewd_index = models[
        "index_hybrid_with_historical_floor"
    ]["leave_one_out"]["ewd"]
    if ewd_index["worst_actual_over_prediction"] != 1.0:
        fail(
            "EWD index limiting case must remain recorded as exact 1.0 "
            "coverage; hiding it would manufacture a safety margin"
        )

    sensitivity = evidence.get("single_model_sensitivity")
    if not isinstance(sensitivity, dict) or set(sensitivity) != {
        "cbd", "ewd", "rmd"
    }:
        fail("single-model sensitivity repository set drifted")

    underprediction_seen = False
    for rid, models_for_repo in sensitivity.items():
        if not isinstance(models_for_repo, dict):
            fail(f"single_model_sensitivity.{rid} must be an object")
        for model_name, record in models_for_repo.items():
            if record.get("covered") != 5 or record.get("cases") != 6:
                fail(
                    f"{rid}/{model_name} must preserve the reviewed 5/6 "
                    "single-model result"
                )
            ratio = record.get("worst_actual_over_prediction")
            if not isinstance(ratio, (int, float)) or ratio <= 1:
                fail(
                    f"{rid}/{model_name} must preserve observed "
                    "underprediction sensitivity"
                )
            underprediction_seen = True

    if not underprediction_seen:
        fail("single-model underprediction sensitivity disappeared")

    limits = evidence.get("reviewed_limits")
    if not isinstance(limits, list) or len(limits) < 5:
        fail("reviewed_limits are incomplete")

    joined = "\n".join(str(item) for item in limits).lower()
    for phrase in (
        "18/18",
        "exactly 1.0",
        "can become stale",
        "does not prove an upper bound",
        "no production predictor",
    ):
        if phrase not in joined:
            fail(f"reviewed limits lost required phrase: {phrase}")

    print(
        "capacity-v2 evidence validation passed: "
        "hybrid 18/18 preserved without selecting production policy"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
