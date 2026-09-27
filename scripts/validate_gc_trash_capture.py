#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"C1-I8a trash capture validation failed: {message}")


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, marker: str, context: str) -> None:
    if marker not in text:
        fail(f"{context} lost marker: {marker}")


def main() -> int:
    scan = read("src/repository_gc_trash_scan.c")
    scan_header = read("src/repository_gc_trash_scan.h")
    purge = read("src/repository_gc_purge.c")
    purge_header = read("src/repository_gc_purge.h")
    native = read("src/RepositoryNative.vala")
    wrapper = read("src/RepositoryGcTrashCapture.vala")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    tests = read("tests/repository_gc_trash_scan_test.c")
    meson = read("meson.build")

    for marker in (
        "atm_repository_gc_trash_name_parse",
        "out_isolation_time",
        "out_pid",
        "out_attempt",
    ):
        require(purge_header, marker, "shared canonical parser header")
        require(purge, marker, "shared canonical parser implementation")

    require(
        purge,
        "!atm_repository_gc_trash_name_parse (",
        "I5 exact grammar reuse",
    )

    for marker in (
        "atm_repository_gc_select_oldest_trash_candidate",
        "KNOWN_REPOSITORIES",
        '"ewd"',
        '"cbd"',
        '"rmd"',
        "O_DIRECTORY",
        "O_NOFOLLOW",
        "O_CLOEXEC",
        "AT_SYMLINK_NOFOLLOW",
        "fstatat (",
        "openat (",
        "fdopendir (",
        "readdir (",
        "atm_repository_gc_trash_name_parse (",
        "candidate_is_older (",
        "validate_trash_root_entries (",
        "GC trash root contains unexpected repository namespace",
        "noncanonical I4 identity",
    ):
        require(scan, marker, "read-only trash scanner")

    for forbidden in (
        "unlinkat (",
        "unlink (",
        "remove (",
        "renameat",
        "rename (",
        "rmdir (",
        "mkdir",
        "ControlState",
        "Conversation",
    ):
        if forbidden in scan:
            fail(f"read-only trash scanner contains destructive/authority marker: {forbidden}")

    for marker in (
        "read-only/advisory",
        "never authorizes deletion",
        "Unexpected repository names",
        "symlinks and special",
    ):
        require(scan_header, marker, "trash capture public contract")

    for marker in (
        'cname = "atm_repository_gc_select_oldest_trash_candidate"',
        'cheader_filename = "repository_gc_trash_scan.h"',
        "gc_select_oldest_trash_candidate (",
    ):
        require(native, marker, "Vala native bridge")

    for marker in (
        "public class RepositoryGcTrashCandidate",
        "public class RepositoryGcTrashCapture",
        "capture_oldest (",
        "RepositoryNative.",
        "gc_select_oldest_trash_candidate (",
        "if (!found)",
        "isolation_time <= 0",
    ):
        require(wrapper, marker, "Vala capture wrapper")

    if "gc_select_oldest_trash_candidate" in lifecycle:
        fail("I8a must remain lifecycle-unwired")
    if "gc_purge_trash_entry" in lifecycle:
        fail("I8a must not wire phase-2 purge")

    for marker in (
        "'src/RepositoryGcTrashCapture.vala'",
        "'src/repository_gc_trash_scan.c'",
        "repository_gc_trash_scan_test = executable(",
        "'repository-gc-trash-scan'",
    ):
        require(meson, marker, "Meson wiring")

    for marker in (
        "/repository-gc-trash-scan/missing-trash-empty",
        "/repository-gc-trash-scan/oldest-global-candidate",
        "/repository-gc-trash-scan/malformed-identity-fail-closed",
        "/repository-gc-trash-scan/symlink-object-fail-closed",
        "/repository-gc-trash-scan/unknown-repository-fail-closed",
        "/repository-gc-trash-scan/symlink-trash-root-fail-closed",
        "Timestamp ties are deterministic",
    ):
        require(tests, marker, "trash capture tests")

    print(
        "C1-I8a trash capture validation passed: shared canonical I4 grammar; "
        "catalog-only no-follow read-only scan; deterministic oldest capture; "
        "lifecycle and purge remain unwired"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
