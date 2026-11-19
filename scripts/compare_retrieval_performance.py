#!/usr/bin/env python3
"""Compare paired AtM retrieval benchmark runs from two source revisions.

The comparison keeps the benchmark, pinned corpus, runner, policy and CI
machine constant. Timing is observational; quality and evidence differences
are reported separately rather than hidden inside aggregate latency numbers.
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any


QUALITY_METRICS = (
    "exact_id_success_at_1",
    "ndcg_at_5",
    "canonical_required_recall_at_5",
    "context_required_recall",
    "evidence_traceability",
    "clarification_outcome_accuracy",
)
LATENCY_FIELDS = {
    "retrieval": "latency_ms",
    "context_assembly": "context_assembly_latency_ms",
}


class ComparisonError(ValueError):
    pass


def load(path: Path) -> dict[str, Any]:
    try:
        with path.open(encoding="utf-8") as handle:
            value = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise ComparisonError(f"could not read {path}: {error}") from error

    if not isinstance(value, dict):
        raise ComparisonError(f"{path} must contain a JSON object.")
    return value


def percentile_nearest_rank(values: list[float], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    rank = max(1, math.ceil(fraction * len(ordered)))
    return ordered[rank - 1]


def latency_summary(values: list[float]) -> dict[str, float | None]:
    return {
        "sample_count": len(values),
        "median_ms": statistics.median(values) if values else None,
        "p95_ms": percentile_nearest_rank(values, 0.95),
        "mean_ms": statistics.fmean(values) if values else None,
    }


def percent_delta(baseline: float | None, candidate: float | None) -> float | None:
    if baseline is None or candidate is None or baseline == 0:
        return None
    return ((candidate / baseline) - 1.0) * 100.0


def mean_metric(evaluations: list[dict[str, Any]], metric: str) -> float | None:
    values = [
        float(evaluation["metrics"][metric])
        for evaluation in evaluations
        if evaluation.get("metrics", {}).get(metric) is not None
    ]
    return statistics.fmean(values) if values else None


def topic_map(run: dict[str, Any]) -> dict[str, dict[str, Any]]:
    topics = run.get("topics")
    if not isinstance(topics, list):
        raise ComparisonError("run topics must be an array.")

    result: dict[str, dict[str, Any]] = {}
    for topic in topics:
        if not isinstance(topic, dict):
            raise ComparisonError("run topic entries must be objects.")
        topic_id = topic.get("topic_id")
        if not isinstance(topic_id, str) or not topic_id or topic_id in result:
            raise ComparisonError("run contains an invalid or duplicate topic_id.")
        result[topic_id] = topic
    return result


def evaluation_topic_map(evaluation: dict[str, Any]) -> dict[str, dict[str, Any]]:
    diagnostics = evaluation.get("topic_diagnostics")
    if not isinstance(diagnostics, list):
        raise ComparisonError("evaluation topic_diagnostics must be an array.")
    result: dict[str, dict[str, Any]] = {}
    for item in diagnostics:
        if not isinstance(item, dict):
            raise ComparisonError("evaluation topic diagnostics must be objects.")
        topic_id = item.get("topic_id")
        if not isinstance(topic_id, str) or not topic_id or topic_id in result:
            raise ComparisonError("evaluation contains an invalid or duplicate topic_id.")
        result[topic_id] = item
    return result


def output_signature(topic: dict[str, Any]) -> str:
    fields = (
        "outcome",
        "results",
        "context_sources",
        "evidence_bytes",
        "evidence_token_count",
    )
    return json.dumps(
        {field: topic.get(field) for field in fields},
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
    )


def compare(args: argparse.Namespace) -> dict[str, Any]:
    benchmark = load(args.benchmark)
    if not isinstance(benchmark.get("benchmark_id"), str):
        raise ComparisonError("benchmark_id is missing.")
    expected_topics = {
        topic["topic_id"] for topic in benchmark.get("topics", [])
        if isinstance(topic, dict) and isinstance(topic.get("topic_id"), str)
    }
    if not expected_topics:
        raise ComparisonError("benchmark has no valid topics.")

    baseline_runs: list[dict[str, Any]] = []
    candidate_runs: list[dict[str, Any]] = []
    baseline_evaluations: list[dict[str, Any]] = []
    candidate_evaluations: list[dict[str, Any]] = []
    baseline_maps: list[dict[str, dict[str, Any]]] = []
    candidate_maps: list[dict[str, dict[str, Any]]] = []
    baseline_diagnostics: list[dict[str, dict[str, Any]]] = []
    candidate_diagnostics: list[dict[str, dict[str, Any]]] = []

    for sample in range(1, args.samples + 1):
        baseline_run = load(Path(args.baseline_run_pattern.format(sample=sample)))
        candidate_run = load(Path(args.candidate_run_pattern.format(sample=sample)))
        baseline_eval = load(Path(args.baseline_evaluation_pattern.format(sample=sample)))
        candidate_eval = load(Path(args.candidate_evaluation_pattern.format(sample=sample)))

        for label, run, evaluation in (
            ("baseline", baseline_run, baseline_eval),
            ("candidate", candidate_run, candidate_eval),
        ):
            if run.get("benchmark_id") != benchmark["benchmark_id"]:
                raise ComparisonError(f"{label} sample {sample}: benchmark ID mismatch.")
            if evaluation.get("benchmark_id") != benchmark["benchmark_id"]:
                raise ComparisonError(f"{label} sample {sample}: evaluation ID mismatch.")
            if run.get("policy", {}).get("name") != "production":
                raise ComparisonError(f"{label} sample {sample}: expected production policy.")

        if baseline_run.get("corpus") != candidate_run.get("corpus"):
            raise ComparisonError(f"sample {sample}: baseline/candidate corpus differs.")
        if baseline_run.get("corpus") != benchmark.get("corpus"):
            raise ComparisonError(f"sample {sample}: run corpus differs from benchmark.")

        base_topics = topic_map(baseline_run)
        candidate_topics = topic_map(candidate_run)
        if set(base_topics) != expected_topics or set(candidate_topics) != expected_topics:
            raise ComparisonError(f"sample {sample}: run topic set differs from benchmark.")

        baseline_runs.append(baseline_run)
        candidate_runs.append(candidate_run)
        baseline_evaluations.append(baseline_eval)
        candidate_evaluations.append(candidate_eval)
        baseline_maps.append(base_topics)
        candidate_maps.append(candidate_topics)
        base_diagnostics = evaluation_topic_map(baseline_eval)
        candidate_topic_diagnostics = evaluation_topic_map(candidate_eval)
        if set(base_diagnostics) != expected_topics or set(candidate_topic_diagnostics) != expected_topics:
            raise ComparisonError(f"sample {sample}: evaluation topic set differs from benchmark.")
        baseline_diagnostics.append(base_diagnostics)
        candidate_diagnostics.append(candidate_topic_diagnostics)

    baseline_policy = baseline_runs[0]["policy"]
    for label, runs in (("baseline", baseline_runs), ("candidate", candidate_runs)):
        for sample, run in enumerate(runs, start=1):
            if run.get("policy") != baseline_policy:
                raise ComparisonError(f"{label} sample {sample}: policy differs from baseline.")

    latency: dict[str, Any] = {}
    for label, field in LATENCY_FIELDS.items():
        baseline_values = [
            float(topic[field])
            for run in baseline_runs
            for topic in run["topics"]
            if topic.get(field) is not None
        ]
        candidate_values = [
            float(topic[field])
            for run in candidate_runs
            for topic in run["topics"]
            if topic.get(field) is not None
        ]
        base_stats = latency_summary(baseline_values)
        candidate_stats = latency_summary(candidate_values)
        latency[label] = {
            "baseline": base_stats,
            "candidate": candidate_stats,
            "median_delta_percent": percent_delta(
                base_stats["median_ms"], candidate_stats["median_ms"]
            ),
            "p95_delta_percent": percent_delta(
                base_stats["p95_ms"], candidate_stats["p95_ms"]
            ),
            "paired_topic_delta_percent_median": None,
        }

        paired_deltas: list[float] = []
        for base_topics, candidate_topics in zip(baseline_maps, candidate_maps):
            for topic_id in sorted(expected_topics):
                base_value = base_topics[topic_id].get(field)
                candidate_value = candidate_topics[topic_id].get(field)
                delta = percent_delta(
                    float(base_value) if base_value is not None else None,
                    float(candidate_value) if candidate_value is not None else None,
                )
                if delta is not None:
                    paired_deltas.append(delta)
        latency[label]["paired_topic_delta_percent_median"] = (
            statistics.median(paired_deltas) if paired_deltas else None
        )

    quality: dict[str, Any] = {}
    for metric in QUALITY_METRICS:
        baseline_value = mean_metric(baseline_evaluations, metric)
        candidate_value = mean_metric(candidate_evaluations, metric)
        quality[metric] = {
            "baseline_mean": baseline_value,
            "candidate_mean": candidate_value,
            "delta": (
                candidate_value - baseline_value
                if baseline_value is not None and candidate_value is not None
                else None
            ),
        }

    changed_topics: set[str] = set()
    per_topic_regressions: list[dict[str, Any]] = []
    for sample, (base_topics, candidate_topics, base_eval_topics, candidate_eval_topics) in enumerate(
        zip(baseline_maps, candidate_maps, baseline_diagnostics, candidate_diagnostics),
        start=1,
    ):
        for topic_id in sorted(expected_topics):
            base_topic = base_topics[topic_id]
            candidate_topic = candidate_topics[topic_id]
            if output_signature(base_topic) != output_signature(candidate_topic):
                changed_topics.add(topic_id)

            base_diagnostic = base_eval_topics[topic_id]
            candidate_diagnostic = candidate_eval_topics[topic_id]
            for metric in ("exact_id_success_at_1", "ndcg_at_5", "context_required_recall"):
                base_value = base_diagnostic.get(metric)
                candidate_value = candidate_diagnostic.get(metric)
                if (
                    isinstance(base_value, (int, float))
                    and isinstance(candidate_value, (int, float))
                    and candidate_value < base_value - 1e-12
                ):
                    per_topic_regressions.append({
                        "sample": sample,
                        "topic_id": topic_id,
                        "metric": metric,
                        "baseline": base_value,
                        "candidate": candidate_value,
                    })

    quality_not_lower = not per_topic_regressions and all(
        item["delta"] is None or item["delta"] >= -1e-12
        for item in quality.values()
    )

    return {
        "schema_version": 1,
        "status": "measurement-only",
        "benchmark_id": benchmark["benchmark_id"],
        "baseline_revision": args.baseline_revision,
        "candidate_revision": args.candidate_revision,
        "corpus": benchmark["corpus"],
        "policy": baseline_policy,
        "paired_samples": args.samples,
        "topic_count": len(expected_topics),
        "latency": latency,
        "quality_metrics": quality,
        "quality_not_lower_than_baseline": quality_not_lower,
        "topic_output_equivalence": {
            "identical": not changed_topics,
            "changed_topic_count": len(changed_topics),
            "changed_topic_ids": sorted(changed_topics),
        },
        "per_topic_quality_regressions": per_topic_regressions,
        "timing_interpretation": (
            "Timing is observational. The same runner, benchmark, pinned corpus, "
            "production policy and CI host are used; runs alternate order to reduce "
            "systematic order bias. This is not a hard CI performance gate."
        ),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", type=Path, required=True)
    parser.add_argument("--baseline-run-pattern", required=True)
    parser.add_argument("--candidate-run-pattern", required=True)
    parser.add_argument("--baseline-evaluation-pattern", required=True)
    parser.add_argument("--candidate-evaluation-pattern", required=True)
    parser.add_argument("--baseline-revision", required=True)
    parser.add_argument("--candidate-revision", required=True)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    if args.samples < 3:
        parser.error("--samples must be at least 3 for a useful paired comparison.")

    try:
        result = compare(args)
    except (ComparisonError, KeyError, TypeError, ValueError) as error:
        print(f"retrieval performance comparison failed: {error}", file=sys.stderr)
        return 2

    args.output.write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    for label, metrics in result["latency"].items():
        print(
            f"{label}: "
            f"baseline median/p95={metrics['baseline']['median_ms']}/"
            f"{metrics['baseline']['p95_ms']} ms; "
            f"candidate median/p95={metrics['candidate']['median_ms']}/"
            f"{metrics['candidate']['p95_ms']} ms; "
            f"median delta={metrics['median_delta_percent']}%; "
            f"paired topic delta={metrics['paired_topic_delta_percent_median']}%"
        )
    print(
        "Quality not lower than baseline: "
        f"{result['quality_not_lower_than_baseline']}; "
        "topic outputs identical: "
        f"{result['topic_output_equivalence']['identical']}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
