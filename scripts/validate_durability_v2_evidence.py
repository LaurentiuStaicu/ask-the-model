#!/usr/bin/env python3

import json
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "benchmarks" / "durability-v2" / "evidence.json"

SHA40 = re.compile(r"^[0-9a-f]{40}$")
SHA256 = re.compile(r"^[0-9a-f]{64}$")


def fail(message: str) -> None:
    raise SystemExit(
        f"durability-v2 evidence validation failed: {message}"
    )


def require_source(source, expected, name):
    if source != expected:
        fail(f"{name} source provenance drifted")
    if SHA40.fullmatch(source["atm_source_commit"]) is None:
        fail(f"{name} source commit is invalid")
    if SHA256.fullmatch(source["artifact_sha256"]) is None:
        fail(f"{name} artifact digest is invalid")


def main() -> int:
    with EVIDENCE.open(encoding="utf-8") as handle:
        evidence = json.load(handle)

    if evidence.get("schema_version") != 1:
        fail("schema_version drifted")
    if evidence.get("evidence_id") != "atm-a1-fresh-namespace-evidence-v1":
        fail("evidence_id drifted")
    if evidence.get("status") != "qualification-only":
        fail("status must remain qualification-only")
    if evidence.get("production_namespace_sequence_selected") is not False:
        fail("F10 must not select production namespace sequencing")

    m10 = evidence.get("m10_replay")
    if not isinstance(m10, dict):
        fail("M10 replay evidence is missing")
    require_source(
        m10.get("source"),
        {
            "actions_run_id": 36145303682,
            "artifact_name": "atm-a1-m10-fresh-namespace-replay-36145303682-1",
            "artifact_id": 10869154094,
            "artifact_sha256": "c85d998ac5c51ca1beed3c33f36cf6545c449b6875f45f5bbe6b349288725cae",
            "atm_source_commit": "1a5aa2f4d19082ab7a611897e0c2247d303c1fcd",
        },
        "M10",
    )
    if m10.get("kernel") != {
        "machine": "x86_64",
        "release": "6.17.0-1022-azure",
        "system": "Linux",
    }:
        fail("M10 kernel context drifted")
    if m10.get("upstream") != {
        "repository": "josefbacik/log-writes",
        "commit": "7b70d8a6863c5de30933d42a7672d35d01d2dc6c",
    }:
        fail("M10 replay-log pin drifted")

    m10_protocol = m10.get("protocol", {})
    if m10_protocol.get("candidate_set") != [
        "S1_PARENT_ONLY",
        "S1_DEST_CHAIN",
        "S1_DEST_SOURCE",
    ]:
        fail("M10 candidate set/order drifted")
    if m10_protocol.get("boundary_order") != [
        "post_barrier_pre_destination_hierarchy",
        "post_destination_hierarchy_pre_rename",
        "post_rename_pre_destination_parent_fsync",
        "post_destination_parent_fsync_pre_source_parent_fsync",
        "post_source_parent_fsync_pre_authority",
        "after_authority",
    ]:
        fail("M10 boundary set/order drifted")
    for key in (
        "baseline_synced_before_candidate",
        "fresh_control_db_without_repository_authority",
        "repositories_hierarchy_absent_at_baseline",
        "fresh_process_seal_verifier_after_replay",
        "clean_unmount_not_used_as_target_mark",
    ):
        if m10_protocol.get(key) is not True:
            fail(f"M10 protocol.{key} must remain true")
    if m10.get("scenario_count") != 18:
        fail("M10 scenario count must remain 18")
    if m10.get("all_e2fsck_recoverable") is not True:
        fail("M10 replay recoverability drifted")

    expected_summary = {
        "all_pre_authority_empty_valid": True,
        "after_authority_new_valid": True,
        "after_authority_namespace_clean": True,
        "post_source_boundary_namespace_clean": True,
    }
    summaries = m10.get("strategy_summaries")
    if not isinstance(summaries, dict):
        fail("M10 strategy summaries are missing")
    for candidate in (
        "S1_PARENT_ONLY",
        "S1_DEST_CHAIN",
        "S1_DEST_SOURCE",
    ):
        if summaries.get(candidate) != expected_summary:
            fail(f"M10 {candidate} summary drifted")

    m11 = evidence.get("m11_eio")
    if not isinstance(m11, dict):
        fail("M11 EIO evidence is missing")
    require_source(
        m11.get("source"),
        {
            "actions_run_id": 36147082278,
            "artifact_name": "atm-a1-m11-namespace-eio-36147082278-1",
            "artifact_id": 10870456523,
            "artifact_sha256": "e11a8a6c4b9d198c223a355d1129a9807da445cbdf564daaa11ce706fe9cb754",
            "atm_source_commit": "3682bcd555b3f1aa2a3f778270896cac43fbd30e",
        },
        "M11",
    )
    if m11.get("kernel") != {
        "machine": "x86_64",
        "release": "6.17.0-1022-azure",
        "system": "Linux",
    }:
        fail("M11 kernel context drifted")

    m11_protocol = m11.get("protocol", {})
    if m11_protocol.get("candidate") != "S1_DEST_SOURCE":
        fail("M11 candidate drifted")
    if m11_protocol.get("device_mapper_target") != "flakey":
        fail("M11 dm target drifted")
    if m11_protocol.get("feature") != "error_writes":
        fail("M11 fault feature drifted")
    if m11_protocol.get("boundaries") != [
        "destination_hierarchy_fsync",
        "destination_parent_fsync",
        "source_parent_fsync",
    ]:
        fail("M11 boundary set/order drifted")
    for key in (
        "empty_authority_synced_before_each_scenario",
        "fault_teardown_suspend_noflush_nolockfs",
        "fresh_process_verifier_after_recovery",
        "no_syncfs_fallback",
    ):
        if m11_protocol.get(key) is not True:
            fail(f"M11 protocol.{key} must remain true")

    if m11.get("scenario_count") != 3:
        fail("M11 scenario count must remain 3")
    if m11.get("all_authority_fail_closed") is not True:
        fail("M11 fail-closed aggregate drifted")
    if m11.get("all_e2fsck_recoverable") is not True:
        fail("M11 recoverability aggregate drifted")

    expected_results = {
        "destination_hierarchy_fsync": {
            "checkpoint": "fresh_post_barrier_pre_destination_hierarchy",
            "helper_exit_code": 2,
            "e2fsck_exit_code": 1,
            "io_error_observed": True,
            "classification": "EMPTY_AUTHORITY_VALID",
            "active_generation_id": 0,
            "active_repository_sha": None,
            "final_snapshot_exists": False,
            "staging_exists": True,
            "namespace_clean": False,
            "authority_fail_closed": True,
        },
        "destination_parent_fsync": {
            "checkpoint": "fresh_post_rename_pre_destination_parent_fsync",
            "helper_exit_code": 2,
            "e2fsck_exit_code": 1,
            "io_error_observed": True,
            "classification": "EMPTY_AUTHORITY_VALID",
            "active_generation_id": 0,
            "active_repository_sha": None,
            "final_snapshot_exists": False,
            "staging_exists": True,
            "namespace_clean": False,
            "authority_fail_closed": True,
        },
        "source_parent_fsync": {
            "checkpoint": "fresh_post_destination_parent_fsync_pre_source_parent_fsync",
            "helper_exit_code": 2,
            "e2fsck_exit_code": 0,
            "io_error_observed": True,
            "classification": "EMPTY_AUTHORITY_VALID",
            "active_generation_id": 0,
            "active_repository_sha": None,
            "final_snapshot_exists": True,
            "staging_exists": False,
            "namespace_clean": True,
            "authority_fail_closed": True,
        },
    }
    if m11.get("results") != expected_results:
        fail("M11 exact reviewed boundary results drifted")

    expected_scope_review = {
        "destination_hierarchy_checkpoint_preceded_namespace_helper": True,
        "destination_hierarchy_fsync_isolated": False,
        "destination_hierarchy_result_scope": (
            "broad destination-hierarchy write-error fail-closed evidence only"
        ),
        "destination_parent_fsync_isolated": True,
        "source_parent_fsync_isolated": False,
        "source_parent_result_scope": (
            "checkpoint is immediately before source-parent fsync, but the frozen "
            "artifact records only generic EIO and does not prove that this fsync "
            "returned the error"
        ),
        "superseding_gate": "A1-M11b exact namespace fsync EIO qualification",
    }
    if m11.get("scope_review") != expected_scope_review:
        fail("M11 reviewed evidence scope drifted")

    interpretation = evidence.get("interpretation", {})
    if interpretation.get("m10_empirically_distinguished_candidates") is not False:
        fail("M10 empirical tie interpretation drifted")
    if interpretation.get("m11_completed") is not True:
        fail("M11 completion marker drifted")
    if interpretation.get("candidate_for_policy_review") != "S1_DEST_SOURCE":
        fail("policy-review candidate drifted")
    if interpretation.get("policy_review_ready") is not False:
        fail("policy review must remain blocked pending M11b")
    if interpretation.get("m11_exact_fsync_qualification_complete") is not False:
        fail("M11 exact-fsync completion must remain false pending M11b")
    if interpretation.get("m11b_required") is not True:
        fail("M11b requirement was lost")
    if interpretation.get("production_namespace_sequence_selected") is not False:
        fail("F10 must remain evidence-only")
    if interpretation.get("automatic_orphan_recovery_authorized") is not False:
        fail("F10 must not authorize automatic orphan recovery")

    contract = str(
        interpretation.get("linux_directory_entry_contract", "")
    ).lower()
    for phrase in (
        "does not necessarily persist",
        "containing directory entry",
        "explicit fsync",
    ):
        if phrase not in contract:
            fail(f"Linux fsync contract phrase lost: {phrase}")

    reason = str(interpretation.get("reason", "")).lower()
    for phrase in (
        "fresh destination hierarchy",
        "destination rename-parent",
        "source staging-parent",
        "historical m11 destination-parent result fsync-specific",
        "hierarchy checkpoint preceded mkdirat/open/fstat",
        "source-parent testing showed that fsync can return successfully",
        "later control db write",
        "blocked on m11b",
    ):
        if phrase not in reason:
            fail(f"review rationale lost: {phrase}")

    limitations = "\n".join(
        str(x).lower()
        for x in evidence.get("limitations", [])
    )
    for phrase in (
        "not a universal filesystem",
        "does not weaken the linux directory-entry durability contract",
        "production namespace sequencing remains unselected",
        "authority-wide exclusion",
        "does not isolate fsync from preceding mkdirat/open/fstat",
        "suspended until m11b",
        "source-parent checkpoint is positioned immediately before",
        "may not force write i/o at that call",
    ):
        if phrase not in limitations:
            fail(f"reviewed limitation lost: {phrase}")

    print("durability-v2 evidence: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
