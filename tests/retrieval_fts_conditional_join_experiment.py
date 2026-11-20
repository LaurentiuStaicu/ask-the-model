#!/usr/bin/env python3
"""Compare the production NOT EXISTS predicate with two LEFT JOIN forms.

Variant C adds the dataset_row discriminator to every LEFT JOIN ON clause.
This is diagnostic-only: it never changes production SQL.
"""
from __future__ import annotations

import argparse
import statistics
import time
from pathlib import Path

from retrieval_fts_join_population_sweep import create_population, load_queries


REPLACEMENTS = (
    (
        "LEFT JOIN datasets dd ON dd.id = dr.dataset_id",
        "LEFT JOIN datasets dd ON f.evidence_kind = 'dataset_row' AND dd.id = dr.dataset_id",
    ),
    (
        "LEFT JOIN source_files ds ON ds.id = dd.source_id",
        "LEFT JOIN source_files ds ON f.evidence_kind = 'dataset_row' AND ds.id = dd.source_id",
    ),
    (
        "LEFT JOIN snapshot_metadata dm ON dm.id = 1",
        "LEFT JOIN snapshot_metadata dm ON f.evidence_kind = 'dataset_row' AND dm.id = 1",
    ),
)


def conditional_join(candidate: str) -> str:
    result = candidate
    for old, new in REPLACEMENTS:
        if result.count(old) != 1:
            raise RuntimeError(f"Expected exactly one occurrence of {old!r}")
        result = result.replace(old, new, 1)
    if result == candidate:
        raise RuntimeError("Conditional JOIN transformation made no changes")
    return result


def execute(db, sql: str) -> None:
    row = db.execute(sql).fetchone()
    if row is not None:
        raise RuntimeError(f"Expected a valid population, got {row!r}")


def run_case(dataset_rows: int, other_rows: int, queries: tuple[str, str, str], samples: int):
    db = create_population(dataset_rows, other_rows)
    try:
        for label, sql in zip(("A_NOT_EXISTS", "B_LEFT_JOIN", "C_CONDITIONAL_LEFT_JOIN"), queries):
            plan = db.execute("EXPLAIN QUERY PLAN " + sql).fetchall()
            if not plan:
                raise RuntimeError(f"{label}: no query plan")
            execute(db, sql)

        timings = {label: [] for label in ("A_NOT_EXISTS", "B_LEFT_JOIN", "C_CONDITIONAL_LEFT_JOIN")}
        labels = tuple(timings)
        for sample in range(samples):
            order = labels if sample % 2 == 0 else tuple(reversed(labels))
            for label in order:
                started = time.perf_counter_ns()
                execute(db, queries[labels.index(label)])
                timings[label].append((time.perf_counter_ns() - started) / 1000.0)

        medians = {label: statistics.median(values) for label, values in timings.items()}
        total = dataset_rows + other_rows
        share = dataset_rows * 100.0 / total
        print(
            f"CASE dataset_rows={dataset_rows} other_rows={other_rows} "
            f"dataset_share_percent={share:.2f} "
            f"A_median_us={medians[labels[0]]:.2f} "
            f"B_median_us={medians[labels[1]]:.2f} "
            f"C_median_us={medians[labels[2]]:.2f} "
            f"B_vs_A_percent={(medians[labels[1]] / medians[labels[0]] - 1) * 100:+.2f} "
            f"C_vs_A_percent={(medians[labels[2]] / medians[labels[0]] - 1) * 100:+.2f} "
            f"C_vs_B_percent={(medians[labels[2]] / medians[labels[1]] - 1) * 100:+.2f} "
            "outcome=equivalent"
        )
        return medians
    finally:
        db.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--expected-sqlite-prefix", required=True)
    parser.add_argument("--samples", type=int, default=9)
    args = parser.parse_args()

    import sqlite3
    with sqlite3.connect(":memory:") as db:
        version, source_id = db.execute("SELECT sqlite_version(), sqlite_source_id()").fetchone()
    print(f"SQLITE version={version} source_id={source_id}")
    if not version.startswith(args.expected_sqlite_prefix):
        raise RuntimeError(f"Expected SQLite {args.expected_sqlite_prefix}x, got {version}")

    original, candidate = load_queries(args.source)
    conditional = conditional_join(candidate)
    if candidate == original:
        raise RuntimeError("A and B unexpectedly contain identical SQL")
    if conditional == candidate:
        raise RuntimeError("B and C unexpectedly contain identical SQL")

    cases = {
        (11758, 81), (114, 261), (80, 424),
    }
    for dataset_rows in (80, 114, 250, 500, 1000, 2000, 5000, 11758):
        for other_rows in (81, 261, 500, 1500, 3000, 6000):
            cases.add((dataset_rows, other_rows))

    queries = (original, candidate, conditional)
    for dataset_rows, other_rows in sorted(cases):
        run_case(dataset_rows, other_rows, queries, args.samples)
    print(f"SUMMARY cases={len(cases)} variants=3 semantic_equivalence=PASS performance_is_diagnostic_only=true")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
