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
    live_probe = read("src/RepositoryGcLiveRootProbe.vala")
    control_c = read("src/control_state.c")
    control_h = read("src/control_state.h")
    generation_lease = read("src/repository_generation_lease.c")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    meson = read("meson.build")
    test_source = read("tests/repository_gc_durable_roots_test.vala")
    test_support = read("tests/repository_gc_durable_roots_test_support.c")

    required = (
        "RepositoryGcDurableRootCollector",
        "ConversationPersistenceStore conversation_store",
        "conversation_store.list_conversations ()",
        "conversation_store.load_snapshot (",
        "ControlStateNative.active_generation_id_readonly (",
        "ControlStateNative.\n                    load_repository_values_at_generation_readonly (",
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
        'cname = "atm_control_state_load_repository_values_at_generation_readonly"',
    ):
        if marker not in control_native:
            fail(f"strict read-only Control DB binding lost: {marker}")

    for marker in (
        "collect_with_live_roots (",
        "ControlStateNative.\n                    list_complete_generation_ids_readonly (",
        "RepositoryGcLiveRootProbe.\n                        generation_is_live (",
        "roots.protects_generation (",
        "add_generation_snapshots (",
    ):
        if marker not in collector:
            fail(f"I2 live-root collector contract lost: {marker}")

    for marker in (
        'cname = "atm_control_state_list_complete_generation_ids_readonly"',
        "out int64[] generation_ids",
    ):
        if marker not in control_native:
            fail(f"I2 COMPLETE-generation binding lost: {marker}")

    for marker in (
        "atm_control_state_list_complete_generation_ids_readonly",
        "WHERE lifecycle='COMPLETE'",
        "ORDER BY generation_id",
        "open_existing_control_state_readonly (",
    ):
        if marker not in control_c:
            fail(f"I2 strict COMPLETE-generation query lost: {marker}")

    if "atm_control_state_list_complete_generation_ids_readonly" not in control_h:
        fail("I2 COMPLETE-generation public API declaration lost")

    for marker in (
        "try_acquire_exclusive (",
        "out contended",
        "if (contended)",
        "RepositoryGcLiveRootLeaseNative.release (",
        "return true;",
        "return false;",
    ):
        if marker not in live_probe:
            fail(f"I2 live-generation probe contract lost: {marker}")

    if "ATM_COORDINATION_LEASE_MODE_EXCLUSIVE" not in generation_lease:
        fail("B2 exclusive generation-lease primitive was lost")

    for marker in (
        ".lock",
        "Dir.open",
        "FileUtils.remove",
        "DirUtils.remove",
        "rename (",
        "unlink",
        ".trash",
    ):
        if marker in live_probe:
            fail(f"I2 live probe must not infer liveness/destruct from paths: {marker}")

    for marker in (
        "FileUtils.remove",
        "DirUtils.remove",
        "rename (",
        "renameat",
        "unlink",
        "isolate_snapshot",
        "purge",
        ".trash",
    ):
        if marker in collector:
            fail(f"I2 collector must remain non-destructive: {marker}")

    if "RepositoryGcDurableRootCollector" in lifecycle:
        fail("C1 I1/I2 collectors must remain dormant in RepositoryLifecycleService")
    if "RepositoryGcLiveRootProbe" in lifecycle:
        fail("C1 I2 live-root probe must remain dormant in RepositoryLifecycleService")

    if "'src/RepositoryGcDurableRoots.vala'" not in meson:
        fail("application build lost dormant GC root collector")
    if "'src/RepositoryGcLiveRootProbe.vala'" not in meson:
        fail("application build lost dormant GC live-root probe")

    if "repository_gc_durable_roots_test = executable(" not in meson:
        fail("GC durable-root test target is missing")

    if "'repository-gc-durable-roots'" not in meson:
        fail("GC durable-root test registration is missing")

    for marker in (
        "/repository-gc-roots/empty-complete-generation-fail-closed",
        "test_empty_complete_generation_fails_closed",
        "positive protected repository generation contains no catalog repository state",
    ):
        if marker not in test_source:
            fail(f"empty COMPLETE fail-closed test lost: {marker}")

    for marker in (
        "/repository-gc-roots/live-historical-generation",
        "acquire_shared_generation_lease",
        "collect_with_live_roots (",
        "roots.protects_generation (1)",
        "!after_release.protects_generation (1)",
    ):
        if marker not in test_source:
            fail(f"I2 live-root test lost: {marker}")

    for marker in (
        "atm_c1_test_publish_empty_complete_generation",
        "INSERT INTO repository_generations",
        "SET lifecycle='COMPLETE'",
        "SET active_repository_generation=2",
        "atm_repository_generation_lease_try_acquire_shared",
        "atm_repository_generation_lease_release",
    ):
        if marker not in test_support:
            fail(f"empty COMPLETE test fixture drifted: {marker}")

    print(
        "gc durable-root collector validation passed: "
        "durable + B2-live roots mapped read-only; lifecycle/destruction unwired"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
