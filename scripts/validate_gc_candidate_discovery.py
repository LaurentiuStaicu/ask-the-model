#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    print(f"gc candidate discovery validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def main() -> int:
    candidate = read("src/RepositoryGcCandidates.vala")
    native_scan = read("src/repository_gc_candidate_scan.c")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    meson = read("meson.build")
    tests = read("tests/repository_gc_durable_roots_test.vala")
    support = read("tests/repository_gc_durable_roots_test_support.c")

    required = (
        "RepositoryGcCandidateDiscovery",
        "RepositoryCatalog.all ()",
        "RepositoryGcCandidateNative.scan (",
        "protected_roots.protects_snapshot (",
        "RepositoryGcCandidateSet",
        "quarantine_entry_count",
    )

    for marker in required:
        if marker not in candidate:
            fail(f"candidate-discovery contract lost: {marker}")

    for marker in (
        "O_DIRECTORY",
        "O_NOFOLLOW",
        "O_CLOEXEC",
        "openat (",
        "AT_SYMLINK_NOFOLLOW",
        "fdopendir (",
        "readdir (",
        "sha40_lower_is_valid",
        '"quarantine-entry"',
        '"unexpected-basename"',
        '"not-real-directory"',
    ):
        if marker not in native_scan:
            fail(f"no-follow native scanner contract lost: {marker}")

    forbidden = (
        "FileUtils.remove",
        "DirUtils.remove",
        "rename (",
        "renameat",
        "unlink",
        "quarantine_snapshot",
        "isolate_snapshot",
        "purge",
        '".trash"',
        "try_acquire_mutation_lease",
        "try_acquire_generation_lease",
    )

    for marker in forbidden:
        if marker in candidate:
            fail(f"I3 Vala layer must remain read-only/advisory: {marker}")
        if marker in native_scan:
            fail(f"I3 native scanner must remain read-only/advisory: {marker}")

    for marker in (
        "GLib.Dir.open (",
        "directory.read_name ()",
        "GLib.FileUtils.test (",
    ):
        if marker in candidate:
            fail(f"I3 Vala layer must not replace descriptor-based scanning: {marker}")

    if "RepositoryGcCandidateDiscovery" in lifecycle:
        fail("I3 candidate discovery must remain dormant in RepositoryLifecycleService")

    for marker in (
        "'src/RepositoryGcCandidates.vala'",
        "'src/repository_gc_candidate_scan.c'",
        "repository_gc_durable_roots_test = executable(",
    ):
        if marker not in meson:
            fail(f"I3 build/test wiring lost: {marker}")

    for marker in (
        "/repository-gc-candidates/protected-and-quarantine-filtered",
        "/repository-gc-candidates/malformed-entry-fail-closed",
        "/repository-gc-candidates/symlink-entry-fail-closed",
        "/repository-gc-candidates/regular-file-entry-fail-closed",
        "/repository-gc-candidates/symlink-root-fail-closed",
        "/repository-gc-candidates/intermediate-symlink-fail-closed",
        "protected_roots.add_snapshot",
        "candidates.quarantine_entry_count () == 1",
    ):
        if marker not in tests:
            fail(f"I3 candidate test coverage lost: {marker}")

    for marker in (
        "atm_c1_test_make_symlink",
        "symlink (",
    ):
        if marker not in support:
            fail(f"I3 symlink fixture lost: {marker}")

    print(
        "gc candidate discovery validation passed: "
        "catalog-only descriptor/no-follow scan; protected/quarantine excluded; destructive behavior absent"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
