#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"C1-I4 isolation validation failed: {message}")


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require_order(text: str, items: tuple[str, ...], context: str) -> None:
    position = -1
    for item in items:
        found = text.find(item, position + 1)
        if found < 0:
            fail(f"{context} lost ordered step: {item}")
        position = found


def main() -> int:
    module = read("src/repository_gc_isolation.c")
    header = read("src/repository_gc_isolation.h")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    meson = read("meson.build")
    tests = read("tests/repository_gc_isolation_test.c")
    fault_helper = read("tests/gc_isolation_fault_helper.c")
    fault_test = read("tests/gc_isolation_fault_test.c")

    for marker in (
        "atm_repository_gc_isolate_snapshot_to_trash",
        "O_DIRECTORY",
        "O_NOFOLLOW",
        "O_CLOEXEC",
        "AT_SYMLINK_NOFOLLOW",
        "openat (",
        "fstatat (",
        "renameat2 (",
        "RENAME_NOREPLACE",
        "EXDEV",
        '".trash"',
        "source snapshot changed during structural revalidation",
        "gc_isolate_pre_rename",
        "gc_isolate_post_rename",
        "gc_isolate_before_destination_fsync",
        "gc_isolate_after_destination_fsync",
        "gc_isolate_before_source_fsync",
        "gc_isolate_after_source_fsync",
    ):
        if marker not in module:
            fail(f"isolation primitive lost contract marker: {marker}")

    require_order(
        module,
        (
            "gc_isolate_pre_rename",
            "renameat2 (",
            "gc_isolate_post_rename",
            "gc_isolate_before_destination_fsync",
            "fsync_retry (\n            trash_repository_fd",
            "gc_isolate_after_destination_fsync",
            "gc_isolate_before_source_fsync",
            "fsync_retry (\n            snapshots_fd",
            "gc_isolate_after_source_fsync",
        ),
        "A1 destination->source isolation barrier",
    )

    for marker in (
        "caller is responsible for GC policy",
        "atomically renames the snapshot with no replacement",
        "destination->source order",
        "never decides reachability",
        "never purges trash",
    ):
        if marker not in header:
            fail(f"public isolation contract lost: {marker}")

    for marker in (
        "unlink",
        "unlinkat",
        "FileUtils.remove",
        "DirUtils.remove",
        "g_remove",
        "g_rmdir",
        "purge",
    ):
        if marker in module:
            fail(f"I4 isolation primitive must not purge/delete: {marker}")

    if "atm_repository_gc_isolate_snapshot_to_trash" in lifecycle:
        fail("I4 must remain dormant in RepositoryLifecycleService")

    for marker in (
        "'src/repository_gc_isolation.c'",
        "repository_gc_isolation_test = executable(",
        "'repository-gc-isolation'",
        "repository_gc_isolation_fault_helper = executable(",
        "repository_gc_isolation_fault_test = executable(",
        "'repository-gc-isolation-fault'",
        "ATM_GC_ISOLATION_FAULT_HELPER",
        "-DATM_TEST_FAULT_INJECTION",
    ):
        if marker not in meson:
            fail(f"I4 build/test wiring lost: {marker}")

    for marker in (
        "atm_test_fault_configure",
        "TRASH_ISOLATED",
        "SOURCE_PRESENT",
    ):
        if marker not in fault_helper:
            fail(f"I4 process-crash helper lost: {marker}")

    for marker in (
        "/repository-gc-isolation-fault/control",
        "/repository-gc-isolation-fault/pre-rename-crash",
        "/repository-gc-isolation-fault/post-rename-crash",
        "/repository-gc-isolation-fault/after-destination-fsync-crash",
        "/repository-gc-isolation-fault/before-source-fsync-crash",
        "gc_isolate_pre_rename",
        "gc_isolate_post_rename",
        "gc_isolate_after_destination_fsync",
        "gc_isolate_before_source_fsync",
        "gc_isolate_after_source_fsync",
        '"TRASH_ISOLATED"',
        '"SOURCE_PRESENT"',
    ):
        if marker not in fault_test:
            fail(f"I4 process-crash test coverage lost: {marker}")

    for marker in (
        "/repository-gc-isolation/move-preserve-payload",
        "/repository-gc-isolation/no-overwrite",
        "/repository-gc-isolation/reject-symlink-source",
        "/repository-gc-isolation/reject-symlink-trash-root",
        "/repository-gc-isolation/reject-invalid-identity",
        "stats.namespace_fsync_calls",
        "stats.isolation_fsync_calls",
    ):
        if marker not in tests:
            fail(f"I4 unit test coverage lost: {marker}")

    print(
        "C1-I4 isolation validation passed: "
        "no-follow RENAME_NOREPLACE; destination->source fsync; process-crash replay; no purge/runtime caller"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
