#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"C1-I8 trash discovery validation failed: {message}")


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
    wrapper = read("src/RepositoryGcTrashDiscovery.vala")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    application = read("src/Application.vala")
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
        "I5 canonical grammar reuse",
    )

    for marker in (
        "atm_repository_gc_select_canonical_trash_candidate",
        "KNOWN_REPOSITORIES",
        '"ewd"',
        '"cbd"',
        '"rmd"',
        "candidate_precedes (",
        "g_strcmp0 (",
        "validate_trash_root_entries (",
        "O_DIRECTORY",
        "O_NOFOLLOW",
        "O_CLOEXEC",
        "AT_SYMLINK_NOFOLLOW",
        "fstatat (",
        "openat (",
        "fdopendir (",
        "readdir (",
        "atm_repository_gc_trash_name_parse (",
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
            fail(
                "read-only scanner contains destructive/authority marker: "
                + forbidden
            )

    for marker in (
        "Selection is deterministic by repository ID, then canonical trash basename",
        "does not acquire B0/B2",
        "does not authorize",
        "never mutates trash or repository authority",
    ):
        require(scan_header, marker, "trash discovery contract")

    for marker in (
        'cname = "atm_repository_gc_select_canonical_trash_candidate"',
        'cheader_filename = "repository_gc_trash_scan.h"',
        "gc_select_canonical_trash_candidate (",
        'cname = "atm_repository_gc_purge_trash_entry"',
        'cheader_filename = "repository_gc_purge.h"',
        "gc_purge_trash_entry (",
    ):
        require(native, marker, "narrow Vala bridge")

    for marker in (
        "public class RepositoryGcTrashCandidate",
        "public class RepositoryGcTrashDiscovery",
        "select_one (",
        "gc_select_canonical_trash_candidate (",
        "sha40_is_valid (",
        "trash_name.has_prefix",
    ):
        require(wrapper, marker, "Vala discovery wrapper")

    for forbidden in (
        "gc_select_canonical_trash_candidate",
        "gc_purge_trash_entry",
        "RepositoryGcTrashDiscovery",
    ):
        if forbidden in lifecycle:
            fail(f"I8 must remain lifecycle-unwired: {forbidden}")
        if "gc_purge" in forbidden and forbidden in application:
            fail(f"I8 must remain Application-unwired: {forbidden}")

    for marker in (
        "'src/RepositoryGcTrashDiscovery.vala'",
        "'src/repository_gc_trash_scan.c'",
        "repository_gc_trash_scan_test = executable(",
        "'repository-gc-trash-discovery'",
    ):
        require(meson, marker, "Meson discovery wiring")

    for marker in (
        "/repository-gc-trash-discovery/missing-trash-empty",
        "/repository-gc-trash-discovery/repository-then-basename",
        "/repository-gc-trash-discovery/malformed-identity-fail-closed",
        "/repository-gc-trash-discovery/symlink-object-fail-closed",
        "/repository-gc-trash-discovery/unknown-repository-fail-closed",
        "/repository-gc-trash-discovery/symlink-trash-root-fail-closed",
        "Repository ID is the primary key",
    ):
        require(tests, marker, "trash discovery unit coverage")

    print(
        "C1-I8 trash discovery validation passed: shared canonical I4 grammar; "
        "catalog-only no-follow deterministic discovery; narrow I5 binding; "
        "no purge orchestrator or runtime caller"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
