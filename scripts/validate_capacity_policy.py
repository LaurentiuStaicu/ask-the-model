#!/usr/bin/env python3

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY_PATH = ROOT / "qualification" / "capacity-policy-v1.json"
V1_PATH = ROOT / "benchmarks" / "capacity-v1" / "evidence.json"
V2_PATH = ROOT / "benchmarks" / "capacity-v2" / "evidence.json"
V3_PATH = ROOT / "benchmarks" / "capacity-v3" / "evidence.json"
PROFILE_INC_PATH = ROOT / "src" / "repository_capacity_profiles.inc"
POLICY_HEADER_PATH = ROOT / "src" / "repository_capacity_policy.h"

SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise SystemExit(f"capacity production policy validation failed: {message}")


def load(path: pathlib.Path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    policy = load(POLICY_PATH)
    v1 = load(V1_PATH)
    v2 = load(V2_PATH)
    v3 = load(V3_PATH)

    if policy.get("schema_version") != 1:
        fail("schema_version must remain 1")
    if policy.get("status") != "selected-not-wired":
        fail("policy must remain selected-not-wired in F10")
    if policy.get("runtime_integration_selected") is not False:
        fail("F10 must not activate runtime admission")

    gate = policy.get("runtime_gate")
    if gate != {
        "name": "Optimizations",
        "default_enabled": False,
        "operation_snapshot_required": True,
    }:
        fail("runtime gate contract drifted")

    byte_policy = policy.get("byte_prediction")
    if not isinstance(byte_policy, dict):
        fail("byte_prediction object is missing")
    if byte_policy.get("strategy") != "EXACT_PROFILE_ONLY":
        fail("production byte strategy must remain exact-profile-only")
    if byte_policy.get("exact_profile_source") != (
        "benchmarks/capacity-v1/evidence.json"
    ):
        fail("exact profile source drifted")
    if byte_policy.get("profile_key") != [
        "repository_id",
        "repository_sha",
    ]:
        fail("exact profile key must remain repository ID + SHA")

    expected_mapping = {
        "pre_download_archive_bytes": "archive_allocated_bytes",
        "fresh_install_snapshot_bytes": "fresh_data_additional_peak_bytes",
        "different_sha_update_snapshot_bytes": "snapshot_allocated_bytes",
        "same_sha_repair_snapshot_bytes": (
            "same_sha_repair_data_additional_peak_bytes"
        ),
        "index_build_bytes": "index_cache_additional_peak_bytes",
    }
    if byte_policy.get("field_mapping") != expected_mapping:
        fail("exact-profile field mapping drifted")

    unknown = byte_policy.get("unknown_sha")
    if unknown != {
        "data_cache_byte_rejection": "DISABLED_UNQUALIFIED",
        "inode_admission": "ENFORCE_F2_AND_STRUCTURAL",
        "state_headroom": "ENFORCE",
        "post_admission_exhaustion": "FAIL_CLOSED",
    }:
        fail("unknown-SHA fallback contract drifted")

    hybrid = byte_policy.get("historical_hybrid")
    if not isinstance(hybrid, dict) or hybrid.get("selected") is not False:
        fail("historical hybrid must remain rejected")
    if hybrid.get("evidence_source") != "benchmarks/capacity-v2/evidence.json":
        fail("M6 evidence source drifted")
    if hybrid.get("holdout_source") != "benchmarks/capacity-v3/evidence.json":
        fail("M5 holdout source drifted")

    if v1.get("status") != "qualification-only":
        fail("capacity-v1 evidence status drifted")
    if v1.get("production_thresholds_selected") is not False:
        fail("F10 must not rewrite historical C0 evidence as production policy")
    if v1.get("unknown_sha_policy") != (
        "UNQUALIFIED_NO_PROACTIVE_PREDICTION"
    ):
        fail("capacity-v1 unknown-SHA evidence contract drifted")

    stress = v2.get("structural_stress_m6")
    if not isinstance(stress, dict):
        fail("M6 structural stress evidence is missing")
    summary = stress.get("summary")
    if not isinstance(summary, dict):
        fail("M6 structural stress summary is missing")
    if sum(summary[rid]["index_covered"] for rid in ("cbd", "ewd", "rmd")) != 0:
        fail("M6 must retain 0/6 retrieval-index coverage")
    limiting = stress.get("limiting_case")
    if not isinstance(limiting, dict):
        fail("M6 limiting case is missing")
    if limiting.get("repository_id") != "rmd":
        fail("M6 limiting repository drifted")
    if limiting.get("actual_over_prediction", 0) < 17:
        fail("M6 structural falsification magnitude was weakened")
    conclusion = str(stress.get("reviewed_conclusion", "")).lower()
    if "exact-profile-only" not in conclusion:
        fail("M6 reviewed conclusion lost exact-profile-only direction")

    global_holdout = v3.get("global_summary")
    if not isinstance(global_holdout, dict):
        fail("M5 global holdout summary is missing")
    if global_holdout.get("snapshot_covered") != 12:
        fail("M5 snapshot holdout coverage drifted")
    if global_holdout.get("index_covered") != 12:
        fail("M5 index holdout coverage drifted")
    if v3.get("production_prediction_selected") is not False:
        fail("M5 evidence must remain non-production")

    inode_policy = policy.get("inode_policy")
    if inode_policy != {
        "pre_download_archive_file_entries": 1,
        "post_download_snapshot_entries": "F2_MATERIALIZED_ENTRIES",
        "fresh_install_extra_data_directories": 5,
        "index_build_cache_entries": 4,
    }:
        fail("inode production policy drifted")

    observations = v1["c0_m1"]["observations"]
    for record in observations:
        if (
            record["fresh_data_additional_peak_entries"]
            != record["same_sha_repair_data_additional_peak_entries"] + 5
        ):
            fail("F9 fresh-install +5 inode evidence drifted")
        if record["index_cache_additional_peak_entries"] != 4:
            fail("F9 index inode evidence drifted")

    state = policy.get("state_commit_headroom")
    if not isinstance(state, dict):
        fail("state_commit_headroom is missing")

    m2_frontier = v1["c0_m2"]["observed_frontier"][
        "lowest_successful_available_bytes"
    ]
    m3_frontier = v1["c0_m3"]["observed_frontier"][
        "lowest_successful_available_bytes"
    ]
    frontier = max(m2_frontier, m3_frontier)
    if frontier != 65536:
        fail("reviewed cold-sidecar byte frontier drifted")
    if state.get("selected_bytes") != 2 * frontier:
        fail("selected state byte headroom must remain 2x reviewed frontier")

    byte_evidence = state.get("byte_evidence")
    if not isinstance(byte_evidence, dict):
        fail("state byte evidence metadata is missing")
    if byte_evidence.get("maximum_observed_lowest_successful_bytes") != frontier:
        fail("state byte evidence frontier metadata drifted")
    if byte_evidence.get("selected_to_frontier_ratio") != 2:
        fail("state byte headroom rationale drifted")

    m7 = state.get("inode_evidence")
    if not isinstance(m7, dict):
        fail("M7 inode evidence is missing")
    if m7.get("actions_run_id") != 36044937017:
        fail("M7 Actions run drifted")
    if m7.get("artifact_id") != 10827339334:
        fail("M7 artifact ID drifted")
    digest = m7.get("artifact_sha256")
    if not isinstance(digest, str) or SHA256.fullmatch(digest) is None:
        fail("M7 artifact SHA-256 is invalid")
    if digest != "62f71248cd8104ef05b2c000ba10af328909e20c3a64dec41d8ecdc20678c6b8":
        fail("M7 artifact SHA-256 drifted")
    head = m7.get("qualified_head")
    if not isinstance(head, str) or SHA40.fullmatch(head) is None:
        fail("M7 qualified head is invalid")
    if head != "b99b68880ddd702c6c24853b2dd68fd03f2cfc44":
        fail("M7 qualified head drifted")
    if m7.get("available_bytes_before_each_case") != 16736256:
        fail("M7 byte-isolation condition drifted")

    expected_m7 = [
        (1, "FAIL_CLOSED_OLD_AUTHORITY"),
        (2, "SUCCESS_ATOMIC"),
        (3, "SUCCESS_ATOMIC"),
        (4, "SUCCESS_ATOMIC"),
        (8, "SUCCESS_ATOMIC"),
    ]
    actual_m7 = [
        (item.get("available_inodes"), item.get("classification"))
        for item in m7.get("observations", [])
    ]
    if actual_m7 != expected_m7:
        fail("M7 reviewed inode frontier drifted")
    if m7.get("minimum_observed_successful_inodes") != 2:
        fail("M7 minimum successful inode headroom drifted")
    if state.get("selected_inodes") != 4:
        fail("selected state inode headroom must remain 4")
    if state["selected_inodes"] != (
        2 * m7["minimum_observed_successful_inodes"]
    ):
        fail("state inode headroom rationale drifted")
    if m7.get("selected_to_minimum_ratio") != 2:
        fail("state inode headroom ratio metadata drifted")

    profile_source = PROFILE_INC_PATH.read_text(encoding="utf-8")
    profile_pattern = re.compile(
        r'ATM_CAPACITY_EXACT_PROFILE\(\s*'
        r'"(?P<repository_id>[a-z0-9_-]+)"\s*,\s*'
        r'"(?P<repository_sha>[0-9a-f]{40})"\s*,\s*'
        r'(?P<archive>\d+)\s*,\s*'
        r'(?P<snapshot>\d+)\s*,\s*'
        r'(?P<fresh>\d+)\s*,\s*'
        r'(?P<index>\d+)\s*,\s*'
        r'(?P<repair>\d+)\s*'
        r'\)',
        re.MULTILINE,
    )
    compiled_profiles = {}
    for match in profile_pattern.finditer(profile_source):
        rid = match.group("repository_id")
        if rid in compiled_profiles:
            fail(f"duplicate compiled exact profile for {rid}")
        compiled_profiles[rid] = {
            "repository_sha": match.group("repository_sha"),
            "archive_allocated_bytes": int(match.group("archive")),
            "snapshot_allocated_bytes": int(match.group("snapshot")),
            "fresh_data_additional_peak_bytes": int(match.group("fresh")),
            "index_cache_additional_peak_bytes": int(match.group("index")),
            "same_sha_repair_data_additional_peak_bytes": int(
                match.group("repair")
            ),
        }

    evidence_profiles = {
        item["repository_id"]: {
            "repository_sha": item["repository_sha"],
            "archive_allocated_bytes": item["archive_allocated_bytes"],
            "snapshot_allocated_bytes": item["snapshot_allocated_bytes"],
            "fresh_data_additional_peak_bytes": (
                item["fresh_data_additional_peak_bytes"]
            ),
            "index_cache_additional_peak_bytes": (
                item["index_cache_additional_peak_bytes"]
            ),
            "same_sha_repair_data_additional_peak_bytes": (
                item["same_sha_repair_data_additional_peak_bytes"]
            ),
        }
        for item in v1["c0_m1"]["observations"]
    }
    if compiled_profiles != evidence_profiles:
        fail("compiled exact-SHA profile table drifted from C0-M1 evidence")

    policy_header = POLICY_HEADER_PATH.read_text(encoding="utf-8")
    required_constants = {
        "ATM_CAPACITY_STATE_COMMIT_HEADROOM_BYTES": state["selected_bytes"],
        "ATM_CAPACITY_STATE_COMMIT_HEADROOM_INODES": state["selected_inodes"],
        "ATM_CAPACITY_INDEX_BUILD_INODES": (
            inode_policy["index_build_cache_entries"]
        ),
        "ATM_CAPACITY_ARCHIVE_FILE_INODES": (
            inode_policy["pre_download_archive_file_entries"]
        ),
    }
    for name, expected in required_constants.items():
        pattern = re.compile(
            rf"#define\s+{re.escape(name)}\s+"
            rf"\(\(guint64\)\s+{expected}\)"
        )
        if pattern.search(policy_header) is None:
            fail(f"compiled policy constant drifted: {name}")

    contract = policy.get("operation_contract")
    expected_contract = {
        "two_checkpoint_admission": True,
        "same_sha_repair_must_admit_before_quarantine": True,
        "completed_archive_not_double_counted_post_download": True,
        "shared_filesystem_requirements_grouped_by_device": True,
        "automatic_reclamation_for_admission": False,
        "preflight_error_class": "RepositoryError.NO_SPACE",
        "generic_sqlite_ioerr_is_no_space": False,
    }
    if contract != expected_contract:
        fail("operation contract drifted")

    print(
        "capacity production policy validation passed: "
        "exact-profile-only bytes + 128 KiB/4-inode state headroom, "
        "runtime not wired"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
