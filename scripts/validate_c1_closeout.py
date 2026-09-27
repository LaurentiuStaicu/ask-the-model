#!/usr/bin/env python3

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"C1 closeout validation failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def main() -> int:
    closeout = json.loads(
        (ROOT / "qualification" / "c1-closeout-v1.json").read_text(
            encoding="utf-8"
        )
    )
    policy = json.loads(
        (ROOT / "qualification" / "c1-gc-policy-v1.json").read_text(
            encoding="utf-8"
        )
    )
    evidence = json.loads(
        (ROOT / "benchmarks" / "c1-reclamation-v1" / "evidence.json").read_text(
            encoding="utf-8"
        )
    )

    require(closeout.get("schema_version") == 1, "schema version drifted")
    require(
        closeout.get("closeout_id") == "atm-c1-snapshot-gc-closeout-v1",
        "closeout id drifted",
    )
    require(closeout.get("status") == "complete", "closeout is not complete")
    require(policy.get("status") == "qualified-c1-complete", "policy not closed")

    matrix = closeout.get("original_test_matrix", {})
    expected_tests = {
        "C1-T1_active_generation_protected",
        "C1-T2_saved_history_conversation_protected",
        "C1-T3_live_lease_protected",
        "C1-T4_unrooted_historical_snapshot_isolated",
        "C1-T5_crash_after_trash_rename",
        "C1-T6_purge_interruption",
        "C1-T7_root_appears_before_isolation",
        "C1-T8_malformed_or_symlink_candidate",
        "C1-T9_conversation_permanently_deleted_no_sync_cascade",
    }
    require(set(matrix) == expected_tests, "original C1 test matrix drifted")
    for name, item in matrix.items():
        require(item.get("status") == "qualified", f"{name} not qualified")
        require(bool(item.get("coverage")), f"{name} has no coverage mapping")

    runtime = closeout.get("runtime_policy", {})
    for key in (
        "optimizations_required",
        "bounded_post_repository_action_only",
        "steady_state_no_growth",
    ):
        require(runtime.get(key) is True, f"runtime policy {key} drifted")
    require(
        runtime.get("max_purge_attempts_per_action") == 1,
        "purge bound drifted",
    )
    require(
        runtime.get("max_isolation_attempts_per_action") == 1,
        "isolation bound drifted",
    )
    for key in (
        "background_gc",
        "startup_gc",
        "toggle_gc",
        "enospc_triggered_gc",
        "conversation_delete_cascade",
        "quarantine_purge",
        "retrieval_index_eviction",
        "control_db_generation_pruning",
    ):
        require(runtime.get(key) is False, f"forbidden runtime policy enabled: {key}")

    reclamation = closeout.get("reclamation", {})
    require(
        reclamation.get("space_benefit_qualification_complete") is True,
        "reclamation qualification missing",
    )
    require(
        reclamation.get("evidence")
        == "benchmarks/c1-reclamation-v1/evidence.json",
        "reclamation evidence path drifted",
    )
    require(
        reclamation.get("controlled_filesystems") == ["tmpfs", "ext4"],
        "reclamation filesystem evidence drifted",
    )
    require(
        reclamation.get("production_reclaim_threshold_selected") is False,
        "closeout selected a reclaim threshold",
    )
    require(
        evidence.get("production_thresholds_selected") is False,
        "M1 evidence selected a reclaim threshold",
    )

    accounting = closeout.get("space_accounting_closeout", {})
    for key in (
        "candidate_identity_reported_runtime",
        "isolated_trash_identity_reported_runtime",
        "purge_identity_and_outcome_reported_runtime",
        "future_observability_must_not_change_reachability_or_trigger_policy",
    ):
        require(accounting.get(key) is True, f"accounting {key} drifted")
    for key in (
        "runtime_recursive_byte_measurement_selected",
        "protected_bytes_by_root_class_selected",
    ):
        require(accounting.get(key) is False, f"accounting {key} unexpectedly enabled")
    require(
        accounting.get("decision")
        == "DEFER_OPTIONAL_OBSERVABILITY_NOT_REQUIRED_FOR_C1_CORRECTNESS",
        "observability closeout decision drifted",
    )

    non_goals = closeout.get("non_goals_preserved", {})
    require(non_goals and all(non_goals.values()), "C1 non-goal boundary drifted")

    decision = closeout.get("closeout_decision", {})
    require(decision.get("c1_complete") is True, "C1 not marked complete")
    require(
        decision.get("destructive_scope_expansion_required") is False,
        "closeout unexpectedly requires destructive expansion",
    )
    require(
        decision.get("next_backend_workstream") == "OPT-D0",
        "next backend workstream drifted",
    )
    require(
        decision.get("next_backend_action")
        == "REBASELINE_RETRIEVAL_CONTEXT_EFFICIENCY_MEASUREMENT_ONLY",
        "next backend action drifted",
    )
    require(
        decision.get("d1_production_policy_change_authorized") is False,
        "D1 was authorized by C1 closeout",
    )

    implementation = policy.get("implementation_state", {})
    require(
        implementation.get("c1_closeout_complete") is True,
        "policy implementation state lacks C1 closeout",
    )
    require(
        implementation.get("next_required_slice")
        == "OPT_D0_REBASELINE_RETRIEVAL_CONTEXT_EFFICIENCY",
        "policy next slice is not D0",
    )

    print(
        "C1 closeout validation passed: original T1-T9 matrix is qualified, "
        "P5 no-growth bounded runtime purge remains constrained, M1 reclamation "
        "evidence is complete, optional recursive byte telemetry is deferred, "
        "and the next backend workstream is D0 measurement-only"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
