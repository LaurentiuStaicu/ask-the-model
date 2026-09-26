#!/usr/bin/env python3

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / "qualification" / "c1-gc-policy-v1.json"


def fail(message: str) -> None:
    print(f"C1-P1 policy validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def require_equal(actual, expected, context: str) -> None:
    if actual != expected:
        fail(f"{context} drifted")


def require_marker(text: str, marker: str, context: str) -> int:
    pos = text.find(marker)
    if pos < 0:
        fail(f"{context} lost marker: {marker}")
    return pos


def main() -> int:
    policy = json.loads(POLICY.read_text(encoding="utf-8"))
    coordination = json.loads(
        (ROOT / "qualification" / "c1-coordination-policy-v1.json")
        .read_text(encoding="utf-8")
    )
    lifecycle = (
        ROOT / "src" / "RepositoryLifecycleService.vala"
    ).read_text(encoding="utf-8")
    application = (
        ROOT / "src" / "Application.vala"
    ).read_text(encoding="utf-8")
    grounding = (
        ROOT / "src" / "ConversationGrounding.vala"
    ).read_text(encoding="utf-8")
    session = (
        ROOT / "src" / "ConversationSession.vala"
    ).read_text(encoding="utf-8")
    roots = (
        ROOT / "src" / "RepositoryGcDurableRoots.vala"
    ).read_text(encoding="utf-8")
    candidates = (
        ROOT / "src" / "RepositoryGcCandidates.vala"
    ).read_text(encoding="utf-8")
    isolation = (
        ROOT / "src" / "repository_gc_isolation.c"
    ).read_text(encoding="utf-8")
    purge = (
        ROOT / "src" / "repository_gc_purge.c"
    ).read_text(encoding="utf-8")
    orchestrator = (
        ROOT / "src" / "RepositoryGcIsolationOrchestrator.vala"
    ).read_text(encoding="utf-8")
    meson = (
        ROOT / "meson.build"
    ).read_text(encoding="utf-8")
    orchestrator_tests = (
        ROOT / "tests" / "repository_gc_isolation_orchestrator_test.vala"
    ).read_text(encoding="utf-8")
    lifecycle_tests = (
        ROOT / "tests" / "repository_lifecycle_service_test.vala"
    ).read_text(encoding="utf-8")

    require_equal(policy.get("schema_version"), 1, "schema version")
    require_equal(
        policy.get("policy_id"),
        "atm-c1-snapshot-gc-orchestration-v1",
        "policy id",
    )
    require_equal(
        policy.get("status"),
        "selected-runtime-isolation-wired-purge-unwired",
        "policy status",
    )

    scope = policy.get("scope", {})
    require_equal(scope.get("optimizations_required"), True, "G0 gate")
    require_equal(
        scope.get("runtime_isolation_authorized"),
        True,
        "runtime isolation authorization",
    )
    require_equal(
        scope.get("destructive_gc_authorized"),
        True,
        "bounded isolation authorization",
    )
    for key in (
        "automatic_enospc_gc_authorized",
        "conversation_delete_cascade_authorized",
        "quarantine_purge_authorized",
        "retrieval_index_eviction_authorized",
        "control_db_generation_pruning_authorized",
    ):
        require_equal(scope.get(key), False, f"scope.{key}")

    require_equal(
        policy.get("isolation_protocol"),
        [
            "REQUIRE_OPTIMIZATIONS_ON_OPERATION_SNAPSHOT",
            "ACQUIRE_B0_GLOBAL_MUTATION_EXCLUSIVE",
            "COLLECT_FRESH_DURABLE_AND_LIVE_ROOTS",
            "DISCOVER_OR_REVALIDATE_UNPROTECTED_EXACT_SHA_CANDIDATE",
            "ENUMERATE_ALL_COMPLETE_GENERATIONS_REFERENCING_CANDIDATE",
            "SORT_REFERENCING_GENERATION_IDS_ASCENDING",
            "ACQUIRE_B2_EXCLUSIVE_FOR_EVERY_REFERENCING_POSITIVE_GENERATION",
            "REREAD_DURABLE_CONVERSATION_ROOTS_AFTER_ALL_B2_EXCLUSIONS",
            "ABORT_IF_CANDIDATE_IS_NOW_PROTECTED",
            "REVALIDATE_EXACT_SOURCE_WITH_NOFOLLOW_DIRFD",
            "I4_SAME_FILESYSTEM_ISOLATE_TO_TRASH",
            "RELEASE_B2_EXCLUSIONS",
            "RELEASE_B0_GLOBAL_MUTATION_EXCLUSIVE",
        ],
        "isolation protocol",
    )

    reference = policy.get("generation_reference_resolution", {})
    require_equal(
        reference.get("authority_source"),
        "STRICT_READONLY_COMPLETE_CONTROL_DB_GENERATIONS",
        "reference authority",
    )
    require_equal(
        reference.get("identity_key"),
        "repository_id+snapshot_sha",
        "candidate identity",
    )
    require_equal(
        reference.get("include_all_complete_generations"),
        True,
        "all COMPLETE generations",
    )
    require_equal(
        reference.get("generation_zero_excluded"),
        True,
        "generation-zero exclusion",
    )
    require_equal(
        reference.get("empty_reference_set_allowed"),
        True,
        "orphan reference-set rule",
    )
    require_equal(
        reference.get("exclusive_acquisition_order"),
        "ASCENDING_GENERATION_ID",
        "exclusive acquisition order",
    )

    race = policy.get("new_durable_root_race_closure", {})
    require_equal(
        race.get("required_invariant"),
        (
            "POSITIVE_GENERATION_CONVERSATION_PERSISTENCE_OCCURS_WHILE_"
            "MATCHING_B2_SHARED_LEASE_IS_HELD"
        ),
        "conversation-root lease invariant",
    )
    for key in (
        "existing_reader_blocks_gc_exclusive",
        "gc_exclusive_blocks_new_reader",
        "post_exclusion_durable_root_reread_required",
        "new_positive_generation_root_after_reread_blocked_by_b2_exclusive",
        "conversation_deletion_after_reread_is_overprotection_only",
    ):
        require_equal(race.get(key), True, f"race closure {key}")

    require_equal(
        policy.get("lock_order", {}).get("required"),
        [
            "B0_GLOBAL_MUTATION_EXCLUSIVE",
            "B2_EXCLUSIVE_ASCENDING_GENERATION_ID",
            "FILESYSTEM_EXACT_REVALIDATION_AND_I4_ISOLATION",
        ],
        "destructive lock order",
    )

    two_phase = policy.get("two_phase_reclamation", {})
    require_equal(
        two_phase.get("isolation_and_purge_same_logical_pass"),
        False,
        "two-phase separation",
    )
    require_equal(
        two_phase.get("purge_runtime_authorized"),
        False,
        "purge runtime authorization",
    )
    require_equal(
        two_phase.get("age_threshold_selected"),
        False,
        "purge age threshold",
    )
    require_equal(
        two_phase.get("automatic_restore_from_trash"),
        False,
        "trash restore policy",
    )

    trigger = policy.get("runtime_isolation_trigger_policy", {})
    require_equal(
        trigger.get("selected"),
        "POST_SUCCESSFUL_CHANGED_REPOSITORY_ACTION",
        "runtime isolation trigger",
    )
    for key in (
        "operation_snapshot_required",
        "optimizations_snapshot_must_be_on",
        "repository_action_must_succeed",
        "changed_repository_count_must_be_positive",
        "trigger_after_writer_b0_release",
        "use_same_operation_optimization_snapshot",
        "live_switch_recheck_for_trigger_forbidden",
        "loop_until_empty_forbidden",
        "purge_in_same_trigger_forbidden",
        "background_timer_or_idle_trigger_forbidden",
        "startup_trigger_forbidden",
        "optimization_toggle_trigger_forbidden",
        "conversation_close_delete_archive_trigger_forbidden",
        "enospc_or_capacity_failure_trigger_forbidden",
        "repository_refresh_trigger_forbidden",
    ):
        require_equal(trigger.get(key), True, f"runtime trigger {key}")

    require_equal(
        trigger.get("max_isolation_attempts_per_repository_action"),
        1,
        "bounded isolation attempts",
    )
    require_equal(
        trigger.get("candidate_scope"),
        "GLOBAL_DETERMINISTIC_I6_FIRST_CANDIDATE",
        "runtime candidate scope",
    )
    require_equal(
        trigger.get("ui_trigger_added"),
        False,
        "runtime trigger UI boundary",
    )
    require_equal(
        trigger.get("outcomes"),
        {
            "NOT_ENABLED": "NOOP",
            "B0_CONTENDED": "NOOP_SKIP_CLEANUP",
            "NO_CANDIDATE": "NOOP",
            "B2_CONTENDED": "NOOP_SKIP_CANDIDATE",
            "DURABLE_ROOT_APPEARED": "NOOP_SKIP_CANDIDATE",
            "ISOLATED": "ONE_SNAPSHOT_STAGED_TO_TRASH_NO_PURGE",
        },
        "runtime trigger outcomes",
    )
    require_equal(
        trigger.get("unexpected_failure"),
        (
            "DIAGNOSTIC_ONLY_ALREADY_COMMITTED_REPOSITORY_ACTION_REMAINS_SUCCESS"
        ),
        "runtime trigger failure boundary",
    )

    require_equal(
        trigger.get("runtime_authorized"),
        True,
        "runtime trigger authorization",
    )
    require_equal(
        trigger.get("conversation_store_unavailable"),
        "NOOP_SKIP_CLEANUP",
        "conversation-store unavailable handling",
    )
    require_equal(
        trigger.get("isolation_failure_does_not_change_repository_action_outcome"),
        True,
        "post-commit cleanup failure semantics",
    )
    require_equal(
        trigger.get("isolation_does_not_reclaim_trash_bytes"),
        True,
        "isolation versus purge boundary",
    )

    implementation = policy.get("implementation_state", {})
    require_equal(
        implementation,
        {
            "policy_selected": True,
            "dormant_orchestrator_implemented": True,
            "orchestrator_race_qualification_complete": True,
            "runtime_trigger_policy_selected": True,
            "bounded_runtime_isolation_qualification_complete": True,
            "runtime_caller_present": True,
            "destructive_gc_authorized": True,
            "purge_runtime_authorized": False,
            "next_required_slice": (
                "REVIEW_ISOLATED_TRASH_RETENTION_AND_PURGE_RUNTIME_POLICY"
            ),
        },
        "implementation state",
    )

    coord_state = coordination.get("implementation_state", {})
    require_equal(
        coord_state.get("writer_b0_always_on"),
        True,
        "authority-wide B0 prerequisite",
    )
    require_equal(
        coord_state.get("reader_b2_always_on"),
        True,
        "mode-independent B2 prerequisite",
    )
    require_equal(
        coordination.get("mixed_mode_qualification_complete"),
        True,
        "mixed-mode coordination qualification",
    )

    # The current runtime must preserve the lease lifetime needed to close the
    # new-durable-root race before a destructive caller is implemented.
    require_marker(
        grounding,
        "private RepositoryGenerationLease? generation_lease = null;",
        "grounding lease ownership",
    )
    require_marker(
        grounding,
        "generation_lease = lease;",
        "grounding shared lease retention",
    )
    require_marker(
        session,
        "grounding = prepared_grounding;",
        "session grounding ownership",
    )
    require_marker(
        session,
        "grounding = null;",
        "session lease release boundary",
    )

    persist_start = require_marker(
        application,
        "private bool ensure_persistent_conversation (",
        "conversation persistence entry",
    )
    persist_end = application.find(
        "\n        private string? immutable_permalink_for_citation",
        persist_start,
    )
    if persist_end < 0:
        fail("conversation persistence method boundary is unavailable")
    persist = application[persist_start:persist_end]
    for marker in (
        "!state.session.is_active ()",
        "conversation_store.create_conversation (",
        "state.session.repository_generation_id ()",
        "state.session.repository_pin_at (i)",
    ):
        require_marker(
            persist,
            marker,
            "positive-generation persistence under active session",
        )

    # Existing readonly/root and storage primitives must remain available.
    for marker in (
        "ControlStateNative.\n                    list_complete_generation_ids_readonly (",
        "RepositoryNative.\n                        try_acquire_generation_lease_exclusive (",
        "RepositoryNative.release_generation_lease (",
    ):
        require_marker(roots, marker, "C1 root/live primitives")
    require_marker(
        candidates,
        "RepositoryGcCandidate",
        "candidate discovery",
    )
    for marker in (
        "atm_repository_gc_isolate_snapshot_to_trash",
        "RENAME_NOREPLACE",
        "O_NOFOLLOW",
    ):
        require_marker(isolation, marker, "I4 isolation primitive")
    for marker in (
        "atm_repository_gc_purge_trash_entry",
        "collect_directory_entry_names",
        "unlinkat (",
    ):
        require_marker(purge, marker, "I5 purge primitive")

    # I6 is implemented but remains dormant. Lock and revalidation order are
    # structural invariants in addition to the native/Valac race tests.
    ordered_orchestrator_markers = [
        "try_acquire_mutation_lease (",
        "RepositoryGcDurableRootCollector.\n                        collect (",
        "RepositoryGcCandidateDiscovery.\n                        discover (",
        "int64[] references =\n                    referencing_generations (",
        "try_acquire_generation_lease_exclusive (",
        "\"after-b2-exclusions\"",
        "RepositoryGcDurableRootCollector.\n                        collect_durable_only (",
        "durable_roots.protects_snapshot (",
        "gc_isolate_snapshot_to_trash (",
    ]
    previous = -1
    for marker in ordered_orchestrator_markers:
        current = require_marker(
            orchestrator,
            marker,
            "I6 isolation ordering",
        )
        if current <= previous:
            fail(f"I6 isolation ordering drifted at marker: {marker}")
        previous = current

    for marker in (
        "sort_generation_ids_ascending (",
        "release_generation_exclusions (",
        "RepositoryGcIsolationOutcome.B2_CONTENDED",
        "Contended B2 exclusion unexpectedly returned an owned file descriptor.",
        "RepositoryGcIsolationOutcome.DURABLE_ROOT_APPEARED",
    ):
        require_marker(orchestrator, marker, "I6 race closure")

    if "gc_purge" in orchestrator or "purge_trash" in orchestrator:
        fail("I6 orchestrator must not invoke phase-2 purge")

    for marker in (
        "'src/RepositoryGcIsolationOrchestrator.vala'",
        "'tests/repository_gc_isolation_orchestrator_test.vala'",
        "'repository-gc-isolation-orchestrator'",
    ):
        require_marker(meson, marker, "I6 Meson qualification wiring")

    for marker in (
        '"/repository-gc-i6/off-noop"',
        '"/repository-gc-i6/b0-contention-noop"',
        '"/repository-gc-i6/no-candidate-noop"',
        '"/repository-gc-i6/orphan-zero-generation-reference"',
        '"/repository-gc-i6/unrooted-historical-isolated"',
        '"/repository-gc-i6/late-reader-blocks-exclusive"',
        '"/repository-gc-i6/late-durable-root-reread"',
        '"/repository-gc-i6/exclusive-blocks-new-reader"',
        '"/repository-gc-i6/partial-exclusions-released"',
        '"/repository-gc-i6/durable-only-under-owned-exclusive"',
    ):
        require_marker(
            orchestrator_tests,
            marker,
            "I6 deterministic race coverage",
        )

    # I7 wires exactly one bounded post-successful-mutation isolation
    # trigger. The operation snapshot must be returned by the lifecycle
    # mutation itself rather than reread from the live switch after await.
    lifecycle_context_start = require_marker(
        lifecycle,
        "download_or_update_with_context (",
        "I7 operation context API",
    )
    lifecycle_private_start = require_marker(
        lifecycle,
        "private async uint download_or_update_for_operation (",
        "I7 operation implementation",
    )
    if lifecycle_context_start >= lifecycle_private_start:
        fail("I7 operation context wrapper must precede private mutation body")

    context_slice = lifecycle[
        lifecycle_context_start:lifecycle_private_start
    ]
    for marker in (
        "bool optimized_operation =\n                optimization_mode_snapshot ();",
        "yield download_or_update_for_operation (",
        "new RepositoryMutationOutcome (",
        "changed,\n                optimized_operation",
    ):
        require_marker(
            context_slice,
            marker,
            "I7 operation snapshot carry",
        )

    private_end = lifecycle.find(
        "\n        }\n\n    }",
        lifecycle_private_start,
    )
    if private_end < 0:
        private_end = len(lifecycle)
    mutation_slice = lifecycle[
        lifecycle_private_start:private_end
    ]
    if "optimization_mode_snapshot (" in mutation_slice:
        fail("I7 private mutation body rereads live Optimizations state")

    cleanup_start = require_marker(
        lifecycle,
        "run_post_mutation_isolation (",
        "I7 cleanup seam",
    )
    cleanup_end = lifecycle.find(
        "\n\n        private async uint download_or_update_for_operation",
        cleanup_start,
    )
    if cleanup_end < 0:
        fail("I7 cleanup seam boundary is unavailable")
    cleanup_slice = lifecycle[cleanup_start:cleanup_end]

    for marker in (
        "!operation.optimized_operation ||\n                operation.changed == 0",
        "RepositoryGcIsolationOrchestrator.\n                    isolate_one (",
        "failure = error.message;",
        "return null;",
    ):
        require_marker(
            cleanup_slice,
            marker,
            "I7 bounded cleanup seam",
        )

    if "optimization_mode_snapshot (" in cleanup_slice:
        fail("I7 cleanup seam rereads live Optimizations state")
    if "gc_purge" in cleanup_slice or "purge_trash" in cleanup_slice:
        fail("I7 cleanup seam must not invoke I5 purge")
    if lifecycle.count("RepositoryGcIsolationOrchestrator") != 1:
        fail("I7 lifecycle must contain exactly one orchestrator call site")

    app_cleanup_start = require_marker(
        application,
        "private void run_post_mutation_isolation_best_effort (",
        "I7 Application cleanup wrapper",
    )
    app_action_start = require_marker(
        application,
        "private async void download_or_update_selected_repositories ()",
        "I7 repository action",
    )
    if app_cleanup_start >= app_action_start:
        fail("I7 cleanup wrapper must precede repository action")

    app_cleanup = application[
        app_cleanup_start:app_action_start
    ]
    for marker in (
        "!operation.optimized_operation ||\n                operation.changed == 0",
        "ConversationPersistenceStore? store =\n                conversation_store;",
        "run_post_mutation_isolation (",
        "repository isolation maintenance skipped",
        "repository isolation maintenance failed after committed repository action",
    ):
        require_marker(
            app_cleanup,
            marker,
            "I7 best-effort cleanup wrapper",
        )
    if "optimization_policy" in app_cleanup or "snapshot_enabled (" in app_cleanup:
        fail("I7 Application cleanup wrapper rereads live Optimizations state")

    app_action_end = application.find(
        "\n        private Gtk.CheckButton build_repository_check_button",
        app_action_start,
    )
    if app_action_end < 0:
        fail("I7 repository action boundary is unavailable")
    app_action = application[app_action_start:app_action_end]

    ordered_action_markers = [
        "download_or_update_with_context (",
        "finish_repository_operation (\n                    RepositoryOperationOutcome.NORMAL",
        "run_post_mutation_isolation_best_effort (",
    ]
    previous = -1
    for marker in ordered_action_markers:
        current = app_action.find(marker, previous + 1)
        if current < 0:
            fail(
                "I7 post-mutation trigger ordering lost marker after "
                f"position {previous}: {marker}"
            )
        previous = current

    for forbidden in (
        "optimization_policy.snapshot_enabled",
        "optimization_mode_snapshot (",
        "gc_purge",
        "purge_trash",
    ):
        if forbidden in app_action:
            fail(f"I7 repository action contains forbidden marker: {forbidden}")

    if application.count(
        "run_post_mutation_isolation_best_effort ("
    ) != 2:
        fail("I7 Application must define and invoke the cleanup wrapper once")

    if "RepositoryGcIsolationOrchestrator" in application:
        fail("I7 Application must call GC only through lifecycle seam")

    for forbidden in (
        "atm_repository_gc_purge_trash_entry",
        "gc_purge",
        "purge_trash",
    ):
        if forbidden in lifecycle:
            fail(f"I7 lifecycle must not invoke phase-2 purge: {forbidden}")

    for marker in (
        "RepositoryMutationOutcome off_context =",
        "assert (!off_context.optimized_operation);",
        "RepositoryMutationOutcome on_context =",
        "assert (on_context.optimized_operation);",
        "carried_on_cleanup.outcome ==",
        "RepositoryGcIsolationOutcome.B0_CONTENDED",
        "assert (failed_cleanup == null);",
        "assert (cleanup_failure != null);",
    ):
        require_marker(
            lifecycle_tests,
            marker,
            "I7 lifecycle trigger qualification",
        )

    print(
        "C1-I7 validation passed: one post-successful changed repository "
        "action isolation attempt is wired under the carried ON snapshot; "
        "cleanup failure is diagnostic-only; purge remains unwired"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
