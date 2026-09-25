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

    m11b = evidence.get("m11b_exact_eio")
    if not isinstance(m11b, dict):
        fail("M11b exact-EIO evidence is missing")
    require_source(
        m11b.get("source"),
        {
            "actions_run_id": 36151935138,
            "artifact_name": "atm-a1-m11b-exact-namespace-fsync-eio-36151935138-1",
            "artifact_id": 10871264620,
            "artifact_sha256": "f86852cb93ec3d2ba9ca77a78a2be0fd1630eba4730990f38e493599b5cbf99c",
            "atm_source_commit": "d2eeee61466dccb85eab1ed94b248aac056f961f",
        },
        "M11b",
    )
    expected_m11b = {
        "measurement_id": "atm-a1-m11b-exact-namespace-fsync-eio-v2",
        "status": "measurement-only",
        "production_namespace_selected": False,
        "kernel": {
            "machine": "x86_64",
            "release": "6.17.0-1022-azure",
            "system": "Linux",
        },
        "protocol": {
            "candidate": "S1_DEST_SOURCE",
            "device_mapper_target": "flakey",
            "dm_flakey_boundaries": [
                "destination_hierarchy_child_fsync",
                "destination_parent_fsync",
            ],
            "empty_authority_synced_before_each_scenario": True,
            "exact_fsync_context_required": True,
            "fault_teardown_suspend_noflush_nolockfs": True,
            "feature": "error_writes",
            "flakey_down_interval_seconds": 600,
            "flakey_up_interval_seconds": 1,
            "fresh_process_verifier_after_recovery": True,
            "no_syncfs_fallback": True,
            "parent_fsync_probe": "synthetic-fsync-return-eio-on-exact-call",
            "source_fsync_probe": "full-flow-synthetic-fsync-return-eio-on-call-6",
        },
        "results": [
            {
                "active_generation_id": 0,
                "active_repository_sha": None,
                "authority_fail_closed": True,
                "boundary": "destination_hierarchy_child_fsync",
                "candidate_failed": True,
                "checkpoint": "namespace_repository_before_child_fsync",
                "classification": "EMPTY_AUTHORITY_VALID",
                "e2fsck_exit_code": 1,
                "error_context_match": True,
                "expected_classification": "EMPTY_AUTHORITY_VALID",
                "expected_error_context": "Could not fsync snapshot namespace child directory",
                "final_snapshot_exists": False,
                "helper_exit_code": 2,
                "io_error_observed": True,
                "namespace_clean": False,
                "qualified": True,
                "reason_code": "no_repository_authority",
                "staging_exists": True,
                "strategy": "S1_DEST_SOURCE",
            },
            {
                "active_generation_id": 0,
                "active_repository_sha": None,
                "authority_fail_closed": True,
                "boundary": "destination_parent_fsync",
                "candidate_failed": True,
                "checkpoint": "fresh_post_rename_pre_destination_parent_fsync",
                "classification": "EMPTY_AUTHORITY_VALID",
                "e2fsck_exit_code": 1,
                "error_context_match": True,
                "expected_classification": "EMPTY_AUTHORITY_VALID",
                "expected_error_context": "Could not fsync promoted snapshot parent",
                "final_snapshot_exists": False,
                "helper_exit_code": 2,
                "io_error_observed": True,
                "namespace_clean": False,
                "qualified": True,
                "reason_code": "no_repository_authority",
                "staging_exists": True,
                "strategy": "S1_DEST_SOURCE",
            },
        ],
        "namespace_parent_fsync_probe": {
            "directories_created_before_failure": 2,
            "error_context_match": True,
            "error_is_eio": True,
            "fsync_calls": 4,
            "injection": "fsync-return-eio-on-call-4",
            "probe_id": "atm-a1-m11b-namespace-parent-fsync-eio-v1",
            "qualified": True,
            "schema_version": 1,
            "successful_fsync_calls_before_failure": 3,
        },
        "namespace_parent_fsync_probe_qualified": True,
        "source_parent_fsync_probe": {
            "active_generation_id": 0,
            "active_repository_sha": None,
            "classification": "EMPTY_AUTHORITY_VALID",
            "error_context_match": True,
            "failed_fsync_call": 6,
            "final_snapshot_exists": True,
            "helper_exit_code": 2,
            "injection": "snapshot-durability-fsync-return-eio-on-call-6",
            "io_error_observed": True,
            "namespace_clean": True,
            "probe_id": "atm-a1-m11b-source-parent-fsync-eio-v1",
            "qualified": True,
            "schema_version": 1,
            "staging_exists": False,
        },
        "source_parent_fsync_probe_qualified": True,
        "all_authority_fail_closed": True,
        "all_e2fsck_recoverable": True,
        "all_exact_error_contexts": True,
        "limitations": [
            "M11b qualifies block-layer EIO at the exact hierarchy-child and destination-parent fsync contexts on ext4. The containing-parent namespace fsync and the post-rename source-parent fsync use deterministic exact-syscall probes because a preceding fsync can drain the shared journal and leave no block write for dm-flakey to fail at those exact calls.",
            "M10 remains the process-crash replay evidence for namespace ordering and fresh-authority classification.",
            "Passing M11b does not authorize automatic orphan deletion or replace the separate authority-wide recovery-exclusion problem.",
        ],
    }
    actual_m11b = dict(m11b)
    actual_m11b.pop("source", None)
    if actual_m11b != expected_m11b:
        fail("M11b exact frozen result drifted")

    interpretation = evidence.get("interpretation", {})
    if interpretation.get("m10_empirically_distinguished_candidates") is not False:
        fail("M10 empirical tie interpretation drifted")
    if interpretation.get("m11_completed") is not True:
        fail("M11 completion marker drifted")
    if interpretation.get("candidate_for_policy_review") != "S1_DEST_SOURCE":
        fail("policy-review candidate drifted")
    if interpretation.get("m11b_completed") is not True:
        fail("M11b completion marker was lost")
    if interpretation.get("policy_review_ready") is not True:
        fail("F11b must mark namespace evidence ready for separate policy review")
    if interpretation.get("m11_exact_fsync_qualification_complete") is not True:
        fail("M11b exact-fsync qualification completion was lost")
    if interpretation.get("m11b_required") is not False:
        fail("M11b must no longer remain required after F11b freeze")
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
        "contract-complete candidate for policy review",
        "m10 shows fresh-install process-crash replay",
        "linux requires explicit containing-directory fsync",
        "hierarchy child and destination parent via dm-flakey",
        "containing namespace parent via an exact syscall probe",
        "source staging parent via a full-flow exact call-6 probe",
        "every measured failure remains fail-closed before authority",
        "does not select production namespace sequencing",
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
        "historical m11 hierarchy/source fsync-specific interpretation remains superseded",
        "exact syscall error propagation and fail-closed control flow",
        "not block-device writeback",
        "authority-wide exclusion",
    ):
        if phrase not in limitations:
            fail(f"reviewed limitation lost: {phrase}")

    print("durability-v2 evidence: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
