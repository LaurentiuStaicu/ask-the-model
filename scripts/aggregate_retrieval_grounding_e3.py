#!/usr/bin/env python3
"""Aggregate independent E3 end-to-end retrieval/grounding benchmark runs."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import statistics
from pathlib import Path


RUNNERS = 10
INDEX_RE = re.compile(
    r"^ATM_E3_INDEX repository=(ewd|cbd|rmd) "
    r"ensure=(rebuilt|reused) elapsed_us=(\d+)$",
    re.MULTILINE,
)
PREP_RE = re.compile(r"^ATM_E3_INDEX_PREPARE elapsed_us=(\d+)$", re.MULTILINE)
TOPIC_RE = re.compile(
    r"^ATM_E3_TOPIC topic_id=([^ ]+) pipeline_us=(\d+)$",
    re.MULTILINE,
)


def percentile(values: list[float], p: float) -> float:
    ordered = sorted(values)
    if not ordered:
        raise ValueError("cannot calculate percentile of empty sample")
    position = (len(ordered) - 1) * p
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def summary(values: list[float]) -> dict[str, float]:
    return {
        "samples": len(values),
        "median": statistics.median(values),
        "p95": percentile(values, 0.95),
        "min": min(values),
        "max": max(values),
    }


def semantic_payload(run: dict) -> list[dict]:
    return [
        {
            "topic_id": topic["topic_id"],
            "outcome": topic["outcome"],
            "results": topic["results"],
            "context_sources": topic["context_sources"],
            "evidence_bytes": topic["evidence_bytes"],
        }
        for topic in run["topics"]
    ]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark", required=True, type=Path)
    parser.add_argument("--runs-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    benchmark = json.loads(args.benchmark.read_text(encoding="utf-8"))
    expected_topics = [topic["topic_id"] for topic in benchmark["topics"]]
    expected_corpus = benchmark["corpus"]
    runner_data = []
    semantic_digests = set()
    index_samples = {repo["repository_id"]: [] for repo in expected_corpus}
    index_prepare_samples = []
    pipeline_samples = {topic_id: [] for topic_id in expected_topics}
    retrieval_samples = {topic_id: [] for topic_id in expected_topics}
    context_samples = {topic_id: [] for topic_id in expected_topics}

    for runner_id in range(1, RUNNERS + 1):
        directory = args.runs_dir / f"runner-{runner_id}"
        run_path = directory / "run.json"
        log_path = directory / "runner.log"
        if not run_path.is_file() or not log_path.is_file():
            raise SystemExit(f"missing output for runner {runner_id:02d}")

        run = json.loads(run_path.read_text(encoding="utf-8"))
        log = log_path.read_text(encoding="utf-8")
        if run.get("benchmark_id") != benchmark["benchmark_id"]:
            raise SystemExit(f"runner {runner_id:02d}: benchmark ID mismatch")
        if run.get("corpus") != expected_corpus:
            raise SystemExit(f"runner {runner_id:02d}: pinned corpus mismatch")
        if run.get("run_id") != f"e3-runner-{runner_id:02d}":
            raise SystemExit(f"runner {runner_id:02d}: run identity mismatch")

        topics = run.get("topics", [])
        topic_ids = [topic.get("topic_id") for topic in topics]
        if topic_ids != expected_topics:
            raise SystemExit(f"runner {runner_id:02d}: topic set/order mismatch")

        payload = semantic_payload(run)
        digest = hashlib.sha256(
            json.dumps(payload, sort_keys=True, separators=(",", ":")).encode()
        ).hexdigest()
        semantic_digests.add(digest)

        repo_matches = INDEX_RE.findall(log)
        if len(repo_matches) != len(expected_corpus):
            raise SystemExit(f"runner {runner_id:02d}: expected 3 index timing records")
        seen_repositories = set()
        for repository_id, ensure, elapsed in repo_matches:
            if repository_id in seen_repositories:
                raise SystemExit(f"runner {runner_id:02d}: duplicate index timing")
            if ensure != "rebuilt":
                raise SystemExit(f"runner {runner_id:02d}: expected cold rebuild for {repository_id}, got {ensure}")
            if int(elapsed) <= 0:
                raise SystemExit(f"runner {runner_id:02d}: non-positive index timing for {repository_id}")
            seen_repositories.add(repository_id)
            index_samples[repository_id].append(int(elapsed))
        if seen_repositories != set(index_samples):
            raise SystemExit(f"runner {runner_id:02d}: index repository set mismatch")

        prep_matches = PREP_RE.findall(log)
        if len(prep_matches) != 1:
            raise SystemExit(f"runner {runner_id:02d}: expected one index-preparation timing")
        if int(prep_matches[0]) <= 0:
            raise SystemExit(f"runner {runner_id:02d}: non-positive index preparation timing")
        index_prepare_samples.append(int(prep_matches[0]))

        topic_matches = TOPIC_RE.findall(log)
        if len(topic_matches) != len(expected_topics):
            raise SystemExit(f"runner {runner_id:02d}: expected one pipeline timing per topic")
        topic_timing = {}
        for topic_id, elapsed in topic_matches:
            if topic_id in topic_timing:
                raise SystemExit(f"runner {runner_id:02d}: duplicate topic timing {topic_id}")
            topic_timing[topic_id] = int(elapsed)
        if set(topic_timing) != set(expected_topics):
            raise SystemExit(f"runner {runner_id:02d}: pipeline topic set mismatch")

        for topic in topics:
            topic_id = topic["topic_id"]
            retrieval = topic.get("latency_ms")
            context = topic.get("context_assembly_latency_ms")
            if not isinstance(retrieval, (int, float)) or not isinstance(context, (int, float)):
                raise SystemExit(f"runner {runner_id:02d}: missing stage latency for {topic_id}")
            if topic_timing[topic_id] <= 0:
                raise SystemExit(f"runner {runner_id:02d}: non-positive pipeline timing for {topic_id}")
            pipeline_samples[topic_id].append(topic_timing[topic_id] / 1000.0)
            retrieval_samples[topic_id].append(float(retrieval))
            context_samples[topic_id].append(float(context))

        environment_path = directory / "environment.txt"
        if not environment_path.is_file():
            raise SystemExit(f"missing environment metadata for runner {runner_id:02d}")
        environment = environment_path.read_text(encoding="utf-8").strip()
        if not environment.startswith("SQLite "):
            raise SystemExit(f"runner {runner_id:02d}: invalid SQLite version metadata")
        runner_data.append({"runner_id": runner_id, "semantic_sha256": digest, "sqlite_version": environment.removeprefix("SQLite ")})

    if len(semantic_digests) != 1:
        raise SystemExit("semantic output differs across independent runners")
    sqlite_versions = {item["sqlite_version"] for item in runner_data}
    if len(sqlite_versions) != 1:
        raise SystemExit("SQLite versions differ across independent runners")

    output = {
        "schema_version": 1,
        "experiment": "E3",
        "benchmark_id": benchmark["benchmark_id"],
        "runner_count": RUNNERS,
        "sqlite_version": next(iter(sqlite_versions)),
        "semantic_equivalence": True,
        "semantic_sha256": next(iter(semantic_digests)),
        "index_prepare_ms": summary([value / 1000.0 for value in index_prepare_samples]),
        "index_ensure_ms_by_repository": {
            repository_id: summary([value / 1000.0 for value in values])
            for repository_id, values in index_samples.items()
        },
        "topic_latency_ms": {
            topic_id: {
                "pipeline_end_to_end": summary(pipeline_samples[topic_id]),
                "conversation_retrieval": summary(retrieval_samples[topic_id]),
                "grounding_context_assembly": summary(context_samples[topic_id]),
            }
            for topic_id in expected_topics
        },
        "runners": runner_data,
        "interpretation": {
            "pipeline_end_to_end": "append_topic_run wall time, including conversation retrieval, context assembly, and benchmark serialization",
            "index_prepare": "total repository index ensure phase; per-repository ensure timings are reported separately",
            "retrieval_and_context": "existing R5 stage timers, retained without changing the R5 run schema",
            "scope": "cold-cache full R5 execution per independent runner; host-dependent latency diagnostics, frozen semantic output",
        },
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: E3 aggregate validated {RUNNERS}/{RUNNERS} independent runners.")
    print(f"PASS: semantic equivalence SHA-256 {output['semantic_sha256']}")
    print(f"Index preparation median/p95: {output['index_prepare_ms']['median']:.3f}/{output['index_prepare_ms']['p95']:.3f} ms")
    for repository_id, metrics in output["index_ensure_ms_by_repository"].items():
        print(f"Index ensure {repository_id} median/p95: {metrics['median']:.3f}/{metrics['p95']:.3f} ms")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
