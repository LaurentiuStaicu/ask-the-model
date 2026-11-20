#!/usr/bin/env python3
"""Validate and aggregate independent E1 retrieval-index lifecycle runs."""

from __future__ import annotations

import argparse
import json
import math
import statistics
from pathlib import Path


def percentile(values: list[int], fraction: float) -> float:
    ordered = sorted(values)
    if len(ordered) == 1:
        return float(ordered[0])
    position = (len(ordered) - 1) * fraction
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return float(ordered[lower])
    weight = position - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def load_runs(directory: Path, expected: int) -> list[dict]:
    paths = sorted(directory.glob("*.json"))
    if len(paths) != expected:
        raise SystemExit(
            f"Expected exactly {expected} runner JSON files; found {len(paths)}."
        )
    runs = []
    for path in paths:
        try:
            run = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as exc:
            raise SystemExit(f"Cannot read {path}: {exc}") from exc
        required = {
            "schema_version", "benchmark_id", "run_id", "sqlite_version",
            "cold_samples_per_repository", "warm_samples_per_repository",
            "repositories", "runner_identity", "runner_matrix_id",
        }
        missing = required - run.keys()
        if missing:
            raise SystemExit(f"{path}: missing fields {sorted(missing)}")
        if run["schema_version"] != 1:
            raise SystemExit(f"{path}: unsupported schema version.")
        if run["benchmark_id"] != "atm-retrieval-lifecycle-e1":
            raise SystemExit(f"{path}: unexpected benchmark ID.")
        if run["cold_samples_per_repository"] != 5:
            raise SystemExit(f"{path}: unexpected cold sample count.")
        if run["warm_samples_per_repository"] != 15:
            raise SystemExit(f"{path}: unexpected warm sample count.")
        if not isinstance(run["runner_identity"], str) or len(run["runner_identity"]) != 64:
            raise SystemExit(f"{path}: invalid runner identity.")
        if len(run["repositories"]) != 3:
            raise SystemExit(f"{path}: expected exactly three repositories.")
        runs.append(run)

    identities = [run["runner_identity"] for run in runs]
    if len(set(identities)) != expected:
        raise SystemExit(
            f"Runner identity collision: {len(set(identities))}/{expected} unique."
        )
    matrix_ids = [run["runner_matrix_id"] for run in runs]
    if len(set(matrix_ids)) != expected:
        raise SystemExit("Runner matrix IDs are not unique.")
    sqlite_versions = {run["sqlite_version"] for run in runs}
    if len(sqlite_versions) != 1:
        raise SystemExit(f"SQLite version mismatch across runners: {sqlite_versions}")

    expected_repositories = {"ewd", "cbd", "rmd"}
    for run in runs:
        repositories = {item["repository_id"]: item for item in run["repositories"]}
        if set(repositories) != expected_repositories:
            raise SystemExit(
                f"{run['run_id']}: repository set mismatch: {sorted(repositories)}"
            )
        for repository_id, item in repositories.items():
            if item.get("cold_classification") != "rebuilt":
                raise SystemExit(f"{run['run_id']}/{repository_id}: cold not rebuilt.")
            if item.get("warm_classification") != "reused":
                raise SystemExit(f"{run['run_id']}/{repository_id}: warm not reused.")
            for field, count in (("cold_ensure_us", 5), ("warm_ensure_us", 15)):
                values = item.get(field)
                if not isinstance(values, list) or len(values) != count:
                    raise SystemExit(
                        f"{run['run_id']}/{repository_id}: invalid {field} sample count."
                    )
                if any(not isinstance(value, int) or value <= 0 for value in values):
                    raise SystemExit(
                        f"{run['run_id']}/{repository_id}: invalid {field} values."
                    )
    return runs


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input-dir", required=True, type=Path)
    parser.add_argument("--expected-runners", required=True, type=int)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    runs = load_runs(args.input_dir, args.expected_runners)
    summary = {
        "schema_version": 1,
        "benchmark_id": "atm-retrieval-lifecycle-e1",
        "runner_count": len(runs),
        "distinct_runner_identities": len({run["runner_identity"] for run in runs}),
        "sqlite_version": runs[0]["sqlite_version"],
        "repositories": [],
        "runner_identities": sorted(run["runner_identity"] for run in runs),
    }

    for repository_id in ("ewd", "cbd", "rmd"):
        cold = []
        warm = []
        versions = set()
        for run in runs:
            item = next(
                entry for entry in run["repositories"]
                if entry["repository_id"] == repository_id
            )
            cold.extend(item["cold_ensure_us"])
            warm.extend(item["warm_ensure_us"])
            versions.add(item["repository_version"])
        if len(versions) != 1:
            raise SystemExit(
                f"Repository version mismatch for {repository_id}: {versions}"
            )
        summary["repositories"].append({
            "repository_id": repository_id,
            "repository_version": next(iter(versions)),
            "cold_sample_count": len(cold),
            "warm_sample_count": len(warm),
            "cold_ensure_median_us": statistics.median(cold),
            "cold_ensure_p95_us": percentile(cold, 0.95),
            "warm_ensure_median_us": statistics.median(warm),
            "warm_ensure_p95_us": percentile(warm, 0.95),
        })

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(summary, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    print(
        "PASS: E1 aggregate validated "
        f"{summary['distinct_runner_identities']}/{args.expected_runners} "
        "distinct runner identities."
    )
    for item in summary["repositories"]:
        print(
            f"{item['repository_id']}: "
            f"cold median={item['cold_ensure_median_us']:.1f}us "
            f"p95={item['cold_ensure_p95_us']:.1f}us; "
            f"warm median={item['warm_ensure_median_us']:.1f}us "
            f"p95={item['warm_ensure_p95_us']:.1f}us"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
