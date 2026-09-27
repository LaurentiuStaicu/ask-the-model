#!/usr/bin/env python3

import argparse
import json
import math
import statistics
from pathlib import Path


POLICIES = [
    "production",
    "candidate-results5",
    "candidate-sources8",
    "candidate-sources7",
    "candidate-sources6",
    "candidate-sources5",
    "candidate-sources4",
    "candidate-sources3",
    "candidate-bytes16k",
    "candidate-compact",
]


def percentile_nearest_rank(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil(fraction * len(ordered)))
    return ordered[rank - 1]


def load(path: Path) -> dict:
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def run_path(root: Path, policy: str) -> Path:
    if policy == "production":
        return root / "atm-retrieval-production-policy-run.json"
    return root / f"atm-retrieval-{policy}-run.json"


def evaluation_path(root: Path, policy: str) -> Path:
    if policy == "production":
        return root / "atm-retrieval-production-policy-evaluation.json"
    return root / f"atm-retrieval-{policy}-evaluation.json"


def latency_summary(run: dict, field: str) -> dict:
    values = [
        float(topic[field])
        for topic in run["topics"]
        if topic.get(field) is not None
    ]
    return {
        "median_ms": statistics.median(values) if values else None,
        "p95_ms": percentile_nearest_rank(values, 0.95),
        "mean_ms": statistics.fmean(values) if values else None,
    }


def topic_map(evaluation: dict) -> dict[str, dict]:
    return {
        item["topic_id"]: item
        for item in evaluation["topic_diagnostics"]
    }


def diagnostic(
    topic_id: str,
    baseline: dict,
    candidate: dict,
    metric: str,
) -> dict:
    return {
        "topic_id": topic_id,
        "information_need_id": candidate.get("information_need_id"),
        "topic_type": candidate.get("topic_type"),
        "language": candidate.get("language"),
        "metric": metric,
        "baseline": baseline.get(metric),
        "candidate": candidate.get(metric),
        "delta": (
            candidate.get(metric) - baseline.get(metric)
            if candidate.get(metric) is not None
            and baseline.get(metric) is not None
            else None
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path("/tmp"))
    parser.add_argument("--benchmark", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--measurement-head", required=True)
    parser.add_argument("--workflow-commit", required=True)
    args = parser.parse_args()

    benchmark = load(args.benchmark)
    baseline_run = load(run_path(args.root, "production"))
    baseline_evaluation = load(evaluation_path(args.root, "production"))
    baseline_metrics = baseline_evaluation["metrics"]
    baseline_topics = topic_map(baseline_evaluation)

    expected_control = {
        "name": "production",
        "max_results_per_repository": 6,
        "max_context_sources": 12,
        "max_context_bytes": 32 * 1024,
    }
    if baseline_run.get("policy") != expected_control:
        raise SystemExit(
            "Production control policy does not match current shared 6/12/32-KiB baseline."
        )
    if not baseline_evaluation["passes_provisional_targets"]:
        raise SystemExit("Production control failed frozen quality gates.")

    rows = []
    epsilon = 1e-12

    for policy in POLICIES:
        run = load(run_path(args.root, policy))
        evaluation = load(evaluation_path(args.root, policy))
        metrics = evaluation["metrics"]
        topics = topic_map(evaluation)

        if topics.keys() != baseline_topics.keys():
            missing = sorted(baseline_topics.keys() - topics.keys())
            extra = sorted(topics.keys() - baseline_topics.keys())
            raise SystemExit(
                f"{policy}: topic set differs from production; "
                f"missing={missing} extra={extra}"
            )

        regressions = []

        for topic_id in sorted(baseline_topics):
            base = baseline_topics[topic_id]
            candidate = topics[topic_id]

            for metric in (
                "exact_id_success_at_1",
                "ndcg_at_5",
                "context_required_recall",
            ):
                base_value = base.get(metric)
                candidate_value = candidate.get(metric)

                if base_value is None or candidate_value is None:
                    continue

                if candidate_value < base_value - epsilon:
                    regressions.append(
                        diagnostic(
                            topic_id,
                            base,
                            candidate,
                            metric,
                        )
                    )

        quality_preserved = (
            evaluation["passes_provisional_targets"]
            and metrics.get("exact_id_success_at_1")
            >= baseline_metrics.get("exact_id_success_at_1") - epsilon
            and metrics.get("ndcg_at_5")
            >= baseline_metrics.get("ndcg_at_5") - epsilon
            and metrics.get("context_required_recall")
            >= baseline_metrics.get("context_required_recall") - epsilon
            and not regressions
        )

        baseline_sources = baseline_metrics.get("context_source_count_mean")
        baseline_bytes = baseline_metrics.get("evidence_bytes_mean")
        source_delta = (
            metrics.get("context_source_count_mean") - baseline_sources
            if baseline_sources is not None
            and metrics.get("context_source_count_mean") is not None
            else None
        )
        bytes_delta = (
            metrics.get("evidence_bytes_mean") - baseline_bytes
            if baseline_bytes is not None
            and metrics.get("evidence_bytes_mean") is not None
            else None
        )

        resource_benefit_observed = (
            (source_delta is not None and source_delta < -epsilon)
            or (bytes_delta is not None and bytes_delta < -epsilon)
        )

        rows.append(
            {
                "policy": policy,
                "policy_parameters": run.get("policy"),
                "quality": {
                    "passes_existing_quality_gates": evaluation[
                        "passes_provisional_targets"
                    ],
                    "exact_id_success_at_1": metrics.get(
                        "exact_id_success_at_1"
                    ),
                    "ndcg_at_5": metrics.get("ndcg_at_5"),
                    "canonical_required_recall_at_5": metrics.get(
                        "canonical_required_recall_at_5"
                    ),
                    "context_required_recall": metrics.get(
                        "context_required_recall"
                    ),
                    "evidence_traceability": metrics.get(
                        "evidence_traceability"
                    ),
                },
                "context_cost": {
                    "source_count_mean": metrics.get(
                        "context_source_count_mean"
                    ),
                    "source_count_max": metrics.get(
                        "context_source_count_max"
                    ),
                    "evidence_bytes_mean": metrics.get(
                        "evidence_bytes_mean"
                    ),
                    "evidence_bytes_max": metrics.get(
                        "evidence_bytes_max"
                    ),
                    "delta_source_count_mean_vs_production": source_delta,
                    "delta_evidence_bytes_mean_vs_production": bytes_delta,
                    "source_count_reduction_percent_vs_production": (
                        (-source_delta / baseline_sources) * 100.0
                        if source_delta is not None
                        and baseline_sources not in (None, 0)
                        else None
                    ),
                    "evidence_bytes_reduction_percent_vs_production": (
                        (-bytes_delta / baseline_bytes) * 100.0
                        if bytes_delta is not None
                        and baseline_bytes not in (None, 0)
                        else None
                    ),
                },
                "timing_observational": {
                    "retrieval": latency_summary(run, "latency_ms"),
                    "context_assembly": latency_summary(
                        run,
                        "context_assembly_latency_ms",
                    ),
                },
                "topic_regression_count": len(regressions),
                "topic_regressions": regressions,
                "quality_preserved_vs_production": quality_preserved,
                "resource_benefit_observed": resource_benefit_observed,
                "pareto_dominated_by": [],
                "pareto_frontier_observational": False,
                "promotion_eligible_observational": False,
            }
        )

    quality_candidates = [
        row
        for row in rows
        if row["policy"] != "production"
        and row["quality_preserved_vs_production"]
        and row["resource_benefit_observed"]
    ]

    for row in quality_candidates:
        row_cost = row["context_cost"]
        dominated_by = []

        for other in quality_candidates:
            if other is row:
                continue

            other_cost = other["context_cost"]
            row_sources = row_cost["source_count_mean"]
            row_bytes = row_cost["evidence_bytes_mean"]
            other_sources = other_cost["source_count_mean"]
            other_bytes = other_cost["evidence_bytes_mean"]

            if None in (
                row_sources,
                row_bytes,
                other_sources,
                other_bytes,
            ):
                continue

            no_worse = (
                other_sources <= row_sources + epsilon
                and other_bytes <= row_bytes + epsilon
            )
            strictly_better = (
                other_sources < row_sources - epsilon
                or other_bytes < row_bytes - epsilon
            )

            if no_worse and strictly_better:
                dominated_by.append(other["policy"])

        row["pareto_dominated_by"] = sorted(dominated_by)
        row["pareto_frontier_observational"] = not dominated_by
        row["promotion_eligible_observational"] = not dominated_by

    output = {
        "schema_version": 3,
        "status": "measurement-only",
        "measurement_head_sha": args.measurement_head,
        "workflow_commit_sha": args.workflow_commit,
        "benchmark_id": benchmark["benchmark_id"],
        "corpus": benchmark["corpus"],
        "production_control": expected_control,
        "timing_interpretation": (
            "retrieval and context-assembly timing are observational; "
            "CI runner variance is not a blocking quality gate"
        ),
        "promotion_interpretation": (
            "promotion_eligible_observational requires quality preservation, "
            "measured resource reduction and membership on the non-dominated "
            "source-count/evidence-byte frontier; timing is excluded from "
            "dominance because CI variance is observational. D1 remains a "
            "separate production-policy decision"
        ),
        "policies": rows,
    }

    args.output.write_text(
        json.dumps(output, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    for row in rows:
        print(
            f"{row['policy']}: "
            f"quality={row['quality_preserved_vs_production']} "
            f"benefit={row['resource_benefit_observed']} "
            f"pareto={row['pareto_frontier_observational']} "
            f"topic_regressions={row['topic_regression_count']} "
            f"sources_mean={row['context_cost']['source_count_mean']} "
            f"bytes_mean={row['context_cost']['evidence_bytes_mean']} "
            f"bytes_reduction_pct="
            f"{row['context_cost']['evidence_bytes_reduction_percent_vs_production']} "
            f"context_ms_mean="
            f"{row['timing_observational']['context_assembly']['mean_ms']}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
