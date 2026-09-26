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

    require_equal(policy.get("schema_version"), 1, "schema version")
    require_equal(
        policy.get("policy_id"),
        "atm-c1-snapshot-gc-orchestration-v1",
        "policy id",
    )
    require_equal(
        policy.get("status"),
        "selected-runtime-unwired",
        "policy status",
    )

    scope = policy.get("scope", {})
    require_equal(scope.get("optimizations_required"), True, "G0 gate")
    for key in (
        "destructive_gc_authorized",
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

    implementation = policy.get("implementation_state", {})
    require_equal(
        implementation,
        {
            "policy_selected": True,
            "dormant_orchestrator_implemented": True,
            "orchestrator_race_qualification_complete": True,
            "runtime_caller_present": False,
            "destructive_gc_authorized": False,
            "next_required_slice": "SELECT_RUNTIME_ISOLATION_TRIGGER_POLICY",
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

    # I6 remains dormant: lifecycle activation and phase-2 purge are forbidden.
    for marker in (
        "RepositoryGcCandidateDiscovery",
        "atm_repository_gc_isolate_snapshot_to_trash",
        "atm_repository_gc_purge_trash_entry",
        "RepositoryGcIsolationOrchestrator",
    ):
        if marker in lifecycle:
            fail(f"P1 introduced destructive lifecycle marker: {marker}")

    if "RepositoryGcIsolationOrchestrator" in application:
        fail("I6 introduced a direct Application runtime caller")

    print(
        "C1-I6 policy validation passed: B0 -> all relevant B2 EX -> "
        "durable-only post-exclusion reread -> exact I4 isolation; "
        "race qualification complete; purge separate; runtime still unwired"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
