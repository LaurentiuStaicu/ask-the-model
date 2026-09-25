#!/usr/bin/env python3

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY_PATH = ROOT / "qualification" / "durability-policy-v1.json"
EVIDENCE_PATH = ROOT / "benchmarks" / "durability-v1" / "evidence.json"


def fail(message: str) -> None:
    raise SystemExit(
        f"durability production policy validation failed: {message}"
    )


def load(path: pathlib.Path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    policy = load(POLICY_PATH)
    evidence = load(EVIDENCE_PATH)

    if policy.get("schema_version") != 1:
        fail("schema_version must remain 1")
    if policy.get("status") != "selected-not-wired":
        fail("P1 must remain selected-not-wired")
    if policy.get("selected_strategy") != "S1_TARGETED_FSYNC":
        fail("selected durability strategy drifted")
    if policy.get("production_barrier_selected") is not True:
        fail("P1 must select one production barrier")
    if policy.get("runtime_integration_selected") is not False:
        fail("P1 must not wire runtime behavior")

    gate = policy.get("runtime_gate")
    if gate != {
        "name": "Optimizations",
        "default_enabled": False,
        "operation_snapshot_required": True,
    }:
        fail("runtime gate contract drifted")

    if evidence.get("status") != "qualification-only":
        fail("durability evidence status drifted")
    if evidence.get("production_barrier_selected") is not False:
        fail("historical evidence must not be rewritten as policy")

    basis = policy.get("selection_basis")
    if not isinstance(basis, dict):
        fail("selection_basis is missing")
    if basis.get("correctness_tie_between_s1_s2") is not True:
        fail("reviewed S1/S2 correctness tie was lost")
    if basis.get("automatic_s1_to_s2_fallback") is not False:
        fail("automatic S1->S2 fallback must remain disabled")
    if basis.get("s1_targeted_fsync", {}).get("selected") is not True:
        fail("S1 selection was lost")
    if basis.get("s2_syncfs", {}).get("selected") is not False:
        fail("S2 must remain unselected")
    if basis.get("s2_syncfs", {}).get("role") != "BENCHMARK_AND_REFERENCE":
        fail("S2 reference role drifted")
    if basis.get("s3_baseline", {}).get("selected") is not False:
        fail("falsified S3 baseline cannot be selected")

    reviewed_cost = policy.get("m1_reviewed_cost_us")
    expected_cost = {
        "cbd": {
            "s1_total_over_control": 30348,
            "s2_total_over_control": 2471,
            "s1_minus_s2": 27877,
        },
        "ewd": {
            "s1_total_over_control": 55321,
            "s2_total_over_control": 25514,
            "s1_minus_s2": 29807,
        },
        "rmd": {
            "s1_total_over_control": 285974,
            "s2_total_over_control": 91671,
            "s1_minus_s2": 194303,
        },
    }
    if reviewed_cost != expected_cost:
        fail("M1 reviewed cost table drifted")

    summary = evidence.get("summary")
    if not isinstance(summary, dict):
        fail("M1 summary is missing")
    for rid, expected in expected_cost.items():
        record = summary.get(rid)
        if not isinstance(record, dict):
            fail(f"M1 summary missing {rid}")
        s1 = record.get("S1_TARGETED_FSYNC")
        s2 = record.get("S2_SYNCFS")
        if not isinstance(s1, dict) or not isinstance(s2, dict):
            fail(f"M1 strategy data missing for {rid}")

        s1_delta = s1.get("median_total_over_control_us")
        s2_delta = s2.get("median_total_over_control_us")
        if s1_delta != expected["s1_total_over_control"]:
            fail(f"{rid} S1 M1 cost drifted")
        if s2_delta != expected["s2_total_over_control"]:
            fail(f"{rid} S2 M1 cost drifted")
        if s1_delta - s2_delta != expected["s1_minus_s2"]:
            fail(f"{rid} S1-S2 M1 delta drifted")
        if s2_delta >= s1_delta:
            fail(f"{rid} reviewed S2<S1 performance relation drifted")

    m4 = evidence.get("tier2_atm_replay_baseline")
    if not isinstance(m4, dict):
        fail("M4 S3 baseline evidence is missing")
    if m4.get("strategy") != "S3_CURRENT_BASELINE":
        fail("M4 baseline strategy drifted")
    if m4.get("production_durability_authorized") is not False:
        fail("M4 baseline evidence must not authorize production durability")

    m4_scenarios = m4.get("scenarios")
    if not isinstance(m4_scenarios, dict):
        fail("M4 scenario matrix is missing")
    if set(m4_scenarios) != {
        "pre_rename",
        "post_rename",
        "after_authority",
    }:
        fail("M4 scenario set drifted")

    for boundary in ("pre_rename", "post_rename"):
        record = m4_scenarios[boundary]
        if record.get("observed_classification") != "OLD_AUTHORITY_VALID":
            fail(f"M4 {boundary} no longer preserves old authority")
        if record.get("seal_match") is not True:
            fail(f"M4 {boundary} old authority seal no longer matches")
        if record.get("strong_invariant_satisfied") is not True:
            fail(f"M4 {boundary} strong invariant was lost")

    after_authority = m4_scenarios["after_authority"]
    if after_authority.get("expected_classification") != "NEW_AUTHORITY_VALID":
        fail("M4 after-authority expectation drifted")
    if after_authority.get("observed_classification") != "INVALID_AUTHORITY":
        fail("M4 S3 falsification classification drifted")
    if after_authority.get("active_generation_id") != 2:
        fail("M4 falsifying active generation drifted")
    if after_authority.get("active_repository_sha") != (
        "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
    ):
        fail("M4 falsifying active SHA drifted")
    if after_authority.get("stored_seal") != (
        "acebf979895f9efd073014fa707038d47ec71b411f4e12a32aa619e9c63f3132"
    ):
        fail("M4 stored new-authority seal drifted")
    if after_authority.get("computed_seal") != (
        "7299f3e88c9a88995a62ef415e0da959ba4571df5f62976e9f9ac49d4660a3d8"
    ):
        fail("M4 replayed new-snapshot seal drifted")
    if after_authority.get("stored_seal") == after_authority.get("computed_seal"):
        fail("M4 falsification lost the stored/replayed seal mismatch")
    if after_authority.get("seal_match") is not False:
        fail("M4 falsifying seal_match must remain false")
    if after_authority.get("reason_code") != "active_snapshot_seal_mismatch":
        fail("M4 falsification reason drifted")
    if after_authority.get("strong_invariant_satisfied") is not False:
        fail("M4 falsification must retain strong_invariant_satisfied=false")

    m4_conclusion = str(m4.get("reviewed_conclusion", "")).lower()
    for phrase in (
        "s3 is therefore falsified",
        "selects neither s1 nor s2",
        "authorizes no production durability barrier",
    ):
        if phrase not in m4_conclusion:
            fail(f"M4 reviewed conclusion lost: {phrase}")

    m5 = evidence.get("tier2_candidate_replay")
    if not isinstance(m5, dict):
        fail("M5 candidate evidence is missing")
    m5_conclusion = str(m5.get("reviewed_conclusion", "")).lower()
    for phrase in (
        "s1 targeted fsync and s2 syncfs both satisfy",
        "does not select a production barrier",
    ):
        if phrase not in m5_conclusion:
            fail(f"M5 reviewed conclusion lost: {phrase}")

    m6 = evidence.get("tier2_candidate_boundary_replay")
    if not isinstance(m6, dict):
        fail("M6 candidate boundary evidence is missing")
    if m6.get("all_strong_invariants_satisfied") is not True:
        fail("M6 strong invariant aggregate was lost")
    if m6.get("all_expected_classifications_match") is not True:
        fail("M6 expected classification aggregate was lost")

    m8 = evidence.get("tier2_candidate_error_injection")
    if not isinstance(m8, dict):
        fail("M8 candidate EIO evidence is missing")
    for key in (
        "all_candidates_failed_on_eio",
        "all_old_authority_preserved",
        "all_fail_closed",
    ):
        if m8.get(key) is not True:
            fail(f"M8 reviewed aggregate lost: {key}")

    m9 = evidence.get("tier2_same_sha_repair")
    if not isinstance(m9, dict):
        fail("M9 same-SHA repair evidence is missing")
    for key in (
        "all_repair_invariants_satisfied",
        "all_pre_authority_fail_closed",
        "all_after_authority_repaired_valid",
    ):
        if m9.get(key) is not True:
            fail(f"M9 reviewed aggregate lost: {key}")
    if m9.get("protocol", {}).get("quarantine_parent_fsync_added") is not False:
        fail("M9 quarantine must remain qualified without extra parent fsync")

    evidence_refs = policy.get("qualified_evidence")
    expected_refs = {
        "performance_and_scope": "benchmarks/durability-v1/evidence.json#summary",
        "s3_falsification": (
            "benchmarks/durability-v1/evidence.json#tier2_atm_replay_baseline"
        ),
        "candidate_after_authority": (
            "benchmarks/durability-v1/evidence.json#tier2_candidate_replay"
        ),
        "candidate_boundary_matrix": (
            "benchmarks/durability-v1/evidence.json#tier2_candidate_boundary_replay"
        ),
        "candidate_eio": (
            "benchmarks/durability-v1/evidence.json#tier2_candidate_error_injection"
        ),
        "same_sha_repair": (
            "benchmarks/durability-v1/evidence.json#tier2_same_sha_repair"
        ),
    }
    if evidence_refs != expected_refs:
        fail("qualified evidence references drifted")

    storage = policy.get("storage_contract")
    if storage != {
        "platform": "Linux",
        "tier2_qualified_filesystem": "ext4",
        "production_scope": "LOCAL_FILESYSTEM",
        "regular_file_fsync_required": True,
        "directory_fsync_required": True,
        "network_or_remote_filesystem_durability_claim": False,
        "on_mode_unsupported_or_failed_barrier": "FAIL_CLOSED",
        "off_mode_preserves_baseline": True,
        "note": (
            "The selected calls are narrower than syncfs but are not claimed "
            "to isolate AtM from every storage-level writeback error."
        ),
    }:
        fail("storage contract drifted")

    expected_order = [
        "EXTRACT_TO_STAGING",
        "VALIDATE_IDENTITY_AND_VERSION",
        "COMPUTE_PRE_BARRIER_SNAPSHOT_SEAL",
        "FSYNC_EVERY_REGULAR_FILE_IN_STAGING_TREE",
        "FSYNC_DIRECTORIES_BOTTOM_UP_INCLUDING_STAGING_ROOT",
        "ATOMIC_RENAME_STAGING_TO_FINAL",
        "FSYNC_FINAL_SNAPSHOT_PARENT",
        "BUILD_OR_VALIDATE_DERIVED_RETRIEVAL_INDEX",
        "COMPUTE_POST_PREPARE_SNAPSHOT_SEAL",
        "REQUIRE_PRE_AND_POST_SEALS_EQUAL",
        "STATE_CAPACITY_RECHECK",
        "GUARDED_CONTROL_DB_SET_CURRENT",
    ]
    if policy.get("normal_promotion_order") != expected_order:
        fail("selected promotion ordering drifted")

    repair_policy = policy.get("same_sha_repair")
    if repair_policy != {
        "quarantine_parent_fsync_selected": False,
        "pre_authority_state": "REPAIR_REQUIRED",
        "post_authority_state": "REPAIRED_AUTHORITY_VALID",
        "replacement_uses_selected_s1_order": True,
    }:
        fail("same-SHA repair policy drifted")

    retry = policy.get("retry_recovery")
    if retry != {
        "preexisting_final_snapshot_may_bypass_barrier": False,
        "unreferenced_recovery_artifact_action": (
            "REBUILD_THROUGH_SELECTED_S1_SEQUENCE_ONLY_AFTER_NON_PROTECTION_"
            "IS_ESTABLISHED"
        ),
        "unresolved_or_protected_preexisting_target": (
            "FAIL_CLOSED_WITHOUT_REMOVAL_OR_AUTHORITY_ADVANCE"
        ),
        "future_final_tree_requalification_requires_separate_qualification": True,
        "reason": (
            "Final-directory existence is not durability evidence. A target proven "
            "to be an unreferenced recovery artifact may be isolated or removed "
            "and rebuilt through the qualified staging sequence; a target that is "
            "protected, historically required, or not proven disposable must "
            "remain untouched and fail closed until a separately qualified "
            "final-tree requalification path exists."
        ),
    }:
        fail("preexisting-final retry policy drifted")

    retrieval = policy.get("retrieval_index")
    if retrieval != {
        "authority_role": "DERIVED_CACHE",
        "inherits_snapshot_authority_barrier": False,
        "new_durability_barrier_selected": False,
        "recovery_contract": "VALID_OR_ABSENT_REBUILDABLE",
        "reason": (
            "A1-P1 selects durability for repository snapshot authority only. "
            "The deterministic retrieval index remains governed by its "
            "existing validation, single-flight and rebuild recovery contract."
        ),
    }:
        fail("retrieval-index durability scope drifted")

    failure = policy.get("failure_contract")
    expected_failure = {
        "pre_rename_barrier_failure": (
            "ABORT_WITHOUT_PROMOTION_OR_AUTHORITY_ADVANCE"
        ),
        "promotion_parent_fsync_failure": (
            "ABORT_WITHOUT_AUTHORITY_ADVANCE_AND_TREAT_FINAL_AS_"
            "UNREFERENCED_RECOVERY_ARTIFACT"
        ),
        "control_db_activation_failure": (
            "ABORT_WITHOUT_NEW_AUTHORITY_AND_TREAT_FINAL_AS_"
            "UNREFERENCED_RECOVERY_ARTIFACT"
        ),
        "preexisting_target_reference_unresolved": (
            "FAIL_CLOSED_WITHOUT_REMOVAL_OR_AUTHORITY_ADVANCE"
        ),
        "same_sha_repair_pre_authority_failure": (
            "REMAIN_FAIL_CLOSED_REPAIR_REQUIRED"
        ),
        "automatic_syncfs_fallback": False,
    }
    if failure != expected_failure:
        fail("durability failure contract drifted")

    print(
        "durability production policy validation passed: "
        "S1 targeted fsync selected, S2 reference-only, "
        "runtime not wired"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
