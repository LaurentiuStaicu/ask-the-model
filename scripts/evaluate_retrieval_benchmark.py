#!/usr/bin/env python3
"""Evaluate an AtM deterministic retrieval benchmark run.

The evaluator intentionally depends only on the Python standard library.
Benchmark topics use graded relevance:
  3 = canonical / essential
  2 = directly supporting
  1 = useful supplementary
  0 = irrelevant

nDCG uses linear gain equal to the relevance grade and logarithmic
rank discount: gain / log2(rank + 1).
"""

from __future__ import annotations

import argparse
import json
import math
import statistics
import sys
from pathlib import Path
from typing import Any


TARGETS = {
    "exact_id_success_at_1_min": 0.98,
    "ndcg_at_5_min": 0.90,
    "canonical_required_recall_at_5_min": 0.90,
    "wrong_repository_contamination_at_5_max": 0.05,
    "ro_en_ndcg_gap_max": 0.05,
    "evidence_traceability_min": 1.0,
}

REPOSITORIES = {"ewd", "cbd", "rmd"}
LANGUAGES = {"en", "ro", "mixed"}
TOPIC_TYPES = {
    "exact_entity_lookup",
    "current_status_boundary",
    "structure_mechanism",
    "evidence_provenance",
    "numeric_tabular",
    "negative_limitation",
    "unsupported_premise",
    "cross_repository_comparison",
    "multi_turn_follow_up",
}
SPLITS = {"development", "validation"}


class BenchmarkError(ValueError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        value = json.load(handle)

    if not isinstance(value, dict):
        raise BenchmarkError(f"{path} must contain one JSON object.")

    return value


def source_key(value: dict[str, Any]) -> tuple[str, str]:
    return (value["repository_id"], value["logical_source_id"])


def corpus_map(document: dict[str, Any]) -> dict[str, tuple[str, str]]:
    corpus = document.get("corpus")

    if not isinstance(corpus, list) or not corpus:
        raise BenchmarkError("corpus must be a non-empty array.")

    result: dict[str, tuple[str, str]] = {}

    for item in corpus:
        if not isinstance(item, dict):
            raise BenchmarkError("corpus entries must be objects.")

        repository_id = item.get("repository_id")
        version = item.get("repository_version")
        sha = item.get("snapshot_sha")

        if repository_id not in REPOSITORIES:
            raise BenchmarkError(
                f"unknown corpus repository_id: {repository_id!r}"
            )
        if repository_id in result:
            raise BenchmarkError(
                f"duplicate corpus repository_id: {repository_id}"
            )
        if not isinstance(version, str) or not version:
            raise BenchmarkError(
                f"{repository_id}: repository_version is required."
            )
        if (
            not isinstance(sha, str)
            or len(sha) != 40
            or any(character not in "0123456789abcdef" for character in sha)
        ):
            raise BenchmarkError(
                f"{repository_id}: snapshot_sha must be 40 lowercase hex."
            )

        result[repository_id] = (version, sha)

    return result


def validate_benchmark(benchmark: dict[str, Any]) -> None:
    if benchmark.get("schema_version") != 1:
        raise BenchmarkError("benchmark schema_version must be 1.")

    benchmark_id = benchmark.get("benchmark_id")
    if not isinstance(benchmark_id, str) or not benchmark_id:
        raise BenchmarkError("benchmark_id is required.")

    corpus = corpus_map(benchmark)
    topics = benchmark.get("topics")

    if not isinstance(topics, list) or not topics:
        raise BenchmarkError("topics must be a non-empty array.")

    topic_ids: set[str] = set()
    need_split: dict[str, str] = {}

    for topic in topics:
        if not isinstance(topic, dict):
            raise BenchmarkError("topic entries must be objects.")

        topic_id = topic.get("topic_id")
        need_id = topic.get("information_need_id")
        split = topic.get("split")
        topic_type = topic.get("topic_type")
        language = topic.get("language")
        query = topic.get("query")
        active = topic.get("active_repositories")
        explicit = topic.get("explicit_repositories")
        judgments = topic.get("judgments")
        expect_unsupported = topic.get("expect_unsupported")

        if not isinstance(topic_id, str) or not topic_id:
            raise BenchmarkError("every topic requires topic_id.")
        if topic_id in topic_ids:
            raise BenchmarkError(f"duplicate topic_id: {topic_id}")
        topic_ids.add(topic_id)

        if not isinstance(need_id, str) or not need_id:
            raise BenchmarkError(
                f"{topic_id}: information_need_id is required."
            )
        if split not in SPLITS:
            raise BenchmarkError(f"{topic_id}: invalid split {split!r}.")
        if need_id in need_split and need_split[need_id] != split:
            raise BenchmarkError(
                f"{topic_id}: language/query variants for {need_id!r} "
                "must stay in one benchmark split."
            )
        need_split[need_id] = split

        if topic_type not in TOPIC_TYPES:
            raise BenchmarkError(
                f"{topic_id}: invalid topic_type {topic_type!r}."
            )
        if language not in LANGUAGES:
            raise BenchmarkError(
                f"{topic_id}: invalid language {language!r}."
            )
        if not isinstance(query, str) or not query:
            raise BenchmarkError(f"{topic_id}: query is required.")
        if not isinstance(active, list) or not active:
            raise BenchmarkError(
                f"{topic_id}: active_repositories must be non-empty."
            )
        if len(active) != len(set(active)):
            raise BenchmarkError(
                f"{topic_id}: active_repositories contains duplicates."
            )
        if any(repository not in corpus for repository in active):
            raise BenchmarkError(
                f"{topic_id}: active repository is outside pinned corpus."
            )

        if not isinstance(explicit, list):
            raise BenchmarkError(
                f"{topic_id}: explicit_repositories must be an array."
            )
        if len(explicit) != len(set(explicit)):
            raise BenchmarkError(
                f"{topic_id}: explicit_repositories contains duplicates."
            )
        if any(repository not in active for repository in explicit):
            raise BenchmarkError(
                f"{topic_id}: explicit repository is outside active scope."
            )

        if not isinstance(judgments, list):
            raise BenchmarkError(
                f"{topic_id}: judgments must be an array."
            )
        if not isinstance(expect_unsupported, bool):
            raise BenchmarkError(
                f"{topic_id}: expect_unsupported must be boolean."
            )

        qrel_keys: set[tuple[str, str]] = set()
        positive = 0
        required = 0

        for judgment in judgments:
            if not isinstance(judgment, dict):
                raise BenchmarkError(
                    f"{topic_id}: judgment must be an object."
                )

            repository_id = judgment.get("repository_id")
            logical_id = judgment.get("logical_source_id")
            grade = judgment.get("grade")
            is_required = judgment.get("required")

            if repository_id not in active:
                raise BenchmarkError(
                    f"{topic_id}: judged source is outside active scope."
                )
            if not isinstance(logical_id, str) or not logical_id:
                raise BenchmarkError(
                    f"{topic_id}: judgment logical_source_id is required."
                )
            if not isinstance(grade, int) or isinstance(grade, bool):
                raise BenchmarkError(
                    f"{topic_id}: judgment grade must be integer."
                )
            if grade < 0 or grade > 3:
                raise BenchmarkError(
                    f"{topic_id}: judgment grade must be 0..3."
                )
            if not isinstance(is_required, bool):
                raise BenchmarkError(
                    f"{topic_id}: judgment required must be boolean."
                )

            key = (repository_id, logical_id)
            if key in qrel_keys:
                raise BenchmarkError(
                    f"{topic_id}: duplicate judgment for {key!r}."
                )
            qrel_keys.add(key)

            if grade > 0:
                positive += 1
            if is_required:
                if grade <= 0:
                    raise BenchmarkError(
                        f"{topic_id}: required evidence must be relevant."
                    )
                required += 1

        if expect_unsupported:
            if positive > 0 or required > 0:
                raise BenchmarkError(
                    f"{topic_id}: unsupported topic cannot have positive qrels."
                )
        else:
            if positive == 0:
                raise BenchmarkError(
                    f"{topic_id}: supported topic needs positive qrels."
                )
            if required == 0:
                raise BenchmarkError(
                    f"{topic_id}: supported topic needs required evidence."
                )

        expected_exact = topic.get("expected_exact")
        if topic_type == "exact_entity_lookup":
            if not isinstance(expected_exact, dict):
                raise BenchmarkError(
                    f"{topic_id}: exact lookup requires expected_exact."
                )
            if expected_exact.get("repository_id") not in active:
                raise BenchmarkError(
                    f"{topic_id}: exact target is outside active scope."
                )
            logical_id = expected_exact.get("logical_source_id")
            if not isinstance(logical_id, str) or not logical_id:
                raise BenchmarkError(
                    f"{topic_id}: exact target logical_source_id required."
                )


def validate_run(
    benchmark: dict[str, Any],
    run: dict[str, Any],
) -> None:
    if run.get("schema_version") != 1:
        raise BenchmarkError("run schema_version must be 1.")
    if run.get("benchmark_id") != benchmark.get("benchmark_id"):
        raise BenchmarkError("run benchmark_id does not match benchmark.")

    benchmark_corpus = corpus_map(benchmark)
    run_corpus = corpus_map(run)

    if run_corpus != benchmark_corpus:
        raise BenchmarkError(
            "run corpus does not exactly match pinned benchmark corpus."
        )

    benchmark_topics = {
        topic["topic_id"]: topic
        for topic in benchmark["topics"]
    }
    run_topics = run.get("topics")

    if not isinstance(run_topics, list):
        raise BenchmarkError("run topics must be an array.")

    seen: set[str] = set()

    for topic_run in run_topics:
        if not isinstance(topic_run, dict):
            raise BenchmarkError("run topic entries must be objects.")

        topic_id = topic_run.get("topic_id")
        if topic_id not in benchmark_topics:
            raise BenchmarkError(
                f"run contains unknown topic_id: {topic_id!r}"
            )
        if topic_id in seen:
            raise BenchmarkError(
                f"run contains duplicate topic_id: {topic_id}"
            )
        seen.add(topic_id)

        latency = topic_run.get("latency_ms")
        if (
            not isinstance(latency, (int, float))
            or isinstance(latency, bool)
            or latency < 0
        ):
            raise BenchmarkError(
                f"{topic_id}: latency_ms must be non-negative."
            )

        evidence_bytes = topic_run.get("evidence_bytes")
        if (
            not isinstance(evidence_bytes, int)
            or isinstance(evidence_bytes, bool)
            or evidence_bytes < 0
        ):
            raise BenchmarkError(
                f"{topic_id}: evidence_bytes must be non-negative integer."
            )

        token_count = topic_run.get("evidence_token_count")
        if token_count is not None and (
            not isinstance(token_count, int)
            or isinstance(token_count, bool)
            or token_count < 0
        ):
            raise BenchmarkError(
                f"{topic_id}: evidence_token_count must be null or integer."
            )

        for field in ("results", "context_sources"):
            values = topic_run.get(field)
            if not isinstance(values, list):
                raise BenchmarkError(
                    f"{topic_id}: {field} must be an array."
                )
            for item in values:
                if not isinstance(item, dict):
                    raise BenchmarkError(
                        f"{topic_id}: {field} entries must be objects."
                    )
                if not isinstance(item.get("repository_id"), str):
                    raise BenchmarkError(
                        f"{topic_id}: evidence repository_id required."
                    )
                if not isinstance(item.get("repository_version"), str):
                    raise BenchmarkError(
                        f"{topic_id}: evidence repository_version required."
                    )
                if not isinstance(item.get("snapshot_sha"), str):
                    raise BenchmarkError(
                        f"{topic_id}: evidence snapshot_sha required."
                    )
                logical_id = item.get("logical_source_id")
                if not isinstance(logical_id, str) or not logical_id:
                    raise BenchmarkError(
                        f"{topic_id}: evidence logical_source_id required."
                    )

    missing = set(benchmark_topics) - seen
    if missing:
        raise BenchmarkError(
            "run is missing benchmark topics: " +
            ", ".join(sorted(missing))
        )


def qrels_for(topic: dict[str, Any]) -> dict[tuple[str, str], int]:
    return {
        source_key(judgment): judgment["grade"]
        for judgment in topic["judgments"]
    }


def dcg(grades: list[int], k: int) -> float:
    total = 0.0
    for rank, grade in enumerate(grades[:k], start=1):
        total += grade / math.log2(rank + 1)
    return total


def ndcg_at_k(
    topic: dict[str, Any],
    results: list[dict[str, Any]],
    k: int,
) -> float | None:
    qrels = qrels_for(topic)
    positive_grades = [grade for grade in qrels.values() if grade > 0]

    if not positive_grades:
        return None

    result_grades = [
        qrels.get(source_key(result), 0)
        for result in results[:k]
    ]
    ideal = sorted(positive_grades, reverse=True)
    ideal_dcg = dcg(ideal, k)

    if ideal_dcg == 0:
        return None

    return dcg(result_grades, k) / ideal_dcg


def reciprocal_rank(
    topic: dict[str, Any],
    results: list[dict[str, Any]],
) -> float | None:
    qrels = qrels_for(topic)

    if not any(grade > 0 for grade in qrels.values()):
        return None

    for rank, result in enumerate(results, start=1):
        if qrels.get(source_key(result), 0) > 0:
            return 1.0 / rank

    return 0.0


def required_recall_at_k(
    topic: dict[str, Any],
    results: list[dict[str, Any]],
    k: int,
) -> float | None:
    required = {
        source_key(judgment)
        for judgment in topic["judgments"]
        if judgment["required"]
    }

    if not required:
        return None

    retrieved = {
        source_key(result)
        for result in results[:k]
    }

    return len(required & retrieved) / len(required)


def context_precision(
    topic: dict[str, Any],
    context_sources: list[dict[str, Any]],
) -> float | None:
    qrels = qrels_for(topic)

    if not any(grade > 0 for grade in qrels.values()):
        return None

    if not context_sources:
        return 0.0

    relevant = sum(
        1
        for source in context_sources
        if qrels.get(source_key(source), 0) > 0
    )
    return relevant / len(context_sources)


def traceability_fraction(
    corpus: dict[str, tuple[str, str]],
    topic_runs: list[dict[str, Any]],
) -> float:
    total = 0
    valid = 0

    for topic_run in topic_runs:
        for field in ("results", "context_sources"):
            for item in topic_run[field]:
                total += 1
                repository_id = item["repository_id"]
                expected = corpus.get(repository_id)

                if expected is None:
                    continue

                if (
                    item["repository_version"] == expected[0]
                    and item["snapshot_sha"] == expected[1]
                ):
                    valid += 1

    return 1.0 if total == 0 else valid / total


def mean_or_none(values: list[float]) -> float | None:
    return statistics.fmean(values) if values else None


def percentile_nearest_rank(
    values: list[float],
    fraction: float,
) -> float | None:
    if not values:
        return None

    ordered = sorted(values)
    rank = max(1, math.ceil(fraction * len(ordered)))
    return float(ordered[rank - 1])


def evaluate(
    benchmark: dict[str, Any],
    run: dict[str, Any],
) -> dict[str, Any]:
    validate_benchmark(benchmark)
    validate_run(benchmark, run)

    corpus = corpus_map(benchmark)
    topic_by_id = {
        topic["topic_id"]: topic
        for topic in benchmark["topics"]
    }

    exact_scores: list[float] = []
    reciprocal_ranks: list[float] = []
    ndcg_scores: list[float] = []
    recall_scores: list[float] = []
    context_precisions: list[float] = []
    latencies: list[float] = []
    evidence_bytes: list[int] = []
    evidence_tokens: list[int] = []
    wrong_repository = 0
    scoped_results = 0
    unsupported_empty = 0
    unsupported_total = 0
    duplicate_results = 0
    total_results = 0
    language_ndcg: dict[str, list[float]] = {
        "en": [],
        "ro": [],
        "mixed": [],
    }
    need_language_ndcg: dict[str, dict[str, list[float]]] = {}

    for topic_run in run["topics"]:
        topic = topic_by_id[topic_run["topic_id"]]
        results = topic_run["results"]
        context_sources = topic_run["context_sources"]

        latencies.append(float(topic_run["latency_ms"]))
        evidence_bytes.append(topic_run["evidence_bytes"])

        if topic_run.get("evidence_token_count") is not None:
            evidence_tokens.append(topic_run["evidence_token_count"])

        if topic["topic_type"] == "exact_entity_lookup":
            target = source_key(topic["expected_exact"])
            success = (
                1.0
                if results and source_key(results[0]) == target
                else 0.0
            )
            exact_scores.append(success)

        rr = reciprocal_rank(topic, results)
        if rr is not None:
            reciprocal_ranks.append(rr)

        ndcg = ndcg_at_k(topic, results, 5)
        if ndcg is not None:
            ndcg_scores.append(ndcg)
            language = topic["language"]
            language_ndcg[language].append(ndcg)
            need_language_ndcg.setdefault(
                topic["information_need_id"],
                {},
            ).setdefault(language, []).append(ndcg)

        recall = required_recall_at_k(topic, results, 5)
        if recall is not None:
            recall_scores.append(recall)

        precision = context_precision(topic, context_sources)
        if precision is not None:
            context_precisions.append(precision)

        if (
            len(topic["explicit_repositories"]) == 1
            and results
        ):
            expected_repository = topic["explicit_repositories"][0]
            for result in results[:5]:
                scoped_results += 1
                if result["repository_id"] != expected_repository:
                    wrong_repository += 1

        if topic["expect_unsupported"]:
            unsupported_total += 1
            if not context_sources:
                unsupported_empty += 1

        seen_results: set[tuple[str, str]] = set()
        for result in results:
            total_results += 1
            key = source_key(result)
            if key in seen_results:
                duplicate_results += 1
            seen_results.add(key)

    paired_gaps: list[float] = []

    for languages in need_language_ndcg.values():
        if "en" not in languages or "ro" not in languages:
            continue

        en_mean = statistics.fmean(languages["en"])
        ro_mean = statistics.fmean(languages["ro"])
        paired_gaps.append(abs(ro_mean - en_mean))

    traceability = traceability_fraction(
        corpus,
        run["topics"],
    )

    metrics = {
        "exact_id_success_at_1": mean_or_none(exact_scores),
        "mrr": mean_or_none(reciprocal_ranks),
        "ndcg_at_5": mean_or_none(ndcg_scores),
        "canonical_required_recall_at_5": mean_or_none(
            recall_scores
        ),
        "context_precision": mean_or_none(context_precisions),
        "wrong_repository_contamination_at_5": (
            wrong_repository / scoped_results
            if scoped_results
            else 0.0
        ),
        "ro_en_ndcg_gap": mean_or_none(paired_gaps),
        "ndcg_at_5_en": mean_or_none(language_ndcg["en"]),
        "ndcg_at_5_ro": mean_or_none(language_ndcg["ro"]),
        "ndcg_at_5_mixed": mean_or_none(language_ndcg["mixed"]),
        "evidence_traceability": traceability,
        "retrieval_latency_ms_median": (
            statistics.median(latencies)
            if latencies
            else None
        ),
        "retrieval_latency_ms_p95": percentile_nearest_rank(
            latencies,
            0.95,
        ),
        "evidence_bytes_mean": (
            statistics.fmean(evidence_bytes)
            if evidence_bytes
            else None
        ),
        "evidence_bytes_max": (
            max(evidence_bytes)
            if evidence_bytes
            else None
        ),
        "evidence_token_count_mean": (
            statistics.fmean(evidence_tokens)
            if evidence_tokens
            else None
        ),
        "evidence_token_count_max": (
            max(evidence_tokens)
            if evidence_tokens
            else None
        ),
        "evidence_token_budget_coverage": (
            len(evidence_tokens) / len(run["topics"])
            if run["topics"]
            else 0.0
        ),
        "unsupported_empty_context_rate": (
            unsupported_empty / unsupported_total
            if unsupported_total
            else None
        ),
        "duplicate_result_rate": (
            duplicate_results / total_results
            if total_results
            else 0.0
        ),
    }

    gates = {
        "exact_id_success_at_1": (
            metrics["exact_id_success_at_1"] is not None
            and metrics["exact_id_success_at_1"]
            >= TARGETS["exact_id_success_at_1_min"]
        ),
        "ndcg_at_5": (
            metrics["ndcg_at_5"] is not None
            and metrics["ndcg_at_5"]
            >= TARGETS["ndcg_at_5_min"]
        ),
        "canonical_required_recall_at_5": (
            metrics["canonical_required_recall_at_5"] is not None
            and metrics["canonical_required_recall_at_5"]
            >= TARGETS["canonical_required_recall_at_5_min"]
        ),
        "wrong_repository_contamination_at_5": (
            metrics["wrong_repository_contamination_at_5"]
            <= TARGETS["wrong_repository_contamination_at_5_max"]
        ),
        "ro_en_ndcg_gap": (
            metrics["ro_en_ndcg_gap"] is not None
            and metrics["ro_en_ndcg_gap"]
            <= TARGETS["ro_en_ndcg_gap_max"]
        ),
        "evidence_traceability": (
            math.isclose(
                metrics["evidence_traceability"],
                TARGETS["evidence_traceability_min"],
                abs_tol=1e-12,
            )
        ),
    }

    return {
        "schema_version": 1,
        "benchmark_id": benchmark["benchmark_id"],
        "run_id": run["run_id"],
        "topic_count": len(benchmark["topics"]),
        "metrics": metrics,
        "provisional_targets": TARGETS,
        "gates": gates,
        "passes_provisional_targets": all(gates.values()),
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Evaluate an AtM retrieval benchmark run."
    )
    parser.add_argument("benchmark", type=Path)
    parser.add_argument("run", type=Path)
    parser.add_argument(
        "--pretty",
        action="store_true",
        help="Pretty-print JSON output.",
    )
    args = parser.parse_args()

    try:
        benchmark = load_json(args.benchmark)
        run = load_json(args.run)
        result = evaluate(benchmark, run)
    except (OSError, json.JSONDecodeError, BenchmarkError) as error:
        print(f"benchmark evaluation failed: {error}", file=sys.stderr)
        return 2

    json.dump(
        result,
        sys.stdout,
        ensure_ascii=False,
        indent=2 if args.pretty else None,
        sort_keys=True,
    )
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
