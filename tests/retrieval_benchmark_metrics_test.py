#!/usr/bin/env python3

import math
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import evaluate_retrieval_benchmark as evaluator  # noqa: E402


EWD_SHA = "1" * 40
RMD_SHA = "2" * 40


def evidence(repository_id, logical_id):
    version = "0.1.0"
    sha = EWD_SHA if repository_id == "ewd" else RMD_SHA
    return {
        "repository_id": repository_id,
        "repository_version": version,
        "snapshot_sha": sha,
        "logical_source_id": logical_id,
    }


def benchmark_fixture():
    return {
        "schema_version": 1,
        "benchmark_id": "atm-test-benchmark",
        "corpus": [
            {
                "repository_id": "ewd",
                "repository_version": "0.1.0",
                "snapshot_sha": EWD_SHA,
            },
            {
                "repository_id": "rmd",
                "repository_version": "0.1.0",
                "snapshot_sha": RMD_SHA,
            },
        ],
        "topics": [
            {
                "topic_id": "exact-food-en",
                "information_need_id": "food-id",
                "split": "validation",
                "topic_type": "exact_entity_lookup",
                "language": "en",
                "query": "food_per_capita EWD",
                "active_repositories": ["ewd", "rmd"],
                "explicit_repositories": ["ewd"],
                "expected_exact": {
                    "repository_id": "ewd",
                    "logical_source_id": "ewd:entity:variable:food_per_capita",
                },
                "judgments": [
                    {
                        "repository_id": "ewd",
                        "logical_source_id": "ewd:entity:variable:food_per_capita",
                        "grade": 3,
                        "required": True,
                    },
                    {
                        "repository_id": "ewd",
                        "logical_source_id": "ewd:section:STATUS.md:lines:1-4",
                        "grade": 1,
                        "required": False,
                    },
                ],
                "expected_outcome": "retrieval",
                "expect_unsupported": False,
            },
            {
                "topic_id": "exact-food-ro",
                "information_need_id": "food-id",
                "split": "validation",
                "topic_type": "exact_entity_lookup",
                "language": "ro",
                "query": "variabila food_per_capita din EWD",
                "active_repositories": ["ewd", "rmd"],
                "explicit_repositories": ["ewd"],
                "expected_exact": {
                    "repository_id": "ewd",
                    "logical_source_id": "ewd:entity:variable:food_per_capita",
                },
                "judgments": [
                    {
                        "repository_id": "ewd",
                        "logical_source_id": "ewd:entity:variable:food_per_capita",
                        "grade": 3,
                        "required": True,
                    },
                    {
                        "repository_id": "ewd",
                        "logical_source_id": "ewd:section:STATUS.md:lines:1-4",
                        "grade": 1,
                        "required": False,
                    },
                ],
                "expected_outcome": "retrieval",
                "expect_unsupported": False,
            },
            {
                "topic_id": "rmd-status-mixed",
                "information_need_id": "rmd-current-status",
                "split": "validation",
                "topic_type": "current_status_boundary",
                "language": "mixed",
                "query": "RMD current stare",
                "active_repositories": ["rmd"],
                "explicit_repositories": ["rmd"],
                "expected_exact": None,
                "judgments": [
                    {
                        "repository_id": "rmd",
                        "logical_source_id": "rmd:section:STATUS.md:lines:1-8",
                        "grade": 3,
                        "required": True,
                    }
                ],
                "expected_outcome": "retrieval",
                "expect_unsupported": False,
            },
            {
                "topic_id": "unsupported-ewd",
                "information_need_id": "unsupported-fiction",
                "split": "validation",
                "topic_type": "unsupported_premise",
                "language": "en",
                "query": "What is EWD's validated probability of collapse?",
                "active_repositories": ["ewd"],
                "explicit_repositories": ["ewd"],
                "expected_exact": None,
                "judgments": [
                    {
                        "repository_id": "ewd",
                        "logical_source_id": "ewd:section:STATUS.md:lines:1-4",
                        "grade": 0,
                        "required": False,
                    }
                ],
                "expected_outcome": "retrieval",
                "expect_unsupported": True,
            },
            {
                "topic_id": "clarify-cross-repo-ro",
                "information_need_id": "cross-repo-exact-ambiguity",
                "split": "validation",
                "topic_type": "multi_turn_follow_up",
                "language": "ro",
                "query": "Și în RMD?",
                "active_repositories": ["ewd", "rmd"],
                "explicit_repositories": ["rmd"],
                "expected_exact": None,
                "judgments": [],
                "expected_outcome": "needs_clarification",
                "expect_unsupported": False,
                "conversation_id": "exact-anchor-cross-repo",
                "turn_index": 2,
            },
        ],
    }


def run_fixture():
    return {
        "schema_version": 1,
        "benchmark_id": "atm-test-benchmark",
        "run_id": "fixture-run",
        "corpus": [
            {
                "repository_id": "ewd",
                "repository_version": "0.1.0",
                "snapshot_sha": EWD_SHA,
            },
            {
                "repository_id": "rmd",
                "repository_version": "0.1.0",
                "snapshot_sha": RMD_SHA,
            },
        ],
        "topics": [
            {
                "topic_id": "exact-food-en",
                "outcome": "retrieval",
                "latency_ms": 10,
                "results": [
                    evidence(
                        "ewd",
                        "ewd:entity:variable:food_per_capita",
                    ),
                    evidence(
                        "ewd",
                        "ewd:section:STATUS.md:lines:1-4",
                    ),
                ],
                "context_sources": [
                    evidence(
                        "ewd",
                        "ewd:entity:variable:food_per_capita",
                    )
                ],
                "evidence_bytes": 400,
                "evidence_token_count": 100,
            },
            {
                "topic_id": "exact-food-ro",
                "outcome": "retrieval",
                "latency_ms": 20,
                "results": [
                    evidence("rmd", "rmd:section:README.md:lines:1-3"),
                    evidence(
                        "ewd",
                        "ewd:section:STATUS.md:lines:1-4",
                    ),
                    evidence(
                        "ewd",
                        "ewd:entity:variable:food_per_capita",
                    ),
                ],
                "context_sources": [
                    evidence(
                        "ewd",
                        "ewd:section:STATUS.md:lines:1-4",
                    ),
                    evidence("rmd", "rmd:section:README.md:lines:1-3"),
                ],
                "evidence_bytes": 500,
                "evidence_token_count": None,
            },
            {
                "topic_id": "rmd-status-mixed",
                "outcome": "retrieval",
                "latency_ms": 15,
                "results": [
                    evidence(
                        "rmd",
                        "rmd:section:STATUS.md:lines:1-8",
                    )
                ],
                "context_sources": [
                    evidence(
                        "rmd",
                        "rmd:section:STATUS.md:lines:1-8",
                    )
                ],
                "evidence_bytes": 300,
                "evidence_token_count": 80,
            },
            {
                "topic_id": "unsupported-ewd",
                "outcome": "retrieval",
                "latency_ms": 5,
                "results": [],
                "context_sources": [],
                "evidence_bytes": 0,
                "evidence_token_count": 0,
            },
            {
                "topic_id": "clarify-cross-repo-ro",
                "outcome": "needs_clarification",
                "latency_ms": 7,
                "results": [],
                "context_sources": [],
                "evidence_bytes": 0,
                "evidence_token_count": 0,
            },
        ],
    }


class RetrievalBenchmarkEvaluatorTest(unittest.TestCase):
    def test_metrics_and_gates(self):
        result = evaluator.evaluate(
            benchmark_fixture(),
            run_fixture(),
        )
        metrics = result["metrics"]

        self.assertAlmostEqual(
            metrics["exact_id_success_at_1"],
            0.5,
        )
        self.assertAlmostEqual(
            metrics["mrr"],
            (1.0 + 0.5 + 1.0) / 3.0,
        )

        ro_ndcg = (
            1.0 / math.log2(3)
            + 3.0 / math.log2(4)
        ) / (
            3.0 / math.log2(2)
            + 1.0 / math.log2(3)
        )
        self.assertAlmostEqual(
            metrics["ndcg_at_5_ro"],
            ro_ndcg,
        )
        self.assertAlmostEqual(
            metrics["ndcg_at_5_en"],
            1.0,
        )
        self.assertAlmostEqual(
            metrics["ndcg_at_5_mixed"],
            1.0,
        )
        self.assertAlmostEqual(
            metrics["ro_en_ndcg_gap"],
            1.0 - ro_ndcg,
        )
        self.assertAlmostEqual(
            metrics["ndcg_at_5"],
            (1.0 + ro_ndcg + 1.0) / 3.0,
        )
        self.assertAlmostEqual(
            metrics["canonical_required_recall_at_5"],
            1.0,
        )
        self.assertAlmostEqual(
            metrics["context_precision"],
            (1.0 + 0.5 + 1.0) / 3.0,
        )
        self.assertAlmostEqual(
            metrics["wrong_repository_contamination_at_5"],
            1.0 / 6.0,
        )
        self.assertAlmostEqual(
            metrics["evidence_traceability"],
            1.0,
        )
        self.assertEqual(
            metrics["retrieval_latency_ms_median"],
            10.0,
        )
        self.assertEqual(
            metrics["retrieval_latency_ms_p95"],
            20.0,
        )
        self.assertEqual(
            metrics["evidence_bytes_mean"],
            240.0,
        )
        self.assertEqual(
            metrics["evidence_bytes_max"],
            500,
        )
        self.assertEqual(
            metrics["evidence_token_count_mean"],
            45.0,
        )
        self.assertEqual(
            metrics["evidence_token_count_max"],
            100,
        )
        self.assertAlmostEqual(
            metrics["evidence_token_budget_coverage"],
            0.8,
        )
        self.assertEqual(
            metrics["unsupported_empty_context_rate"],
            1.0,
        )
        self.assertEqual(
            metrics["duplicate_result_rate"],
            0.0,
        )
        self.assertEqual(
            metrics["expected_outcome_accuracy"],
            1.0,
        )
        self.assertEqual(
            metrics["clarification_outcome_accuracy"],
            1.0,
        )

        self.assertFalse(result["gates"]["exact_id_success_at_1"])
        self.assertFalse(result["gates"]["ndcg_at_5"])
        self.assertTrue(
            result["gates"]["canonical_required_recall_at_5"]
        )
        self.assertFalse(
            result["gates"]["wrong_repository_contamination_at_5"]
        )
        self.assertFalse(result["gates"]["ro_en_ndcg_gap"])
        self.assertTrue(result["gates"]["evidence_traceability"])
        self.assertTrue(
            result["gates"]["clarification_outcome_accuracy"]
        )
        self.assertFalse(result["passes_provisional_targets"])

    def test_per_topic_diagnostics_expose_required_evidence_ranks(self):
        result = evaluator.evaluate(
            benchmark_fixture(),
            run_fixture(),
        )

        diagnostics = {
            item["topic_id"]: item
            for item in result["topic_diagnostics"]
        }

        english = diagnostics["exact-food-en"]
        self.assertEqual(english["exact_id_success_at_1"], 1.0)
        self.assertEqual(english["reciprocal_rank"], 1.0)
        self.assertEqual(
            english["canonical_required_recall_at_5"],
            1.0,
        )
        self.assertEqual(
            english["required_evidence"],
            [
                {
                    "repository_id": "ewd",
                    "logical_source_id":
                        "ewd:entity:variable:food_per_capita",
                    "grade": 3,
                    "rank": 1,
                    "in_top_k": True,
                }
            ],
        )

        romanian = diagnostics["exact-food-ro"]
        self.assertEqual(romanian["exact_id_success_at_1"], 0.0)
        self.assertAlmostEqual(romanian["reciprocal_rank"], 1.0 / 2.0)
        self.assertEqual(
            romanian["canonical_required_recall_at_5"],
            1.0,
        )
        self.assertEqual(
            romanian["required_evidence"][0]["rank"],
            3,
        )
        self.assertTrue(
            romanian["required_evidence"][0]["in_top_k"]
        )

        unsupported = diagnostics["unsupported-ewd"]
        self.assertIsNone(
            unsupported["canonical_required_recall_at_5"]
        )
        self.assertEqual(
            unsupported["required_evidence"],
            [],
        )

    def test_cross_language_variants_must_share_split(self):
        benchmark = benchmark_fixture()
        benchmark["topics"][1]["split"] = "development"

        with self.assertRaises(evaluator.BenchmarkError):
            evaluator.validate_benchmark(benchmark)

    def test_run_corpus_must_match_pinned_benchmark(self):
        benchmark = benchmark_fixture()
        run = run_fixture()
        run["corpus"][0]["snapshot_sha"] = "3" * 40

        with self.assertRaises(evaluator.BenchmarkError):
            evaluator.validate_run(benchmark, run)

    def test_required_evidence_cannot_be_irrelevant(self):
        benchmark = benchmark_fixture()
        benchmark["topics"][0]["judgments"][0]["grade"] = 0

        with self.assertRaises(evaluator.BenchmarkError):
            evaluator.validate_benchmark(benchmark)

    def test_traceability_detects_wrong_snapshot_metadata(self):
        benchmark = benchmark_fixture()
        run = run_fixture()
        run["topics"][0]["results"][0]["snapshot_sha"] = "9" * 40

        result = evaluator.evaluate(benchmark, run)

        self.assertLess(
            result["metrics"]["evidence_traceability"],
            1.0,
        )
        self.assertFalse(
            result["gates"]["evidence_traceability"]
        )

    def test_clarification_outcome_is_scored(self):
        benchmark = benchmark_fixture()
        run = run_fixture()
        run["topics"][-1]["outcome"] = "retrieval"

        result = evaluator.evaluate(benchmark, run)

        self.assertEqual(
            result["metrics"]["clarification_outcome_accuracy"],
            0.0,
        )
        self.assertFalse(
            result["gates"]["clarification_outcome_accuracy"]
        )

    def test_clarification_is_not_unsupported_evidence(self):
        benchmark = benchmark_fixture()
        benchmark["topics"][-1]["expect_unsupported"] = True

        with self.assertRaises(evaluator.BenchmarkError):
            evaluator.validate_benchmark(benchmark)

    def test_clarification_run_cannot_carry_retrieval_evidence(self):
        benchmark = benchmark_fixture()
        run = run_fixture()
        run["topics"][-1]["results"] = [
            evidence("rmd", "rmd:section:STATUS.md:lines:1-3")
        ]

        with self.assertRaises(evaluator.BenchmarkError):
            evaluator.validate_run(benchmark, run)


if __name__ == "__main__":
    unittest.main()
