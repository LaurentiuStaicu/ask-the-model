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
