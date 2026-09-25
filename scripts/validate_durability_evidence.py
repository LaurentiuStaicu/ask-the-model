#!/usr/bin/env python3

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "benchmarks" / "durability-v1" / "evidence.json"
CORPUS_PATH = ROOT / "benchmarks" / "retrieval-v1" / "benchmark.json"

SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise SystemExit(
        f"durability evidence validation failed: {message}"
    )


def load(path: pathlib.Path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def positive_int(value, context):
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        fail(f"{context} must be a positive integer")


def main() -> int:
    evidence = load(EVIDENCE_PATH)
    benchmark = load(CORPUS_PATH)

    if evidence.get("schema_version") != 1:
        fail("schema_version must remain 1")
    if evidence.get("status") != "qualification-only":
        fail("status must remain qualification-only")
    if evidence.get("production_barrier_selected") is not False:
        fail("A1-M1 must not select a production barrier")

    source = evidence.get("source")
    if not isinstance(source, dict):
        fail("source object is missing")

    if source.get("actions_run_id") != 36116289496:
        fail("A1-M1 Actions run drifted")
    if source.get("artifact_id") != 10854573179:
        fail("A1-M1 artifact ID drifted")
    if source.get("artifact_name") != (
        "atm-a1-m1-durability-36116289496-1"
    ):
        fail("A1-M1 artifact name drifted")

    digest = source.get("artifact_sha256")
    if not isinstance(digest, str) or SHA256.fullmatch(digest) is None:
        fail("artifact SHA-256 is invalid")
    if digest != (
        "4ffbbcc089af101324843756ae43f033a5704a266a0e08166c0f85f87c59cf2c"
    ):
        fail("artifact SHA-256 drifted")

    for key in ("atm_source_commit", "workflow_commit"):
        value = source.get(key)
        if not isinstance(value, str) or SHA40.fullmatch(value) is None:
            fail(f"source.{key} is not a lowercase 40-hex SHA")

    if source["atm_source_commit"] != (
        "092d16394c7f47d3fa12e1af94d02439523a7739"
    ):
        fail("measured source commit drifted")
    if source["workflow_commit"] != (
        "ec56e4860142a1f0e9867d8369d2e021821f6c86"
    ):
        fail("workflow commit drifted")

    runner = evidence.get("runner_filesystem")
    if not isinstance(runner, dict):
        fail("runner_filesystem object is missing")
    if runner.get("filesystem") != "ext4":
        fail("reviewed A1-M1 filesystem must remain ext4")
    if runner.get("source") != "/dev/nvme0n1p1":
        fail("reviewed A1-M1 filesystem source drifted")
    if runner.get("kernel_release") != "6.17.0-1022-azure":
        fail("reviewed A1-M1 kernel release drifted")

    method = evidence.get("method")
    expected_strategies = [
        "S3_CONTROL",
        "S1_TARGETED_FSYNC",
        "S2_SYNCFS",
    ]
    if not isinstance(method, dict):
        fail("method object is missing")
    if method.get("strategies") != expected_strategies:
        fail("strategy set/order drifted")
    if method.get("runs_per_strategy_per_repository") != 3:
        fail("run count drifted")
    if method.get("order_rotated") is not True:
        fail("order rotation must remain explicit")

    corpus = {
        item["repository_id"]: item["snapshot_sha"]
        for item in benchmark["corpus"]
    }
    if evidence.get("corpus") != corpus:
        fail("durability corpus drifted from retrieval-v1 pins")

    expected = {
        "cbd": {
            "files": 108,
            "bytes": 633284,
            "control": 6814,
            "s1_barrier": 30532,
            "s1_total": 37162,
            "s1_delta": 30348,
            "s1_dirs": 19,
            "s2_barrier": 2416,
            "s2_total": 9285,
            "s2_delta": 2471,
        },
        "ewd": {
            "files": 160,
            "bytes": 4616863,
            "control": 24667,
            "s1_barrier": 54657,
            "s1_total": 79988,
            "s1_delta": 55321,
            "s1_dirs": 33,
            "s2_barrier": 25776,
            "s2_total": 50181,
            "s2_delta": 25514,
        },
        "rmd": {
            "files": 865,
            "bytes": 15387131,
            "control": 80747,
            "s1_barrier": 285953,
            "s1_total": 366721,
            "s1_delta": 285974,
            "s1_dirs": 59,
            "s2_barrier": 90616,
            "s2_total": 172418,
            "s2_delta": 91671,
        },
    }

    summary = evidence.get("summary")
    if not isinstance(summary, dict) or set(summary) != set(expected):
        fail("summary must contain exactly CBD/EWD/RMD")

    for rid, exp in expected.items():
        record = summary.get(rid)
        if not isinstance(record, dict):
            fail(f"{rid} summary is missing")

        if record.get("seal_file_count") != exp["files"]:
            fail(f"{rid} seal file count drifted")
        if record.get("seal_total_bytes") != exp["bytes"]:
            fail(f"{rid} seal byte count drifted")

        control = record.get("S3_CONTROL")
        s1 = record.get("S1_TARGETED_FSYNC")
        s2 = record.get("S2_SYNCFS")
        if not all(isinstance(x, dict) for x in (control, s1, s2)):
            fail(f"{rid} strategy summary is incomplete")

        if control.get("median_prepared_elapsed_us") != exp["control"]:
            fail(f"{rid} control median drifted")

        checks = (
            (s1, "median_barrier_elapsed_us", exp["s1_barrier"]),
            (s1, "median_prepared_elapsed_us", exp["s1_total"]),
            (s1, "median_total_over_control_us", exp["s1_delta"]),
            (s1, "file_fsync_calls", exp["files"]),
            (s1, "directory_fsync_calls", exp["s1_dirs"]),
            (s2, "median_barrier_elapsed_us", exp["s2_barrier"]),
            (s2, "median_prepared_elapsed_us", exp["s2_total"]),
            (s2, "median_total_over_control_us", exp["s2_delta"]),
            (s2, "syncfs_calls", 1),
        )
        for obj, key, wanted in checks:
            if obj.get(key) != wanted:
                fail(f"{rid}.{key} drifted")

        if not (
            s2["median_barrier_elapsed_us"] <
            s1["median_barrier_elapsed_us"]
        ):
            fail(f"{rid} reviewed S2<S1 barrier relationship drifted")

        for strategy in (s1, s2):
            positive_int(
                strategy["median_barrier_elapsed_us"],
                f"{rid} barrier median",
            )

    conclusions = evidence.get("reviewed_conclusions")
    if not isinstance(conclusions, list) or len(conclusions) < 6:
        fail("reviewed conclusions are incomplete")

    joined = "\n".join(str(x) for x in conclusions).lower()
    for phrase in (
        "filesystem-wide scope",
        "does not measure snapshot rename",
        "not a physical-power-loss durability proof",
        "no production durability barrier",
    ):
        if phrase not in joined:
            fail(f"reviewed limitation lost: {phrase}")

    tier2 = evidence.get("tier2_direction")
    if not isinstance(tier2, dict):
        fail("Tier-2 direction is missing")
    if tier2.get("preferred_first_prototype") != (
        "dm-log-writes replay qualification"
    ):
        fail("Tier-2 prototype direction drifted")
    if tier2.get("production_authorized") is not False:
        fail("Tier-2 direction must not authorize production")

    capability = evidence.get("tier2_capability")
    if not isinstance(capability, dict):
        fail("Tier-2 capability evidence is missing")
    if capability.get("probe_id") != (
        "atm-a1-m2-dm-log-writes-capability-v1"
    ):
        fail("Tier-2 capability probe identity drifted")
    if capability.get("status") != "supported":
        fail("reviewed Tier-2 capability must remain supported")
    if capability.get("production_durability_authorized") is not False:
        fail("capability evidence must not authorize production durability")

    cap_source = capability.get("source")
    if not isinstance(cap_source, dict):
        fail("Tier-2 capability source is missing")
    if cap_source.get("actions_run_id") != 36117390930:
        fail("Tier-2 capability Actions run drifted")
    if cap_source.get("artifact_id") != 10855264761:
        fail("Tier-2 capability artifact ID drifted")
    if cap_source.get("artifact_name") != (
        "atm-a1-m2-dm-log-writes-36117390930-1"
    ):
        fail("Tier-2 capability artifact name drifted")
    cap_digest = cap_source.get("artifact_sha256")
    if not isinstance(cap_digest, str) or SHA256.fullmatch(cap_digest) is None:
        fail("Tier-2 capability artifact digest is invalid")
    if cap_digest != (
        "4c88f7a4934c8c6470fa62d6857ec4253ef79bcc6c10eb0bf5bdd999f33b0415"
    ):
        fail("Tier-2 capability artifact digest drifted")
    cap_head = cap_source.get("atm_source_commit")
    if not isinstance(cap_head, str) or SHA40.fullmatch(cap_head) is None:
        fail("Tier-2 capability source commit is invalid")
    if cap_head != "d6e1feb16990e1bbffb9e7bcce8c475c472e4272":
        fail("Tier-2 capability source commit drifted")

    if capability.get("kernel") != {
        "system": "Linux",
        "release": "6.17.0-1022-azure",
        "machine": "x86_64",
    }:
        fail("Tier-2 capability kernel context drifted")

    if capability.get("upstream") != {
        "repository": "josefbacik/log-writes",
        "commit": "7b70d8a6863c5de30933d42a7672d35d01d2dc6c",
    }:
        fail("Tier-2 replay-log upstream pin drifted")

    expected_capability_checks = {
        "dm_log_writes_target_available": True,
        "loop_device_available": True,
        "mapping_created": True,
        "ext4_mounted": True,
        "replay_log_built": True,
        "fsync_mark_found": True,
    }
    if capability.get("checks") != expected_capability_checks:
        fail("Tier-2 capability check matrix drifted")

    cap_conclusion = str(
        capability.get("reviewed_conclusion", "")
    ).lower()
    for phrase in (
        "minimum dm-log-writes/replay-log mark mechanism",
        "capability evidence only",
        "does not establish replay durability",
    ):
        if phrase not in cap_conclusion:
            fail(f"Tier-2 capability limitation lost: {phrase}")

    replay = evidence.get("tier2_replay")
    if not isinstance(replay, dict):
        fail("Tier-2 replay evidence is missing")
    if replay.get("measurement_id") != (
        "atm-a1-m3-block-replay-smoke-v1"
    ):
        fail("Tier-2 replay measurement identity drifted")
    if replay.get("status") != "qualified":
        fail("Tier-2 replay must remain qualified")
    if replay.get("production_durability_authorized") is not False:
        fail("Tier-2 replay evidence must not authorize production durability")

    replay_source = replay.get("source")
    if not isinstance(replay_source, dict):
        fail("Tier-2 replay source is missing")
    if replay_source.get("actions_run_id") != 36118334996:
        fail("Tier-2 replay Actions run drifted")
    if replay_source.get("artifact_id") != 10856600862:
        fail("Tier-2 replay artifact ID drifted")
    if replay_source.get("artifact_name") != (
        "atm-a1-m3-replay-smoke-36118334996-1"
    ):
        fail("Tier-2 replay artifact name drifted")
    replay_digest = replay_source.get("artifact_sha256")
    if (
        not isinstance(replay_digest, str)
        or SHA256.fullmatch(replay_digest) is None
        or replay_digest != (
            "fb4da03b7d7d1ade93a12c407efc413f8ca4cd12c946e8bf9014736971829cc9"
        )
    ):
        fail("Tier-2 replay artifact digest drifted")
    replay_head = replay_source.get("atm_source_commit")
    if (
        not isinstance(replay_head, str)
        or SHA40.fullmatch(replay_head) is None
        or replay_head != "4260c0db0d4c8c0bf929aabbf0950435c7ed75a7"
    ):
        fail("Tier-2 replay source commit drifted")

    if replay.get("kernel") != {
        "system": "Linux",
        "release": "6.17.0-1022-azure",
        "machine": "x86_64",
    }:
        fail("Tier-2 replay kernel context drifted")
    if replay.get("upstream") != {
        "repository": "josefbacik/log-writes",
        "commit": "7b70d8a6863c5de30933d42a7672d35d01d2dc6c",
    }:
        fail("Tier-2 replay upstream pin drifted")

    marks = replay.get("marks")
    if marks != {
        "mkfs_entry": 35,
        "fsync_entry": 57,
    }:
        fail("Tier-2 replay mark positions drifted")
    if marks["fsync_entry"] <= marks["mkfs_entry"]:
        fail("Tier-2 fsync mark must remain after mkfs mark")

    replay_result = replay.get("replay")
    if not isinstance(replay_result, dict):
        fail("Tier-2 replay result is missing")
    if replay_result.get("end_mark") != "fsync":
        fail("Tier-2 replay must remain bounded by fsync mark")
    if replay_result.get("e2fsck_exit_code") not in (0, 1, 2):
        fail("Tier-2 replay e2fsck result is not a corrected/clean outcome")
    expected_sha = replay_result.get("expected_sha256")
    replayed_sha = replay_result.get("replayed_sha256")
    for value, context in (
        (expected_sha, "expected replay SHA"),
        (replayed_sha, "replayed SHA"),
    ):
        if not isinstance(value, str) or SHA256.fullmatch(value) is None:
            fail(f"{context} is invalid")
    if expected_sha != replayed_sha:
        fail("Tier-2 replay payload digest mismatch")
    if replay_result.get("content_match") is not True:
        fail("Tier-2 replay content_match must remain true")

    replay_conclusion = str(
        replay.get("reviewed_conclusion", "")
    ).lower()
    for phrase in (
        "replay mechanism only",
        "does not yet exercise atm snapshot promotion",
        "control db authority",
        "authorize a production durability barrier",
    ):
        if phrase not in replay_conclusion:
            fail(f"Tier-2 replay limitation lost: {phrase}")

    atm_replay = evidence.get("tier2_atm_replay_baseline")
    if not isinstance(atm_replay, dict):
        fail("Tier-2 AtM replay baseline evidence is missing")
    if atm_replay.get("measurement_id") != (
        "atm-a1-m4-atm-replay-oracle-v1"
    ):
        fail("Tier-2 AtM replay measurement identity drifted")
    if atm_replay.get("status") != "qualification-only":
        fail("Tier-2 AtM replay status drifted")
    if atm_replay.get("strategy") != "S3_CURRENT_BASELINE":
        fail("Tier-2 AtM replay strategy drifted")
    if atm_replay.get("production_durability_authorized") is not False:
        fail("Tier-2 AtM replay must not authorize production durability")

    atm_source = atm_replay.get("source")
    if not isinstance(atm_source, dict):
        fail("Tier-2 AtM replay source is missing")
    if atm_source.get("actions_run_id") != 36121072400:
        fail("Tier-2 AtM replay Actions run drifted")
    if atm_source.get("artifact_name") != (
        "atm-a1-m4-atm-replay-36121072400-1"
    ):
        fail("Tier-2 AtM replay artifact name drifted")
    if atm_source.get("artifact_id") != 10858715370:
        fail("Tier-2 AtM replay artifact ID drifted")
    if atm_source.get("artifact_sha256") != (
        "7e8e1f16cd92d854be76a6848b0a44138d67d3255e9ca6328af841a3990a68e5"
    ):
        fail("Tier-2 AtM replay artifact digest drifted")
    if atm_source.get("atm_source_commit") != (
        "4a926fcbccbf0c43a2ccd43817ceee70d788e6d5"
    ):
        fail("Tier-2 AtM replay source commit drifted")

    if atm_replay.get("kernel") != {
        "system": "Linux",
        "release": "6.17.0-1022-azure",
        "machine": "x86_64",
    }:
        fail("Tier-2 AtM replay kernel context drifted")
    if atm_replay.get("upstream") != {
        "repository": "josefbacik/log-writes",
        "commit": "7b70d8a6863c5de30933d42a7672d35d01d2dc6c",
    }:
        fail("Tier-2 AtM replay upstream pin drifted")

    protocol = atm_replay.get("protocol")
    if protocol != {
        "old_authority_synced_before_baseline_mark": True,
        "new_snapshot_uses_real_snapshot_seal": True,
        "fresh_process_verifier_after_replay": True,
        "candidate_durability_barrier_added": False,
        "clean_unmount_not_used_as_target_mark": True,
    }:
        fail("Tier-2 AtM replay protocol drifted")

    scenarios = atm_replay.get("scenarios")
    if not isinstance(scenarios, dict) or set(scenarios) != {
        "pre_rename",
        "post_rename",
        "after_authority",
    }:
        fail("Tier-2 AtM replay scenario set drifted")

    old_sha = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    new_sha = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    old_seal = (
        "39d5b5c5ff8fe2d5a0fbee8238abab546f39b07774ad29aeaeb06c4d25aac780"
    )

    for name in ("pre_rename", "post_rename"):
        scenario = scenarios[name]
        if scenario.get("baseline_entry") != 162:
            fail(f"{name} baseline entry drifted")
        if scenario.get("scenario_entry") != 163:
            fail(f"{name} scenario entry drifted")
        if scenario.get("e2fsck_exit_code") != 1:
            fail(f"{name} e2fsck result drifted")
        if scenario.get("expected_classification") != "OLD_AUTHORITY_VALID":
            fail(f"{name} expected classification drifted")
        if scenario.get("observed_classification") != "OLD_AUTHORITY_VALID":
            fail(f"{name} old-authority result drifted")
        if scenario.get("active_repository_sha") != old_sha:
            fail(f"{name} active repository SHA drifted")
        if scenario.get("stored_seal") != old_seal:
            fail(f"{name} stored old seal drifted")
        if scenario.get("computed_seal") != old_seal:
            fail(f"{name} computed old seal drifted")
        if scenario.get("seal_match") is not True:
            fail(f"{name} old authority must remain seal-valid")
        if scenario.get("strong_invariant_satisfied") is not True:
            fail(f"{name} strong invariant must remain satisfied")

    after = scenarios["after_authority"]
    if after.get("baseline_entry") != 162:
        fail("after-authority baseline entry drifted")
    if after.get("scenario_entry") != 195:
        fail("after-authority scenario entry drifted")
    if after.get("e2fsck_exit_code") != 1:
        fail("after-authority e2fsck result drifted")
    if after.get("expected_classification") != "NEW_AUTHORITY_VALID":
        fail("after-authority expected classification drifted")
    if after.get("observed_classification") != "INVALID_AUTHORITY":
        fail("S3 falsification classification was lost")
    if after.get("active_generation_id") != 2:
        fail("after-authority active generation drifted")
    if after.get("active_repository_sha") != new_sha:
        fail("after-authority new active SHA drifted")
    if after.get("stored_seal") != (
        "acebf979895f9efd073014fa707038d47ec71b411f4e12a32aa619e9c63f3132"
    ):
        fail("after-authority stored seal drifted")
    if after.get("computed_seal") != (
        "7299f3e88c9a88995a62ef415e0da959ba4571df5f62976e9f9ac49d4660a3d8"
    ):
        fail("after-authority replayed seal drifted")
    if after.get("seal_match") is not False:
        fail("S3 falsification must retain seal mismatch")
    if after.get("reason_code") != "active_snapshot_seal_mismatch":
        fail("S3 falsification reason drifted")
    if after.get("strong_invariant_satisfied") is not False:
        fail("S3 strong-invariant failure was lost")

    conclusion = str(
        atm_replay.get("reviewed_conclusion", "")
    ).lower()
    for phrase in (
        "s3 is therefore falsified",
        "old-valid/new-valid tier-2 target",
        "selects neither s1 nor s2",
        "authorizes no production durability barrier",
    ):
        if phrase not in conclusion:
            fail(f"Tier-2 AtM replay conclusion lost: {phrase}")

    print(
        "durability evidence validation passed: "
        "A1-M1 CBD/EWD/RMD medians frozen, "
        "S2 faster on reviewed runner, no production barrier selected"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
