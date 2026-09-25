#!/usr/bin/env python3

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
POLICY_PATH = ROOT / "qualification" / "durability-policy-v1.json"
EVIDENCE_V1 = ROOT / "benchmarks" / "durability-v1" / "evidence.json"
EVIDENCE_V2 = ROOT / "benchmarks" / "durability-v2" / "evidence.json"


def fail(message: str) -> None:
    raise SystemExit(
        f"durability production policy validation failed: {message}"
    )


def load(path: pathlib.Path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    policy = load(POLICY_PATH)
    evidence = load(EVIDENCE_V1)
    namespace_evidence = load(EVIDENCE_V2)

    if policy.get("schema_version") != 1:
        fail("schema_version must remain 1")
    if policy.get("status") != "selected-not-wired":
        fail("policy must remain selected-not-wired")
    if policy.get("selected_strategy") != "S1_TARGETED_FSYNC":
        fail("selected durability strategy drifted")
    if policy.get("selected_namespace_strategy") != "S1_DEST_SOURCE":
        fail("selected namespace strategy drifted")
    if policy.get("production_barrier_selected") is not True:
        fail("production barrier selection was lost")
    if policy.get("production_namespace_sequence_selected") is not True:
        fail("selected namespace production sequence was lost")
    if policy.get("runtime_integration_selected") is not False:
        fail("namespace-policy correction must not wire runtime behavior")

    if policy.get("runtime_gate") != {
        "name": "Optimizations",
        "default_enabled": False,
        "operation_snapshot_required": True,
    }:
        fail("runtime gate contract drifted")

    if evidence.get("status") != "qualification-only":
        fail("durability-v1 evidence status drifted")
    if evidence.get("production_barrier_selected") is not False:
        fail("historical v1 evidence must remain evidence-only")
    if namespace_evidence.get("status") != "qualification-only":
        fail("durability-v2 evidence status drifted")
    if namespace_evidence.get(
        "production_namespace_sequence_selected"
    ) is not False:
        fail("F11b evidence must remain separate from P2b policy")

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

    namespace_basis = basis.get("namespace_durability")
    if not isinstance(namespace_basis, dict):
        fail("namespace selection basis is missing")
    if namespace_basis.get("selected") != "S1_DEST_SOURCE":
        fail("namespace policy selection drifted")
    if namespace_basis.get("candidate_for_review") != "S1_DEST_SOURCE":
        fail("namespace review candidate drifted")
    if namespace_basis.get("m10_replay_correctness") != "QUALIFIED":
        fail("M10 qualification was lost")
    if namespace_basis.get("m10_empirical_candidate_ranking") != "TIED":
        fail("M10 empirical candidate tie was lost")
    if namespace_basis.get("m11_eio_correctness") != "PARTIAL_SCOPE_REVIEWED":
        fail("historical M11 reviewed partial scope was lost")
    if namespace_basis.get("m11b_exact_fsync_correctness") != "QUALIFIED":
        fail("M11b exact-fsync qualification was lost")
    if namespace_basis.get("m11b_required") is not False:
        fail("M11b must no longer remain required after F11b")
    if namespace_basis.get("policy_review_ready") is not True:
        fail("namespace evidence must be ready for separate P2b review")
    if namespace_basis.get("linux_directory_entry_contract_driven") is not True:
        fail("Linux directory-entry contract basis was lost")
    if namespace_basis.get("selection_basis") != (
        "CONTRACT_DRIVEN_PLUS_EXACT_FAILURE_QUALIFICATION"
    ):
        fail("namespace policy selection basis drifted")
    namespace_reason = str(namespace_basis.get("reason", "")).lower()
    for phrase in (
        "did not empirically rank them",
        "linux nevertheless requires explicit fsync of containing directories",
        "frozen m11b qualifies the exact",
        "contract-complete namespace sequence",
        "without claiming that m10 falsified the shorter candidates",
    ):
        if phrase not in namespace_reason:
            fail(f"namespace review rationale lost: {phrase}")

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

    for key in (
        "tier2_atm_replay_baseline",
        "tier2_candidate_replay",
        "tier2_candidate_boundary_replay",
        "tier2_candidate_error_injection",
        "tier2_same_sha_repair",
    ):
        if not isinstance(evidence.get(key), dict):
            fail(f"durability-v1 evidence missing {key}")

    m4 = evidence["tier2_atm_replay_baseline"]
    if m4.get("strategy") != "S3_CURRENT_BASELINE":
        fail("M4 baseline strategy drifted")
    if m4.get("production_durability_authorized") is not False:
        fail("M4 baseline evidence must not authorize production durability")

    m6 = evidence["tier2_candidate_boundary_replay"]
    if m6.get("all_strong_invariants_satisfied") is not True:
        fail("M6 strong invariant aggregate was lost")
    if m6.get("all_expected_classifications_match") is not True:
        fail("M6 expected classification aggregate was lost")

    m8 = evidence["tier2_candidate_error_injection"]
    for key in (
        "all_candidates_failed_on_eio",
        "all_old_authority_preserved",
        "all_fail_closed",
    ):
        if m8.get(key) is not True:
            fail(f"M8 reviewed aggregate lost: {key}")

    m9 = evidence["tier2_same_sha_repair"]
    for key in (
        "all_repair_invariants_satisfied",
        "all_pre_authority_fail_closed",
        "all_after_authority_repaired_valid",
    ):
        if m9.get(key) is not True:
            fail(f"M9 reviewed aggregate lost: {key}")
    if m9.get("protocol", {}).get("quarantine_parent_fsync_added") is not False:
        fail("M9 quarantine must remain qualified without extra parent fsync")

    m10 = namespace_evidence.get("m10_replay")
    m11 = namespace_evidence.get("m11_eio")
    interpretation = namespace_evidence.get("interpretation", {})
    if not isinstance(m10, dict) or m10.get("scenario_count") != 18:
        fail("M10 frozen evidence is missing")
    if m10.get("all_e2fsck_recoverable") is not True:
        fail("M10 recoverability was lost")
    if not isinstance(m11, dict) or m11.get("scenario_count") != 3:
        fail("M11 frozen evidence is missing")
    if m11.get("all_authority_fail_closed") is not True:
        fail("M11 fail-closed aggregate was lost")
    if m11.get("all_e2fsck_recoverable") is not True:
        fail("M11 recoverability aggregate was lost")
    if m11.get("protocol", {}).get("candidate") != "S1_DEST_SOURCE":
        fail("M11 candidate drifted")
    if interpretation.get("candidate_for_policy_review") != "S1_DEST_SOURCE":
        fail("F11b policy-review candidate drifted")
    if interpretation.get("m11b_completed") is not True:
        fail("F11b M11b completion marker was lost")
    if interpretation.get("policy_review_ready") is not True:
        fail("F11b evidence must be ready for separate policy review")
    if interpretation.get("m11_exact_fsync_qualification_complete") is not True:
        fail("F11b exact-fsync completion marker was lost")
    if interpretation.get("m11b_required") is not False:
        fail("F11b must clear the M11b evidence requirement")
    if interpretation.get("production_namespace_sequence_selected") is not False:
        fail("F11b evidence must not select policy")
    if interpretation.get("automatic_orphan_recovery_authorized") is not False:
        fail("F11b must not authorize orphan recovery")

    m11b = namespace_evidence.get("m11b_exact_eio")
    if not isinstance(m11b, dict):
        fail("frozen M11b evidence is missing")
    for key in (
        "all_authority_fail_closed",
        "all_e2fsck_recoverable",
        "all_exact_error_contexts",
        "namespace_parent_fsync_probe_qualified",
        "source_parent_fsync_probe_qualified",
    ):
        if m11b.get(key) is not True:
            fail(f"M11b policy-review prerequisite lost: {key}")
    if m11b.get("production_namespace_selected") is not False:
        fail("M11b measurement must remain non-selecting evidence")
    if m11b.get("protocol", {}).get("candidate") != "S1_DEST_SOURCE":
        fail("M11b policy-review candidate drifted")

    evidence_refs = policy.get("qualified_evidence")
    expected_refs = {
        "performance_and_scope": "benchmarks/durability-v1/evidence.json#summary",
        "s3_falsification": "benchmarks/durability-v1/evidence.json#tier2_atm_replay_baseline",
        "candidate_after_authority": "benchmarks/durability-v1/evidence.json#tier2_candidate_replay",
        "candidate_boundary_matrix": "benchmarks/durability-v1/evidence.json#tier2_candidate_boundary_replay",
        "candidate_eio": "benchmarks/durability-v1/evidence.json#tier2_candidate_error_injection",
        "same_sha_repair": "benchmarks/durability-v1/evidence.json#tier2_same_sha_repair",
        "fresh_namespace_replay": "benchmarks/durability-v2/evidence.json#m10_replay",
        "fresh_namespace_eio": "benchmarks/durability-v2/evidence.json#m11_eio",
        "fresh_namespace_exact_eio": "benchmarks/durability-v2/evidence.json#m11b_exact_eio",
    }
    if evidence_refs != expected_refs:
        fail("qualified evidence references drifted")

    expected_order = [
        "EXTRACT_TO_STAGING",
        "VALIDATE_IDENTITY_AND_VERSION",
        "COMPUTE_PRE_BARRIER_SNAPSHOT_SEAL",
        "FSYNC_EVERY_REGULAR_FILE_IN_STAGING_TREE",
        "FSYNC_DIRECTORIES_BOTTOM_UP_INCLUDING_STAGING_ROOT",
        "DURABLY_PREPARE_FINAL_SNAPSHOT_PARENT_HIERARCHY",
        "ATOMIC_RENAME_STAGING_TO_FINAL",
        "FSYNC_FINAL_SNAPSHOT_PARENT",
        "FSYNC_STAGING_SOURCE_PARENT",
        "BUILD_OR_VALIDATE_DERIVED_RETRIEVAL_INDEX",
        "COMPUTE_POST_PREPARE_SNAPSHOT_SEAL",
        "REQUIRE_PRE_AND_POST_SEALS_EQUAL",
        "STATE_CAPACITY_RECHECK",
        "GUARDED_CONTROL_DB_SET_CURRENT",
    ]
    if policy.get("normal_promotion_order") != expected_order:
        fail("selected promotion ordering drifted")

    if policy.get("same_sha_repair") != {
        "quarantine_parent_fsync_selected": False,
        "pre_authority_state": "REPAIR_REQUIRED",
        "post_authority_state": "REPAIRED_AUTHORITY_VALID",
        "replacement_uses_selected_s1_order": True,
    }:
        fail("same-SHA repair policy drifted")

    retry = policy.get("retry_recovery")
    expected_retry = {
        "preexisting_final_snapshot_may_bypass_barrier": False,
        "i1b_zero_reference_authorizes_removal": False,
        "automatic_orphan_recovery_selected": False,
        "first_runtime_preexisting_final_action": (
            "FAIL_CLOSED_WITHOUT_REMOVAL_OR_AUTHORITY_ADVANCE"
        ),
        "authority_wide_exclusion_required_for_future_automatic_recovery": True,
        "future_final_tree_requalification_requires_separate_qualification": True,
        "reason": (
            "Final-directory existence is not durability evidence. I1b can prove "
            "only Control DB non-protection at one instant; the current "
            "Optimizations-ON mutation lease does not exclude Optimizations-OFF "
            "writers. Therefore the first runtime integration must not "
            "automatically remove or quarantine a preexisting final target. "
            "Future automatic orphan recovery requires authority-wide exclusion "
            "across every Control DB writer and the corresponding filesystem "
            "mutation window."
        ),
    }
    if retry != expected_retry:
        fail("preexisting-final retry policy drifted")

    expected_failure = {
        "pre_rename_barrier_failure": "ABORT_WITHOUT_PROMOTION_OR_AUTHORITY_ADVANCE",
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
        "destination_hierarchy_durability_failure": (
            "ABORT_WITHOUT_PROMOTION_OR_AUTHORITY_ADVANCE"
        ),
        "source_parent_fsync_failure": (
            "ABORT_WITHOUT_AUTHORITY_ADVANCE_AND_TREAT_FINAL_AS_"
            "UNREFERENCED_RECOVERY_ARTIFACT"
        ),
        "durable_data_root_invalid": (
            "ABORT_BEFORE_STAGING_OR_NAMESPACE_MUTATION"
        ),
    }
    if policy.get("failure_contract") != expected_failure:
        fail("durability failure contract drifted")

    implementation = policy.get("implementation_state")
    if implementation != {
        "namespace_policy_selected": True,
        "durable_ingest_namespace_complete": True,
        "runtime_wiring_authorized": False,
        "next_required_slice": (
            "LIFECYCLE_ON_ONLY_DURABLE_INGEST_ACTIVATION_REVIEW"
        ),
        "vala_durable_ingest_bridge_available": True,
    }:
        fail("implementation staging contract drifted")

    storage = policy.get("storage_contract")
    if not isinstance(storage, dict):
        fail("storage contract is missing")
    if storage.get("platform") != "Linux":
        fail("storage platform drifted")
    if storage.get("tier2_qualified_filesystem") != "ext4":
        fail("qualified filesystem drifted")
    if storage.get("directory_fsync_required") is not True:
        fail("directory fsync requirement was lost")
    if storage.get(
        "durable_data_root_preexisting_real_directory_required"
    ) is not True:
        fail("trusted durable data-root requirement was lost")
    if storage.get("on_mode_unsupported_or_failed_barrier") != "FAIL_CLOSED":
        fail("failed-barrier policy drifted")
    if storage.get("off_mode_preserves_baseline") is not True:
        fail("OFF baseline preservation drifted")

    retrieval = policy.get("retrieval_index")
    if not isinstance(retrieval, dict):
        fail("retrieval-index durability scope is missing")
    if retrieval.get("authority_role") != "DERIVED_CACHE":
        fail("retrieval index authority role drifted")
    if retrieval.get("inherits_snapshot_authority_barrier") is not False:
        fail("retrieval index must not inherit the snapshot barrier")

    print(
        "durability production policy validation passed: "
        "S1 + S1_DEST_SOURCE selected; dormant Vala bridge available; lifecycle remains unwired"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
