#!/usr/bin/env python3

from pathlib import Path
import json
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"C1-I10 runtime integration validation failed: {message}")


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, marker: str, context: str) -> int:
    pos = text.find(marker)
    if pos < 0:
        fail(f"{context} lost marker: {marker}")
    return pos


def main() -> int:
    lifecycle = read("src/RepositoryLifecycleService.vala")
    application = read("src/Application.vala")
    tests = read("tests/repository_lifecycle_service_test.vala")
    meson = read("meson.build")
    policy = json.loads(read("qualification/c1-gc-policy-v1.json"))

    if policy.get("status") not in {
        "selected-runtime-isolation-and-purge-wired",
        "qualified-runtime-isolation-and-purge-reclamation-evidence-complete",
        "qualified-c1-complete",
    }:
        fail("runtime-wired policy status is not selected or monotonically qualified")
    if policy.get("scope", {}).get("runtime_purge_authorized") is not True:
        fail("runtime purge is not machine-authorized")
    implementation = policy.get("implementation_state", {})
    for key in (
        "runtime_purge_integration_implemented",
        "runtime_purge_integration_qualification_complete",
        "runtime_purge_steady_state_policy_selected",
        "purge_runtime_authorized",
    ):
        if implementation.get(key) is not True:
            fail(f"implementation state {key} is not true")

    integration_policy = policy.get("runtime_purge_integration_policy", {})
    if integration_policy.get("steady_state_no_growth_selected") is not True:
        fail("P5 steady-state no-growth policy is not selected")
    if integration_policy.get(
        "only_purge_progress_or_empty_trash_allows_isolation"
    ) is not True:
        fail("P5 isolation gate is not selected")

    isolation_start = require(
        lifecycle,
        "run_post_mutation_isolation (",
        "I7 isolation seam",
    )
    maintenance_start = require(
        lifecycle,
        "run_post_mutation_maintenance (",
        "I10 maintenance seam",
    )
    if isolation_start >= maintenance_start:
        fail("I7 isolation seam must precede I10 maintenance seam")

    isolation_slice = lifecycle[isolation_start:maintenance_start]
    require(
        isolation_slice,
        "RepositoryGcIsolationOrchestrator.\n                    isolate_one (",
        "I7 isolation call",
    )
    for forbidden in (
        "RepositoryGcPurgeOrchestrator",
        "gc_purge_trash_entry",
        "purge_one (",
    ):
        if forbidden in isolation_slice:
            fail(f"I7 purge-free sub-seam contains: {forbidden}")

    maintenance_end = lifecycle.find(
        "\n\n        private async uint download_or_update_for_operation",
        maintenance_start,
    )
    if maintenance_end < 0:
        fail("I10 maintenance boundary is unavailable")
    maintenance = lifecycle[maintenance_start:maintenance_end]

    ordered = [
        "RepositoryGcPurgeOrchestrator.\n                        purge_one (",
        "switch (purge.outcome)",
        "case RepositoryGcPurgeOutcome.NO_CANDIDATE:",
        "case RepositoryGcPurgeOutcome.PURGED:",
        "case RepositoryGcPurgeOutcome.B0_CONTENDED:",
        "case RepositoryGcPurgeOutcome.B2_CONTENDED:",
        "case RepositoryGcPurgeOutcome.ROOTED_PRESERVED:",
        "run_post_mutation_isolation (",
    ]
    previous = -1
    for marker in ordered:
        current = require(maintenance, marker, "I10 maintenance ordering")
        if current <= previous:
            fail(f"I10 maintenance ordering drifted at: {marker}")
        previous = current

    for marker in (
        "\"purge phase: \"",
        "\"isolation phase: \"",
        "bool allow_isolation = false;",
        "allow_isolation = true;",
        "new RepositoryPostMutationMaintenanceResult (",
    ):
        require(maintenance, marker, "I10 maintenance diagnostics")

    if "optimization_mode_snapshot (" in maintenance:
        fail("I10 rereads live Optimizations state")

    wrapper = require(
        application,
        "private void run_post_mutation_maintenance_best_effort (",
        "Application maintenance wrapper",
    )
    action = require(
        application,
        "private async void download_or_update_selected_repositories ()",
        "repository action",
    )
    if wrapper >= action:
        fail("Application maintenance wrapper order is invalid")
    app_slice = application[wrapper:action]

    for marker in (
        "run_post_mutation_maintenance (",
        "repository purge maintenance outcome=",
        "repository isolation maintenance skipped by purge outcome policy",
        "repository maintenance failed after committed repository action",
    ):
        require(app_slice, marker, "Application maintenance wrapper")
    for forbidden in (
        "RepositoryGcPurgeOrchestrator",
        "RepositoryGcIsolationOrchestrator",
        "gc_purge_trash_entry",
        "optimization_policy.snapshot_enabled",
        "optimization_mode_snapshot (",
    ):
        if forbidden in app_slice:
            fail(f"Application wrapper bypasses lifecycle: {forbidden}")

    for marker in (
        "contended_maintenance =",
        "RepositoryGcPurgeOutcome.B0_CONTENDED",
        "!contended_maintenance.\n                    isolation_allowed_after_purge",
        "!contended_maintenance.isolation_attempted",
        "no_candidate_maintenance =",
        "RepositoryGcPurgeOutcome.NO_CANDIDATE",
        "malformed_maintenance =",
        "\"purge phase:\"",
        "rooted_maintenance =",
        "RepositoryGcPurgeOutcome.ROOTED_PRESERVED",
        "b2_maintenance =",
        "RepositoryGcPurgeOutcome.B2_CONTENDED",
        "!b2_maintenance.\n                    isolation_allowed_after_purge",
        "!b2_maintenance.isolation_attempted",
        "purged_maintenance =",
        "RepositoryGcPurgeOutcome.PURGED",
        "RepositoryGcIsolationOutcome.NO_CANDIDATE",
    ):
        require(tests, marker, "I10 lifecycle test coverage")

    for marker in (
        "'src/RepositoryGcTrashDiscovery.vala'",
        "'src/RepositoryGcPurgeOrchestrator.vala'",
        "'src/repository_gc_trash_scan.c'",
        "'src/repository_gc_purge.c'",
    ):
        if meson.count(marker) < 3:
            fail(f"lifecycle build coverage is missing dependency: {marker}")

    print(
        "C1-I10 runtime integration validation passed: P5 no-growth gate, "
        "purge-free I7 sub-seam, lifecycle ownership, Application indirection, "
        "and B0/B2 contention no-growth coverage are present"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
