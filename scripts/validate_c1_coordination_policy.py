#!/usr/bin/env python3

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
POLICY = ROOT / "qualification" / "c1-coordination-policy-v1.json"


def fail(message: str) -> None:
    print(
        f"c1 coordination policy validation failed: {message}",
        file=sys.stderr,
    )
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
    lifecycle = (
        ROOT / "src" / "RepositoryLifecycleService.vala"
    ).read_text(encoding="utf-8")
    optimization = (
        ROOT / "src" / "OptimizationPolicy.vala"
    ).read_text(encoding="utf-8")
    generation_validator = (
        ROOT / "scripts" / "validate_generation_lease_wiring.py"
    ).read_text(encoding="utf-8")

    require_equal(policy.get("schema_version"), 1, "schema version")
    require_equal(
        policy.get("policy_id"),
        "atm-c1-authority-wide-coordination-v1",
        "policy id",
    )
    require_equal(
        policy.get("status"),
        "selected-not-implemented",
        "policy status",
    )

    scope = policy.get("scope", {})
    require_equal(
        scope.get("supported_storage"),
        "qualified local Linux AtM storage only",
        "supported storage",
    )
    require_equal(
        scope.get("destructive_gc_authorized"),
        False,
        "destructive GC authorization",
    )
    require_equal(
        scope.get("automatic_orphan_recovery_authorized"),
        False,
        "automatic orphan recovery authorization",
    )

    exemption = policy.get("g0_correctness_exemption", {})
    require_equal(exemption.get("selected"), True, "G0 exemption")
    require_equal(
        exemption.get("always_on_coordination_only"),
        [
            "B0_REPOSITORY_MUTATION_LEASE",
            "B2_POSITIVE_GENERATION_SHARED_LEASE",
        ],
        "always-on coordination set",
    )
    require_equal(
        exemption.get("still_optimization_gated"),
        [
            "A1_DURABILITY_BARRIERS",
            "B1_DERIVED_INDEX_SINGLE_FLIGHT",
            "C0_CAPACITY_ADMISSION",
            "C1_DESTRUCTIVE_GC",
            "D1_RETRIEVAL_CONTEXT_POLICY",
        ],
        "still-gated optimization set",
    )
    require_equal(
        exemption.get("optimization_switch_starts_off"),
        True,
        "default-OFF switch contract",
    )

    writer = policy.get("writer_coordination", {})
    require_equal(
        writer.get("lease"),
        "repository-mutation.lock",
        "writer lease",
    )
    require_equal(
        writer.get("required_for_modes"),
        ["OFF", "ON"],
        "writer modes",
    )
    require_equal(
        writer.get("required_for"),
        [
            "DOWNLOAD_UPDATE_AUTHORITY_MUTATION",
            "SAME_SHA_REPAIR",
            "FUTURE_GC_NAMESPACE_ISOLATION",
        ],
        "writer coverage",
    )
    require_equal(
        writer.get("contention_behavior"),
        "BUSY_FAIL_CLOSED",
        "writer contention behavior",
    )
    require_equal(
        writer.get("sqlite_expected_generation_guard_retained"),
        True,
        "SQLite defense in depth",
    )

    reader = policy.get("reader_coordination", {})
    require_equal(
        reader.get("required_for_modes"),
        ["OFF", "ON"],
        "reader modes",
    )
    require_equal(
        reader.get("positive_generation_only"),
        True,
        "positive-generation reader rule",
    )
    require_equal(reader.get("mode"), "SHARED", "reader lease mode")
    require_equal(
        reader.get("exclusive_gc_contention_behavior"),
        "BUSY_FAIL_CLOSED",
        "reader contention behavior",
    )
    require_equal(
        reader.get("generation_zero_creates_no_lease"),
        True,
        "generation-zero rule",
    )

    require_equal(
        policy.get("future_gc_isolation_preconditions"),
        [
            "HOLD_GLOBAL_B0_MUTATION_LEASE",
            "REREAD_DURABLE_CONVERSATION_ROOTS",
            "REBUILD_B2_LIVE_ROOT_UNION",
            "IDENTIFY_ALL_COMPLETE_GENERATIONS_REFERENCING_CANDIDATE",
            "ACQUIRE_RELEVANT_B2_EXCLUSIVE_GENERATION_EXCLUSIONS",
            "REREAD_DURABLE_ROOTS_AFTER_EXCLUSIVE_EXCLUSION",
            "REVALIDATE_EXACT_CANDIDATE_OBJECT_WITH_NOFOLLOW_DIRFD",
            "SAME_FILESYSTEM_ISOLATION_RENAME_ONLY_AFTER_ALL_GATES",
        ],
        "future GC isolation prerequisites",
    )

    lock_order = policy.get("lock_order", {})
    require_equal(
        lock_order.get("writer_or_gc"),
        [
            "B0_GLOBAL_MUTATION_EXCLUSIVE",
            "B2_RELEVANT_GENERATION_EXCLUSIVE_WHEN_GC",
            "FILESYSTEM_PREPARATION_OR_ISOLATION",
            "GUARDED_CONTROL_DB_TRANSACTION_IF_ANY",
        ],
        "writer/GC lock order",
    )
    require_equal(
        lock_order.get("reader"),
        [
            "B2_GENERATION_SHARED",
            "GENERATION_SNAPSHOT_INDEX_USE",
        ],
        "reader lock order",
    )
    require_equal(
        lock_order.get("forbidden"),
        [
            "B2_SHARED_THEN_B0_GLOBAL",
            "CONTROL_DB_WRITE_TRANSACTION_THEN_B0_GLOBAL",
        ],
        "forbidden lock order",
    )

    require_equal(
        policy.get("mixed_mode_qualification_required"),
        [
            "OFF_WRITER_VS_OFF_WRITER_B0_CONTENTION",
            "OFF_WRITER_VS_ON_WRITER_B0_CONTENTION",
            "OFF_READER_SHARED_B2_BLOCKS_GC_EXCLUSIVE",
            "GC_EXCLUSIVE_B2_BLOCKS_NEW_OFF_READER",
            "ON_READER_SHARED_B2_REGRESSION",
            "OPTIMIZATIONS_OFF_DOES_NOT_ENABLE_A1_C0_B1_C1_D1",
        ],
        "mixed-mode qualification matrix",
    )

    implementation = policy.get("implementation_state", {})
    require_equal(
        implementation,
        {
            "writer_b0_always_on": False,
            "reader_b2_always_on": False,
            "destructive_gc_authorized": False,
            "next_required_slice":
                "IMPLEMENT_AND_QUALIFY_MIXED_MODE_COORDINATION",
        },
        "implementation state",
    )

    # P0 is policy-only. The current runtime must still be the pre-exemption
    # ON-only implementation until a later implementation/qualification PR.
    download_start = require_marker(
        lifecycle,
        "        public async uint download_or_update (",
        "download/update method",
    )
    download_end = lifecycle.find(
        "\n        }\n\n    }\n}",
        download_start,
    )
    if download_end < 0:
        fail("download/update method boundary is unavailable")
    download = lifecycle[download_start:download_end]

    writer_gate = require_marker(
        download,
        "            if (optimized_operation) {",
        "current B0 ON-only gate",
    )
    writer_acquire = require_marker(
        download,
        "RepositoryNative.try_acquire_mutation_lease (",
        "current B0 acquisition",
    )
    if writer_gate >= writer_acquire:
        fail("current B0 acquisition is no longer visibly ON-gated")

    grounding_start = require_marker(
        lifecycle,
        "        private async ConversationGrounding\n"
        "        prepare_conversation_grounding_for_generation (",
        "grounding method",
    )
    grounding_end = lifecycle.find(
        "\n        public async ConversationGrounding\n"
        "        prepare_conversation_grounding (",
        grounding_start,
    )
    if grounding_end < 0:
        fail("grounding method boundary is unavailable")
    grounding = lifecycle[grounding_start:grounding_end]

    reader_gate = require_marker(
        grounding,
        "            if (optimized_operation) {",
        "current B2 ON-only gate",
    )
    reader_acquire = require_marker(
        grounding,
        "RepositoryGenerationLease.\n"
        "                            acquire_shared (",
        "current B2 shared acquisition",
    )
    if reader_gate >= reader_acquire:
        fail("current B2 acquisition is no longer visibly ON-gated")

    require_marker(
        optimization,
        "public bool enabled { get; private set; default = false; }",
        "OptimizationPolicy default OFF",
    )
    require_marker(
        generation_validator,
        '"default-OFF gate + SH lifetime + no-upgrade lock order"',
        "current generation-lease validator state",
    )

    # P0 must not smuggle destructive C1 activation into lifecycle.
    for marker in (
        "RepositoryGcCandidateDiscovery",
        "RepositoryGcCandidateSet",
        "gc_isolate",
        "isolate_snapshot",
        '".trash"',
    ):
        if marker in lifecycle:
            fail(f"policy slice introduced destructive/lifecycle C1 marker: {marker}")

    print(
        "C1 coordination policy validation passed: "
        "B0/B2 correctness exemption selected, runtime still ON-only, "
        "destructive GC unauthorized"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
