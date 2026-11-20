#!/usr/bin/env python3
"""Validate and aggregate E2 SQLite query-session benchmark runs."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path


def percentile(values: list[int], fraction: float) -> float:
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    low, high = math.floor(position), math.ceil(position)
    if low == high:
        return float(ordered[low])
    weight = position - low
    return ordered[low] * (1.0 - weight) + ordered[high] * weight


def load_runs(directory: Path, expected: int) -> list[dict]:
    paths = sorted(directory.glob("*.json"))
    if len(paths) != expected:
        raise SystemExit(f"Expected {expected} E2 runner files; found {len(paths)}.")
    runs = []
    for path in paths:
        try:
            run = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise SystemExit(f"{path}: {exc}") from exc
        required = {
            "schema_version", "benchmark_id", "run_id", "sqlite_version",
            "session_open_samples_per_repository", "query_samples_per_mode",
            "repositories", "runner_identity", "runner_matrix_id",
        }
        missing = required - run.keys()
        if missing:
            raise SystemExit(f"{path}: missing {sorted(missing)}")
        if run["schema_version"] != 1 or run["benchmark_id"] != "atm-retrieval-query-session-e2":
            raise SystemExit(f"{path}: unsupported E2 schema or benchmark.")
        if run["session_open_samples_per_repository"] != 15 or run["query_samples_per_mode"] != 15:
            raise SystemExit(f"{path}: unexpected E2 sample counts.")
        if len(run["repositories"]) != 3:
            raise SystemExit(f"{path}: expected three repositories.")
        runs.append(run)

    identities = [r["runner_identity"] for r in runs]
    if any(not isinstance(x, str) or len(x) != 64 for x in identities):
        raise SystemExit("Invalid E2 runner identity.")
    if len(set(identities)) != expected:
        raise SystemExit("E2 runner identities are not distinct.")
    matrix_ids = [r["runner_matrix_id"] for r in runs]
    if len(set(matrix_ids)) != expected:
        raise SystemExit("E2 runner matrix IDs are not distinct.")
    sqlite_versions = {r["sqlite_version"] for r in runs}
    if len(sqlite_versions) != 1:
        raise SystemExit(f"SQLite version mismatch: {sqlite_versions}")

    for run in runs:
        repos = {r["repository_id"]: r for r in run["repositories"]}
        if set(repos) != {"ewd", "cbd", "rmd"}:
            raise SystemExit(f"{run['run_id']}: repository set mismatch.")
        for repo_id, repo in repos.items():
            if len(repo.get("session_open_and_validate_us", [])) != 15:
                raise SystemExit(f"{run['run_id']}/{repo_id}: invalid session-open count.")
            if any(not isinstance(x, int) or x <= 0 for x in repo["session_open_and_validate_us"]):
                raise SystemExit(f"{run['run_id']}/{repo_id}: invalid session-open values.")
            expected_queries = {"exact", "fts"}
            if repo_id in {"ewd", "cbd"}:
                expected_queries.add("dataset_rows")
            queries = {q["query_kind"]: q for q in repo.get("queries", [])}
            if set(queries) != expected_queries:
                raise SystemExit(f"{run['run_id']}/{repo_id}: query route set mismatch.")
            for kind, query in queries.items():
                if query.get("result_equivalence") != "identical":
                    raise SystemExit(f"{run['run_id']}/{repo_id}/{kind}: semantic mismatch.")
                if len(query.get("persistent_session_query_us", [])) != 15:
                    raise SystemExit(f"{run['run_id']}/{repo_id}/{kind}: invalid persistent sample count.")
                if len(query.get("one_shot_open_query_close_us", [])) != 15:
                    raise SystemExit(f"{run['run_id']}/{repo_id}/{kind}: invalid one-shot sample count.")
                if not query.get("semantic_sha256"):
                    raise SystemExit(f"{run['run_id']}/{repo_id}/{kind}: missing semantic digest.")
                for field in ("persistent_session_query_us", "one_shot_open_query_close_us"):
                    values = query[field]
                    if any(not isinstance(x, int) or x <= 0 for x in values):
                        raise SystemExit(f"{run['run_id']}/{repo_id}/{kind}: invalid {field}.")
    return runs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", required=True, type=Path)
    parser.add_argument("--expected-runners", required=True, type=int)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    runs = load_runs(args.input_dir, args.expected_runners)
    result = {
        "schema_version": 1,
        "benchmark_id": "atm-retrieval-query-session-e2",
        "runner_count": len(runs),
        "distinct_runner_identities": len({r["runner_identity"] for r in runs}),
        "sqlite_version": runs[0]["sqlite_version"],
        "repositories": [],
    }

    for repo_id in ("ewd", "cbd", "rmd"):
        repo_runs = [
            next(x for x in r["repositories"] if x["repository_id"] == repo_id)
            for r in runs
        ]
        versions = {r["repository_version"] for r in repo_runs}
        shas = {r["snapshot_sha"] for r in repo_runs}
        if len(versions) != 1 or len(shas) != 1:
            raise SystemExit(f"{repo_id}: pinned repository identity mismatch.")
        opens = [v for r in repo_runs for v in r["session_open_and_validate_us"]]
        query_summaries = []
        kinds = ("exact", "fts", "dataset_rows") if repo_id in ("ewd", "cbd") else ("exact", "fts")
        for kind in kinds:
            items = [
                next(q for q in r["queries"] if q["query_kind"] == kind)
                for r in repo_runs
            ]
            digests = {q["semantic_sha256"] for q in items}
            if len(digests) != 1:
                raise SystemExit(f"{repo_id}/{kind}: semantic digest differs across runners.")
            persistent = [v for q in items for v in q["persistent_session_query_us"]]
            one_shot = [v for q in items for v in q["one_shot_open_query_close_us"]]
            query_summaries.append({
                "query_kind": kind,
                "semantic_sha256": next(iter(digests)),
                "persistent_sample_count": len(persistent),
                "one_shot_sample_count": len(one_shot),
                "persistent_query_median_us": statistics.median(persistent),
                "persistent_query_p95_us": percentile(persistent, 0.95),
                "one_shot_open_query_close_median_us": statistics.median(one_shot),
                "one_shot_open_query_close_p95_us": percentile(one_shot, 0.95),
            })
        result["repositories"].append({
            "repository_id": repo_id,
            "repository_version": next(iter(versions)),
            "snapshot_sha": next(iter(shas)),
            "session_open_sample_count": len(opens),
            "session_open_and_validate_median_us": statistics.median(opens),
            "session_open_and_validate_p95_us": percentile(opens, 0.95),
            "queries": query_summaries,
        })

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(f"PASS: E2 aggregate validated {result['distinct_runner_identities']}/{args.expected_runners} distinct runners.")
    for repo in result["repositories"]:
        print(f"{repo['repository_id']}: session open median={repo['session_open_and_validate_median_us']:.1f}us p95={repo['session_open_and_validate_p95_us']:.1f}us")
        for query in repo["queries"]:
            print(
                f"  {query['query_kind']}: persistent median={query['persistent_query_median_us']:.1f}us "
                f"p95={query['persistent_query_p95_us']:.1f}us; "
                f"one-shot median={query['one_shot_open_query_close_median_us']:.1f}us "
                f"p95={query['one_shot_open_query_close_p95_us']:.1f}us"
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
