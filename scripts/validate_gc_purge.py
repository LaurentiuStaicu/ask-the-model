#!/usr/bin/env python3

from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"C1-I5 purge validation failed: {message}")


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
    module = read("src/repository_gc_purge.c")
    header = read("src/repository_gc_purge.h")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    meson = read("meson.build")
    tests = read("tests/repository_gc_purge_test.c")
    fault_helper = read("tests/gc_purge_fault_helper.c")
    fault_test = read("tests/gc_purge_fault_test.c")
    workflow = read(".github/workflows/c1-i5-purge-qualification.yml")

    for marker in (
        "atm_repository_gc_purge_trash_entry",
        "trash_name_is_valid",
        "canonical_decimal_segment",
        "G_MAXINT64",
        "G_MAXINT",
        "99",
        "O_DIRECTORY",
        "O_NOFOLLOW",
        "O_CLOEXEC",
        "AT_SYMLINK_NOFOLLOW",
        "openat (",
        "fstatat (",
        "fdopendir (",
        'openat (\n            directory_fd,\n            ".",',
        "readdir (",
        "unlinkat (",
        "AT_REMOVEDIR",
        "collect_directory_entry_names",
        "g_ptr_array_new_with_free_func",
        "validate_tree_directory",
        "purge_tree_directory",
        '"Repositories"',
        '".trash"',
        "gc_purge_before_first_unlink",
        "gc_purge_after_first_unlink",
        "gc_purge_before_root_rmdir",
        "gc_purge_after_root_rmdir",
        "gc_purge_after_parent_fsync",
    ):
        if marker not in module:
            fail(f"purge primitive lost contract marker: {marker}")

    require_order(
        module,
        (
            "validate_tree_directory (",
            "purge_tree_directory (",
            "gc_purge_before_root_rmdir",
            "unlinkat (\n            trash_repository_fd",
            "gc_purge_after_root_rmdir",
            "fsync_retry (\n            trash_repository_fd",
            "gc_purge_after_parent_fsync",
        ),
        "trash root purge",
    )

    for marker in (
        "already-isolated C1 trash object",
        "Repositories/.trash/<repository_id>",
        "never scans or deletes ordinary snapshot paths",
        "structurally validated before the first unlink",
        "never traversed",
    ):
        if marker not in header:
            fail(f"public purge contract lost: {marker}")

    for forbidden in (
        '"snapshots"',
        "renameat",
        "rename (",
        "quarantine",
        "ControlState",
        "Conversation",
        "RepositoryGcDurableRoot",
        "RepositoryGcCandidate",
    ):
        if marker := forbidden:
            if marker in module:
                fail(f"I5 purge primitive escaped trash-only responsibility: {marker}")

    if re.search(r"(?m)^\s*dup\s*\(", module):
        fail("I5 purge primitive must not use dup() traversal")

    if "atm_repository_gc_purge_trash_entry" in lifecycle:
        fail("I5 purge primitive must remain dormant in RepositoryLifecycleService")

    for marker in (
        "name: C1-I5 purge qualification",
        "Build purge unit test",
        "Run purge unit test",
        "Build purge interruption helpers",
        "Run purge process-interruption replay",
        "ATM_GC_PURGE_FAULT_HELPER: /tmp/repository-gc-purge-fault-helper",
        "/tmp/repository-gc-purge-fault-test",
        "python3 scripts/validate_gc_purge.py",
    ):
        if marker not in workflow:
            fail(f"I5 qualification workflow lost: {marker}")

    for marker in (
        "'src/repository_gc_purge.c'",
        "repository_gc_purge_test = executable(",
        "'repository-gc-purge'",
        "repository_gc_purge_fault_helper = executable(",
        "repository_gc_purge_fault_test = executable(",
        "'repository-gc-purge-fault'",
        "ATM_GC_PURGE_FAULT_HELPER",
    ):
        if marker not in meson:
            fail(f"I5 build/test wiring lost: {marker}")

    for marker in (
        "--purge ROOT CHECKPOINT",
        "--resume ROOT",
        "--verify ROOT",
        "TRASH_INTACT",
        "TRASH_RESIDUAL",
        "TRASH_EMPTY_ROOT",
        "FULLY_PURGED",
        "atm_repository_gc_isolate_snapshot_to_trash",
        "atm_repository_gc_purge_trash_entry",
    ):
        if marker not in fault_helper:
            fail(f"I5 interruption helper lost replay contract: {marker}")

    for marker in (
        "/repository-gc-purge-fault/control",
        "/repository-gc-purge-fault/before-first-unlink-crash",
        "/repository-gc-purge-fault/after-first-unlink-crash",
        "/repository-gc-purge-fault/before-root-rmdir-crash",
        "/repository-gc-purge-fault/after-root-rmdir-crash",
        "/repository-gc-purge-fault/after-parent-fsync-crash",
        '"TRASH_INTACT"',
        '"TRASH_RESIDUAL"',
        '"TRASH_EMPTY_ROOT"',
        '"FULLY_PURGED"',
        "resume_and_require_fully_purged",
    ):
        if marker not in fault_test:
            fail(f"I5 interruption replay coverage lost: {marker}")

    for marker in (
        "/repository-gc-purge/selected-tree-only",
        "/repository-gc-purge/preserve-other-trash",
        "/repository-gc-purge/reject-noncanonical-id",
        "A123456789abcdef0123456789abcdef01234567-1-1-0",
        "2147483648",
        "9223372036854775808",
        "-1-1-100",
        "/repository-gc-purge/reject-symlink-before-delete",
        "/repository-gc-purge/reject-symlink-trash-root",
        "/repository-gc-purge/reject-nondirectory-entry",
        "/repository-gc-purge/wide-directory-complete",
        "entry-%03u.txt",
        "stats.regular_files_removed",
        "stats.directories_removed",
        "stats.directory_fsync_calls",
    ):
        if marker not in tests:
            fail(f"I5 unit test coverage lost: {marker}")

    print(
        "C1-I5 purge validation passed: "
        "trash-only dirfd recursion; prevalidation; interruption replay/resume; no runtime caller"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
