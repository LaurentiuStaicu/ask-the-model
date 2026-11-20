#!/usr/bin/env python3
"""Paired synthetic population sweep for the FTS canonical validator predicates.

The SQL is extracted from retrieval_fts_consistency.c rather than copied into
this script. The candidate diagnostic query must exactly equal the production
validator query before any measurement is accepted.
"""

from __future__ import annotations

import argparse
import ast
import re
import sqlite3
import statistics
import time
from pathlib import Path


def extract_c_initializer(source: str, marker: str, terminator: str) -> str:
    start = source.index(marker) + len(marker)
    end = source.index(terminator, start)
    if terminator == '";':
        end += 1  # retain the closing quote of the final C string literal
    initializer = source[start:end]
    literals = re.findall(r'"(?:\\.|[^"\\])*"', initializer)
    if not literals:
        raise RuntimeError(f"No C string literals found after {marker!r}")
    return "".join(ast.literal_eval(literal) for literal in literals)


def load_queries(source_path: Path) -> tuple[str, str]:
    source = source_path.read_text(encoding="utf-8")
    diagnostic_start = source.index("measure_fts_canonical_predicate_ab")
    diagnostic_source = source[diagnostic_start:]
    original = extract_c_initializer(
        diagnostic_source, "static const char *original_sql =", '";'
    )
    candidate = extract_c_initializer(
        diagnostic_source, "static const char *candidate_sql =", '";'
    )
    production = extract_c_initializer(
        source, "const char *queries[] = {", "};"
    )
    if candidate != production:
        raise RuntimeError(
            "The paired diagnostic candidate SQL differs from the "
            "production validator SQL; refusing to benchmark stale SQL."
        )
    return original, candidate


def create_population(dataset_rows: int, other_rows: int) -> sqlite3.Connection:
    db = sqlite3.connect(":memory:")
    db.execute("PRAGMA foreign_keys = ON")
    db.executescript(
        """
        CREATE TABLE snapshot_metadata (
            id INTEGER PRIMARY KEY CHECK (id = 1),
            repository_id TEXT NOT NULL
        );
        INSERT INTO snapshot_metadata VALUES (1, 'synthetic');

        CREATE TABLE source_files (
            id INTEGER PRIMARY KEY,
            path TEXT NOT NULL UNIQUE
        );
        INSERT INTO source_files VALUES (1, 'synthetic/data.csv');

        CREATE TABLE datasets (
            id INTEGER PRIMARY KEY,
            source_id INTEGER NOT NULL REFERENCES source_files(id)
        );
        INSERT INTO datasets VALUES (1, 1);

        CREATE TABLE dataset_rows (
            id INTEGER PRIMARY KEY,
            dataset_id INTEGER NOT NULL REFERENCES datasets(id),
            ordinal INTEGER NOT NULL,
            search_text TEXT,
            UNIQUE (dataset_id, ordinal)
        );

        CREATE TABLE document_sections (
            id INTEGER PRIMARY KEY,
            logical_source_id TEXT NOT NULL UNIQUE,
            title TEXT,
            body TEXT NOT NULL
        );

        CREATE TABLE structured_entities (
            id INTEGER PRIMARY KEY,
            logical_source_id TEXT NOT NULL UNIQUE,
            native_id TEXT,
            label TEXT,
            payload_json TEXT NOT NULL
        );

        CREATE TABLE structured_relations (
            id INTEGER PRIMARY KEY,
            logical_source_id TEXT NOT NULL UNIQUE,
            native_id TEXT,
            relation_type TEXT NOT NULL,
            payload_json TEXT NOT NULL
        );

        CREATE VIRTUAL TABLE search_fts USING fts5(
            evidence_kind UNINDEXED,
            evidence_id UNINDEXED,
            logical_source_id UNINDEXED,
            title,
            body,
            tokenize = 'unicode61 remove_diacritics 2'
        );
        """
    )

    dataset_values = [
        (row_id, 1, row_id - 1, f"dataset row {row_id} synthetic value")
        for row_id in range(1, dataset_rows + 1)
    ]
    db.executemany(
        "INSERT INTO dataset_rows VALUES (?, ?, ?, ?)", dataset_values
    )
    db.executemany(
        "INSERT INTO search_fts VALUES (?, ?, ?, ?, ?)",
        [
            (
                "dataset_row",
                row_id,
                f"synthetic:dataset-row:synthetic/data.csv:{row_id - 1}",
                "synthetic/data.csv",
                f"dataset row {row_id} synthetic value",
            )
            for row_id in range(1, dataset_rows + 1)
        ],
    )

    section_count = other_rows // 3
    entity_count = other_rows // 3
    relation_count = other_rows - section_count - entity_count

    db.executemany(
        "INSERT INTO document_sections VALUES (?, ?, ?, ?)",
        [
            (row_id, f"synthetic:section:{row_id}", f"section {row_id}", f"section body {row_id}")
            for row_id in range(1, section_count + 1)
        ],
    )
    db.executemany(
        "INSERT INTO search_fts VALUES (?, ?, ?, ?, ?)",
        [
            ("section", row_id, f"synthetic:section:{row_id}", f"section {row_id}", f"section body {row_id}")
            for row_id in range(1, section_count + 1)
        ],
    )

    db.executemany(
        "INSERT INTO structured_entities VALUES (?, ?, ?, ?, ?)",
        [
            (row_id, f"synthetic:entity:{row_id}", f"entity-native-{row_id}", f"entity {row_id}", "{}")
            for row_id in range(1, entity_count + 1)
        ],
    )
    db.executemany(
        "INSERT INTO search_fts VALUES (?, ?, ?, ?, ?)",
        [
            ("entity", row_id, f"synthetic:entity:{row_id}", f"entity {row_id}", "{}")
            for row_id in range(1, entity_count + 1)
        ],
    )

    db.executemany(
        "INSERT INTO structured_relations VALUES (?, ?, ?, ?, ?)",
        [
            (row_id, f"synthetic:relation:{row_id}", f"relation-native-{row_id}", f"relation-type-{row_id}", "{}")
            for row_id in range(1, relation_count + 1)
        ],
    )
    db.executemany(
        "INSERT INTO search_fts VALUES (?, ?, ?, ?, ?)",
        [
            ("relation", row_id, f"synthetic:relation:{row_id}", f"relation-native-{row_id}", "{}")
            for row_id in range(1, relation_count + 1)
        ],
    )

    violations = db.execute("PRAGMA foreign_key_check").fetchall()
    if violations:
        raise RuntimeError(f"Synthetic population has FK violations: {violations}")
    return db


def execute_valid_query(db: sqlite3.Connection, sql: str) -> None:
    row = db.execute(sql).fetchone()
    if row is not None:
        raise RuntimeError(f"Validator query unexpectedly reported mismatch: {row}")


def median_timing(
    db: sqlite3.Connection, sql: str, samples: int
) -> list[float]:
    timings = []
    for _ in range(samples):
        started = time.perf_counter_ns()
        execute_valid_query(db, sql)
        timings.append((time.perf_counter_ns() - started) / 1000.0)
    return timings


def run_case(
    dataset_rows: int,
    other_rows: int,
    original_sql: str,
    candidate_sql: str,
    samples: int,
) -> dict[str, float | int | str]:
    db = create_population(dataset_rows, other_rows)
    try:
        # Confirm both plans can be prepared and the valid population is
        # accepted before collecting timing samples.
        original_plan = db.execute(
            "EXPLAIN QUERY PLAN " + original_sql
        ).fetchall()
        candidate_plan = db.execute(
            "EXPLAIN QUERY PLAN " + candidate_sql
        ).fetchall()
        if not original_plan or not candidate_plan:
            raise RuntimeError("A validator query plan was not produced")

        execute_valid_query(db, original_sql)
        execute_valid_query(db, candidate_sql)

        original_times: list[float] = []
        candidate_times: list[float] = []
        for sample in range(samples):
            order = (
                ((original_sql, original_times), (candidate_sql, candidate_times))
                if sample % 2 == 0
                else ((candidate_sql, candidate_times), (original_sql, original_times))
            )
            for sql, target in order:
                started = time.perf_counter_ns()
                execute_valid_query(db, sql)
                target.append((time.perf_counter_ns() - started) / 1000.0)

        original_median = statistics.median(original_times)
        candidate_median = statistics.median(candidate_times)
        delta_percent = (
            (candidate_median / original_median) - 1.0
        ) * 100.0
        total = dataset_rows + other_rows
        return {
            "dataset_rows": dataset_rows,
            "other_rows": other_rows,
            "dataset_share_percent": (dataset_rows / total) * 100.0,
            "original_median_us": original_median,
            "candidate_median_us": candidate_median,
            "delta_percent": delta_percent,
            "outcome": "equivalent",
        }
    finally:
        db.close()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--expected-sqlite-prefix", required=True)
    parser.add_argument("--samples", type=int, default=9)
    args = parser.parse_args()

    version, source_id = sqlite3.connect(":memory:").execute(
        "SELECT sqlite_version(), sqlite_source_id()"
    ).fetchone()
    print(f"SQLITE version={version} source_id={source_id}")
    if not version.startswith(args.expected_sqlite_prefix):
        raise RuntimeError(
            f"Expected SQLite {args.expected_sqlite_prefix}x, got {version}"
        )

    original_sql, candidate_sql = load_queries(args.source)
    print("SQL contract: paired diagnostic candidate equals production validator")

    # Include the three real-corpus population shapes observed in the pinned
    # EWD/CBD/RMD snapshots, then sweep both dimensions independently.
    cases = {
        (11758, 81),   # EWD: 11,758 dataset rows, 81 other FTS rows
        (114, 261),    # CBD: 114 dataset rows, 261 other FTS rows
        (80, 424),     # RMD: 80 dataset rows, 424 other FTS rows
    }
    for dataset_rows in (80, 114, 250, 500, 1000, 2000, 5000, 11758):
        for other_rows in (81, 261, 500, 1500, 3000, 6000):
            cases.add((dataset_rows, other_rows))

    results = []
    for dataset_rows, other_rows in sorted(cases):
        result = run_case(
            dataset_rows,
            other_rows,
            original_sql,
            candidate_sql,
            args.samples,
        )
        results.append(result)
        print(
            "SWEEP "
            f"dataset_rows={dataset_rows} "
            f"other_rows={other_rows} "
            f"dataset_share_percent={result['dataset_share_percent']:.2f} "
            f"original_median_us={result['original_median_us']:.2f} "
            f"candidate_median_us={result['candidate_median_us']:.2f} "
            f"delta_percent={result['delta_percent']:+.2f} "
            f"outcome={result['outcome']}"
        )

    wins = sum(1 for result in results if float(result["delta_percent"]) < 0)
    losses = len(results) - wins
    print(
        f"SUMMARY cases={len(results)} candidate_wins={wins} "
        f"candidate_losses_or_ties={losses} "
        "performance_is_diagnostic_only=true"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
