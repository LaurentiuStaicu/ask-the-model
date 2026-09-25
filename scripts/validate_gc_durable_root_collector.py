#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"gc durable-root collector validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def main() -> int:
    collector = read("src/RepositoryGcDurableRoots.vala")
    control_native = read("src/ControlStateNative.vala")
    repository_native = read("src/RepositoryNative.vala")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    meson = read("meson.build")
    test_source = read("tests/repository_gc_durable_roots_test.vala")
    test_support = read("tests/repository_gc_durable_roots_test_support.c")

    required = (
        "RepositoryGcDurableRootCollector",
        "string state_root",
        "ConversationPersistenceStore conversation_store",
        "conversation_store.list_conversations ()",
        "conversation_store.load_snapshot (",
        "ControlStateNative.active_generation_id_readonly (",
        "list_complete_generation_ids_readonly (",
        "ControlStateNative.\n                    load_repository_values_at_generation_readonly (",
        "try_acquire_generation_lease_exclusive (",
        "release_generation_lease (",
        "RepositoryCatalog.all ()",
        "A durable conversation repository pin does not match its Control DB generation.",
        "A positive protected repository generation contains no catalog repository state.",
    )

    for marker in required:
        if marker not in collector:
            fail(f"collector contract lost: {marker}")

    forbidden = (
        "FileUtils.remove",
        "DirUtils.remove",
        "rename (",
        "renameat",
        "unlink",
        "RepositoryNative.quarantine",
        "isolate_snapshot",
        "purge",
        ".trash",
        "ControlStateNative.active_generation_id (",
        "load_repository_values_at_generation (",
        "Dir.open",
        "directory.read_name",
        "repository-generation-leases",
        ".lock",
    )

    for marker in forbidden:
        if marker in collector:
            fail(f"I1 must remain non-destructive: {marker}")

    if "ControlStateNative.active_generation_id_readonly (" not in collector:
        fail("collector lost strict read-only active-generation authority read")

    if "load_repository_values_at_generation_readonly (" not in collector:
        fail("collector lost strict read-only pinned-generation authority read")

    for marker in (
        'cname = "atm_control_state_active_generation_id_readonly"',
        'cname = "atm_control_state_list_complete_generation_ids_readonly"',
        'cname = "atm_control_state_load_repository_values_at_generation_readonly"',
    ):
        if marker not in control_native:
            fail(f"strict read-only Control DB binding lost: {marker}")

    for marker in (
        'cname = "atm_repository_generation_lease_try_acquire_exclusive"',
        'cname = "atm_repository_generation_lease_release"',
    ):
        if marker not in repository_native:
            fail(f"B2 live-root binding lost: {marker}")

    if "RepositoryGcDurableRootCollector" in lifecycle:
        fail("I1 collector must remain dormant in RepositoryLifecycleService")

    if "'src/RepositoryGcDurableRoots.vala'" not in meson:
        fail("application build lost dormant GC root collector")

    if "repository_gc_durable_roots_test = executable(" not in meson:
        fail("GC durable-root test target is missing")

    if "'repository-gc-durable-roots'" not in meson:
        fail("GC durable-root test registration is missing")

    for marker in (
        "/repository-gc-roots/empty-complete-generation-fail-closed",
        "test_empty_complete_generation_fails_closed",
        "positive protected repository generation contains no catalog repository state",
        "/repository-gc-roots/live-generation-lease-protected",
        "test_live_generation_lease_protects_historical_root",
        "try_acquire_generation_lease_shared",
        "!after_release.protects_generation (1)",
    ):
        if marker not in test_source:
            fail(f"empty COMPLETE fail-closed test lost: {marker}")

    for marker in (
        "atm_c1_test_publish_empty_complete_generation",
        "INSERT INTO repository_generations",
        "SET lifecycle='COMPLETE'",
        "SET active_repository_generation=2",
    ):
        if marker not in test_support:
            fail(f"empty COMPLETE test fixture drifted: {marker}")

    print(
        "gc durable-root collector validation passed: "
        "active + conversation + B2 live roots mapped read-only; lifecycle unwired"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
