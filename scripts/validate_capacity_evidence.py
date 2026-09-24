#!/usr/bin/env python3

import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "benchmarks" / "capacity-v1" / "evidence.json"
CORPUS_PATH = ROOT / "benchmarks" / "retrieval-v1" / "benchmark.json"

SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise SystemExit(f"capacity evidence validation failed: {message}")


def load_json(path: pathlib.Path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def require_positive_int(record, key, context):
    value = record.get(key)
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        fail(f"{context}.{key} must be a positive integer")
    return value


def require_sha(value, context):
    if not isinstance(value, str) or SHA40.fullmatch(value) is None:
        fail(f"{context} must be a lowercase 40-hex SHA")


def require_digest(value, context):
    if not isinstance(value, str) or SHA256.fullmatch(value) is None:
        fail(f"{context} must be a lowercase 64-hex SHA-256 digest")


def main() -> int:
    evidence = load_json(EVIDENCE_PATH)
    benchmark = load_json(CORPUS_PATH)

    if evidence.get("schema_version") != 1:
        fail("schema_version must remain 1")

    if evidence.get("status") != "qualification-only":
        fail("status must remain qualification-only")

    if evidence.get("production_thresholds_selected") is not False:
        fail("production thresholds must not be selected in C0 evidence")

    if evidence.get("profile_key_contract") != [
        "repository_id",
        "repository_sha",
    ]:
        fail("profile_key_contract must be exact repository ID + SHA")

    if evidence.get("unknown_sha_policy") != (
        "UNQUALIFIED_NO_PROACTIVE_PREDICTION"
    ):
        fail("unknown SHA policy must remain fail-safe and non-inheriting")

    corpus = {
        item["repository_id"]: item["snapshot_sha"]
        for item in benchmark["corpus"]
    }

    if set(corpus) != {"cbd", "ewd", "rmd"}:
        fail("retrieval benchmark corpus is not exactly CBD/EWD/RMD")

    m1 = evidence.get("c0_m1")
    if not isinstance(m1, dict):
        fail("c0_m1 object is missing")

    require_positive_int(m1, "actions_run_id", "c0_m1")
    require_positive_int(m1, "artifact_id", "c0_m1")
    require_digest(
        m1.get("artifact_sha256"),
        "c0_m1.artifact_sha256",
    )
    require_sha(
        m1.get("atm_source_commit"),
        "c0_m1.atm_source_commit",
    )

    observations = m1.get("observations")
    if not isinstance(observations, list):
        fail("c0_m1.observations must be a list")

    by_repo = {}
    metric_keys = (
        "archive_allocated_bytes",
        "snapshot_allocated_bytes",
        "final_index_allocated_bytes",
        "fresh_data_additional_peak_bytes",
        "index_cache_additional_peak_bytes",
        "same_sha_repair_data_additional_peak_bytes",
        "same_device_operation_peak_bytes",
        "same_device_operation_peak_entries",
    )

    for record in observations:
        if not isinstance(record, dict):
            fail("each M1 observation must be an object")

        repository_id = record.get("repository_id")
        repository_sha = record.get("repository_sha")

        if repository_id in by_repo:
            fail(f"duplicate M1 observation for {repository_id!r}")

        if repository_id not in corpus:
            fail(f"unexpected M1 repository {repository_id!r}")

        require_sha(
            repository_sha,
            f"c0_m1.observations[{repository_id}].repository_sha",
        )

        if repository_sha != corpus[repository_id]:
            fail(
                f"{repository_id} evidence SHA does not match "
                "retrieval-v1 pinned corpus"
            )

        for key in metric_keys:
            require_positive_int(
                record,
                key,
                f"c0_m1.observations[{repository_id}]",
            )

        by_repo[repository_id] = record

    if set(by_repo) != set(corpus):
        fail("M1 evidence does not contain exactly CBD/EWD/RMD")

    for qualification in ("c0_e1", "c0_e2", "c0_e3"):
        record = evidence.get(qualification)
        if not isinstance(record, dict):
            fail(f"{qualification} object is missing")

        require_positive_int(
            record,
            "actions_run_id",
            qualification,
        )
        require_positive_int(
            record,
            "artifact_id",
            qualification,
        )
        require_digest(
            record.get("artifact_sha256"),
            f"{qualification}.artifact_sha256",
        )

    e2 = evidence["c0_e2"]
    if e2.get("pinned_repository_id") != "cbd":
        fail("C0-E2 must remain tied to the qualified CBD inode scenario")
    if e2.get("pinned_repository_sha") != corpus["cbd"]:
        fail("C0-E2 CBD SHA drifted from the pinned corpus")

    for measurement in ("c0_m2", "c0_m3"):
        record = evidence.get(measurement)
        if not isinstance(record, dict):
            fail(f"{measurement} object is missing")

        require_positive_int(
            record,
            "actions_run_id",
            measurement,
        )
        require_positive_int(
            record,
            "artifact_id",
            measurement,
        )
        require_digest(
            record.get("artifact_sha256"),
            f"{measurement}.artifact_sha256",
        )
        require_sha(
            record.get("atm_source_commit"),
            f"{measurement}.atm_source_commit",
        )

        observations = record.get("observations")
        if not isinstance(observations, list) or not observations:
            fail(f"{measurement}.observations must be a non-empty list")

        for index, observation in enumerate(observations):
            if not isinstance(observation, dict):
                fail(
                    f"{measurement}.observations[{index}] "
                    "must be an object"
                )

            context = f"{measurement}.observations[{index}]"
            require_positive_int(
                observation,
                "history_generations",
                context,
            )
            require_positive_int(
                observation,
                "control_db_allocated_bytes",
                context,
            )
            require_positive_int(
                observation,
                "available_bytes_before_operation",
                context,
            )

            classification = observation.get("classification")
            if classification not in {
                "SUCCESS_ATOMIC",
                "FAIL_CLOSED_OLD_AUTHORITY",
            }:
                fail(
                    f"{context}.classification is not an "
                    "accepted atomic outcome"
                )

            sqlite_result = observation.get("sqlite_result")
            if classification == "SUCCESS_ATOMIC":
                if sqlite_result is not None:
                    fail(
                        f"{context} successful observation "
                        "must not carry a SQLite failure result"
                    )
            elif sqlite_result not in {
                "SQLITE_IOERR",
                "SQLITE_FULL",
            }:
                fail(
                    f"{context} fail-closed observation lost "
                    "its SQLite failure classification"
                )

        frontier = record.get("observed_frontier")
        if not isinstance(frontier, dict):
            fail(f"{measurement}.observed_frontier is missing")

        highest_failed = require_positive_int(
            frontier,
            "highest_failed_available_bytes",
            f"{measurement}.observed_frontier",
        )
        lowest_success = require_positive_int(
            frontier,
            "lowest_successful_available_bytes",
            f"{measurement}.observed_frontier",
        )
        if highest_failed >= lowest_success:
            fail(
                f"{measurement} observed frontier is not ordered"
            )

    m2 = evidence["c0_m2"]
    if m2.get("filesystem") != {
        "type": "tmpfs",
        "size_bytes": 33554432,
        "inode_limit": 4096,
    }:
        fail("C0-M2 filesystem qualification contract drifted")

    expected_m2 = {
        (1, 16384, "FAIL_CLOSED_OLD_AUTHORITY"),
        (1, 32768, "FAIL_CLOSED_OLD_AUTHORITY"),
        (1, 65536, "SUCCESS_ATOMIC"),
        (1, 131072, "SUCCESS_ATOMIC"),
        (100, 16384, "FAIL_CLOSED_OLD_AUTHORITY"),
        (100, 32768, "FAIL_CLOSED_OLD_AUTHORITY"),
        (100, 65536, "SUCCESS_ATOMIC"),
        (100, 131072, "SUCCESS_ATOMIC"),
        (1000, 16384, "FAIL_CLOSED_OLD_AUTHORITY"),
        (1000, 32768, "FAIL_CLOSED_OLD_AUTHORITY"),
        (1000, 65536, "SUCCESS_ATOMIC"),
        (1000, 131072, "SUCCESS_ATOMIC"),
    }
    observed_m2 = {
        (
            item["history_generations"],
            item["available_bytes_before_operation"],
            item["classification"],
        )
        for item in m2["observations"]
    }
    if observed_m2 != expected_m2:
        fail("C0-M2 reviewed state-headroom matrix drifted")

    if m2.get("observed_frontier") != {
        "highest_failed_available_bytes": 32768,
        "lowest_successful_available_bytes": 65536,
    }:
        fail("C0-M2 reviewed frontier drifted")

    m3 = evidence["c0_m3"]
    if m3.get("filesystem") != {
        "type": "ext4",
        "image_size_bytes": 67108864,
        "block_size_bytes": 4096,
        "reserved_blocks_percentage": 0,
    }:
        fail("C0-M3 ext4 qualification contract drifted")

    expected_m3 = {
        (1, 12288, "FAIL_CLOSED_OLD_AUTHORITY"),
        (1, 32768, "FAIL_CLOSED_OLD_AUTHORITY"),
        (1, 61440, "SUCCESS_ATOMIC"),
        (1, 131072, "SUCCESS_ATOMIC"),
        (1000, 12288, "FAIL_CLOSED_OLD_AUTHORITY"),
        (1000, 28672, "FAIL_CLOSED_OLD_AUTHORITY"),
        (1000, 61440, "SUCCESS_ATOMIC"),
        (1000, 126976, "SUCCESS_ATOMIC"),
    }
    observed_m3 = {
        (
            item["history_generations"],
            item["available_bytes_before_operation"],
            item["classification"],
        )
        for item in m3["observations"]
    }
    if observed_m3 != expected_m3:
        fail("C0-M3 reviewed ext4 state-headroom matrix drifted")

    if m3.get("observed_frontier") != {
        "highest_failed_available_bytes": 32768,
        "lowest_successful_available_bytes": 61440,
    }:
        fail("C0-M3 reviewed frontier drifted")

    rules = evidence.get("interpretation_rules")
    if not isinstance(rules, list) or len(rules) < 6:
        fail("interpretation_rules are incomplete")

    joined_rules = "\n".join(str(item) for item in rules).lower()
    for required_phrase in (
        "not filesystem-independent upper bounds",
        "exact repository_id and repository_sha",
        "unknown repository sha",
        "no fixed percentage",
    ):
        if required_phrase not in joined_rules:
            fail(
                f"interpretation rules lost required contract: "
                f"{required_phrase}"
            )

    print(
        "capacity evidence validation passed: "
        "exact-SHA corpus + provenance + non-threshold policy"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
