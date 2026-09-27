#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"C1-I9 purge orchestrator validation failed: {message}")


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, marker: str, context: str) -> int:
    pos = text.find(marker)
    if pos < 0:
        fail(f"{context} lost marker: {marker}")
    return pos


def main() -> int:
    orchestrator = read("src/RepositoryGcPurgeOrchestrator.vala")
    tests = read("tests/repository_gc_purge_orchestrator_test.vala")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    application = read("src/Application.vala")
    meson = read("meson.build")

    ordered = [
        "try_acquire_mutation_lease (",
        "RepositoryGcTrashDiscovery.\n                        select_one (",
        "int64[] references =\n                    referencing_generations (",
        "try_acquire_generation_lease_exclusive (",
        "\"after-b2-exclusions\"",
        "RepositoryGcDurableRootCollector.\n                        collect_durable_only (",
        "durable_roots.protects_snapshot (",
        "\"before-i5-purge\"",
        "gc_purge_trash_entry (",
    ]
    previous = -1
    for marker in ordered:
        current = require(orchestrator, marker, "purge ordering")
        if current <= previous:
            fail(f"purge ordering drifted at marker: {marker}")
        previous = current

    for marker in (
        "RepositoryGcPurgeOutcome.NOT_ENABLED",
        "RepositoryGcPurgeOutcome.B0_CONTENDED",
        "RepositoryGcPurgeOutcome.NO_CANDIDATE",
        "RepositoryGcPurgeOutcome.B2_CONTENDED",
        "RepositoryGcPurgeOutcome.ROOTED_PRESERVED",
        "RepositoryGcPurgeOutcome.PURGED",
        "sort_generation_ids_ascending (",
        "release_generation_exclusions (",
        "list_complete_generation_ids_readonly (",
        "load_repository_values_at_generation_readonly (",
    ):
        require(orchestrator, marker, "purge race contract")

    if "RepositoryGcPurgeOrchestrator" in lifecycle:
        fail("dormant purge orchestrator is wired into RepositoryLifecycleService")
    if "RepositoryGcPurgeOrchestrator" in application:
        fail("dormant purge orchestrator is wired into Application")
    if "gc_purge_trash_entry (" in lifecycle:
        fail("I5 purge primitive is wired into RepositoryLifecycleService")
    if "gc_purge_trash_entry (" in application:
        fail("I5 purge primitive is wired into Application")

    for marker in (
        "'src/RepositoryGcPurgeOrchestrator.vala'",
        "'tests/repository_gc_purge_orchestrator_test.vala'",
        "'repository-gc-purge-orchestrator'",
    ):
        require(meson, marker, "Meson purge orchestrator wiring")

    for marker in (
        '"/repository-gc-i9/off-noop"',
        '"/repository-gc-i9/b0-contention-noop"',
        '"/repository-gc-i9/no-candidate-noop"',
        '"/repository-gc-i9/unreferenced-purged"',
        '"/repository-gc-i9/active-root-preserved"',
        '"/repository-gc-i9/shared-reader-blocks-exclusive"',
        '"/repository-gc-i9/late-durable-root-reread"',
        '"/repository-gc-i9/exclusive-blocks-new-reader"',
        '"/repository-gc-i9/partial-exclusions-released"',
    ):
        require(tests, marker, "purge orchestrator race coverage")

    print(
        "C1-I9 purge orchestrator validation passed: B0 -> canonical discovery -> "
        "COMPLETE refs -> B2 exclusions -> durable reread -> exact I5; "
        "runtime caller remains absent"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
