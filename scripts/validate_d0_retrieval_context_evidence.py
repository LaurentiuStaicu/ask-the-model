#!/usr/bin/env python3

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "qualification" / "d0-retrieval-context-rebaseline-v1.json"


def fail(message: str) -> None:
    raise SystemExit(f"D0 retrieval/context evidence validation failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def main() -> int:
    evidence = json.loads(EVIDENCE.read_text(encoding="utf-8"))

    require(evidence.get("schema_version") == 1, "schema version drifted")
    require(
        evidence.get("evidence_id") == "atm-d0-retrieval-context-rebaseline-v1",
        "evidence id drifted",
    )
    require(evidence.get("status") == "measurement-complete", "status drifted")
    require(
        evidence.get("production_policy_changed") is False,
        "D0 must not change production policy",
    )
    require(
        evidence.get("d1_evaluation_authorized") is True,
        "D1 evaluation handoff is not recorded",
    )
    require(
        evidence.get("d1_runtime_policy_change_authorized") is False,
        "D0 unexpectedly authorizes a runtime policy change",
    )
    require(
        evidence.get("qualified_head")
        == "ea48a988fa56b2d767e249b5e594db61735cb93b",
        "qualified head drifted",
    )
    require(
        evidence.get("workflow_run_id") == 36301344385,
        "workflow run id drifted",
    )

    artifact = evidence.get("artifact", {})
    require(
        artifact.get("id") == 10926270298,
        "artifact id drifted",
    )
    require(
        artifact.get("zip_sha256")
        == "63c76150d0fb1451e125c10565407638bf5d4b714ddee76a547fcd578fbdaac6",
        "artifact digest drifted",
    )

    benchmark = evidence.get("benchmark", {})
    require(
        benchmark.get("benchmark_id") == "atm-retrieval-v1-2026-09-20",
        "benchmark id drifted",
    )
    require(
        benchmark.get("corpus")
        == [
            {
                "repository_id": "ewd",
                "repository_version": "0.1.0",
                "snapshot_sha": "795a6b8e42e2a19497f686a89e7c3a6056e6930c",
            },
            {
                "repository_id": "cbd",
                "repository_version": "0.1.0",
                "snapshot_sha": "e6e3b3077b7d78e3f37d963e0734e9084e8a62ab",
            },
            {
                "repository_id": "rmd",
                "repository_version": "0.1.0",
                "snapshot_sha": "a8b5ff89a951d3194fdedb59771bc79937fe7bc7",
            },
        ],
        "frozen corpus drifted",
    )

    control = evidence.get("production_control", {})
    require(
        control.get("max_results_per_repository") == 6,
        "production result count drifted",
    )
    require(
        control.get("max_context_sources") == 12,
        "production source cap drifted",
    )
    require(
        control.get("max_context_bytes") == 32768,
        "production byte cap drifted",
    )
    for key, expected in (
        ("exact_id_success_at_1", 1.0),
        ("ndcg_at_5", 0.9025687328237045),
        ("canonical_required_recall_at_5", 0.9833333333333333),
        ("context_required_recall", 0.9833333333333333),
        ("evidence_traceability", 1.0),
    ):
        require(control.get(key) == expected, f"control metric {key} drifted")

    findings = evidence.get("findings", {})
    require(
        findings.get("source_caps_quality_preserved") == [8, 7, 6, 5, 4],
        "quality-preserving source-cap range drifted",
    )
    require(
        findings.get("results5", {}).get("quality_preserved") is False,
        "5-results candidate unexpectedly qualified",
    )
    require(
        findings.get("source_cap_3", {}).get("quality_preserved") is False,
        "source-cap 3 unexpectedly qualified",
    )
    require(
        findings.get("source_cap_3", {}).get("context_required_recall") == 0.95,
        "source-cap 3 recall boundary drifted",
    )
    require(
        findings.get("source_cap_3", {}).get("regressions")
        == [
            "status.rmd.current_boundary.en",
            "status.rmd.current_boundary.ro",
        ],
        "source-cap 3 regression topics drifted",
    )

    byte_cap = findings.get("bytes16k", {})
    require(
        byte_cap.get("quality_preserved") is True,
        "16 KiB quality result drifted",
    )
    require(
        byte_cap.get("pareto_frontier") is False,
        "16 KiB candidate unexpectedly became a frontier point",
    )
    require(
        byte_cap.get("evidence_bytes_reduction_percent_vs_production")
        == 0.46379205597270784,
        "16 KiB byte reduction drifted",
    )

    frontier = evidence.get("observational_frontier", {})
    require(
        frontier.get("policy") == "candidate-sources4",
        "D0 frontier policy drifted",
    )
    require(
        frontier.get("max_results_per_repository") == 6,
        "frontier retrieval count drifted",
    )
    require(
        frontier.get("max_context_sources") == 4,
        "frontier source cap drifted",
    )
    require(
        frontier.get("max_context_bytes") == 32768,
        "frontier byte cap drifted",
    )
    require(
        frontier.get("topic_regression_count") == 0,
        "frontier has topic regressions",
    )
    require(
        frontier.get("promotion_eligible_observational") is True,
        "frontier is not observationally eligible",
    )
    for key in (
        "exact_id_success_at_1",
        "ndcg_at_5",
        "canonical_required_recall_at_5",
        "context_required_recall",
        "evidence_traceability",
    ):
        require(
            frontier.get(key) == control.get(key),
            f"frontier quality metric {key} no longer matches production",
        )
    require(
        frontier.get("evidence_bytes_reduction_percent_vs_production")
        == 40.77399270162843,
        "frontier byte reduction drifted",
    )
    require(
        frontier.get("source_count_reduction_percent_vs_production")
        == 39.047619047619044,
        "frontier source reduction drifted",
    )

    policy = (
        ROOT / "src" / "retrieval_policy.h"
    ).read_text(encoding="utf-8")
    for marker in (
        "#define ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY 6",
        "#define ATM_PRODUCTION_GROUNDING_MAX_SOURCES 12",
        "#define ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES (32 * 1024)",
    ):
        require(marker in policy, f"production policy marker missing: {marker}")

    print(
        "D0 retrieval/context evidence validation passed: production remains "
        "6/12/32 KiB, source caps 8..4 preserve measured quality, source cap 3 "
        "is the first context-recall failure boundary, source cap 4 is the sole "
        "non-dominated observational frontier point, and no runtime policy "
        "change is authorized"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
