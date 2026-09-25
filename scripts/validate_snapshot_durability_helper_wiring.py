#!/usr/bin/env python3

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
HELPER = ROOT / "tests" / "a1_snapshot_replay_helper.c"
BENCHMARK = ROOT / "tests" / "snapshot_durability_benchmark.c"
MODULE_C = ROOT / "src" / "snapshot_durability.c"
MODULE_H = ROOT / "src" / "snapshot_durability.h"

REPLAY_WORKFLOWS = [
    ROOT / ".github/workflows/dm-log-writes-atm-replay.yml",
    ROOT / ".github/workflows/dm-log-writes-candidate-replay.yml",
    ROOT / ".github/workflows/dm-log-writes-candidate-boundary-replay.yml",
    ROOT / ".github/workflows/dm-flakey-candidate-errors.yml",
    ROOT / ".github/workflows/dm-log-writes-same-sha-repair.yml",
]
BENCHMARK_WORKFLOW = ROOT / ".github/workflows/snapshot-durability-benchmark.yml"


def fail(message: str) -> None:
    raise SystemExit(
        f"snapshot durability helper wiring validation failed: {message}"
    )


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def main() -> int:
    helper = read(HELPER)
    module_c = read(MODULE_C)
    module_h = read(MODULE_H)
    benchmark = read(BENCHMARK)

    for required in (
        '#include "snapshot_durability.h"',
        "atm_snapshot_durability_sync_tree",
        "atm_snapshot_durability_sync_parent",
    ):
        if required not in helper:
            fail(f"replay helper lost shared durability use: {required}")

    for forbidden in (
        "static gboolean\nfsync_retry (",
        "static gboolean\nsync_tree_directory (",
    ):
        if forbidden in helper:
            fail("replay helper reintroduced a private S1 implementation")

    for required in (
        "AtmSnapshotDurabilityStats",
        "atm_snapshot_durability_sync_tree",
        "atm_snapshot_durability_sync_parent",
    ):
        if required not in module_h:
            fail(f"shared durability interface lost: {required}")

    for required in (
        "AT_SYMLINK_NOFOLLOW",
        "O_NOFOLLOW",
        "O_NONBLOCK",
        "O_DIRECTORY",
        "EINTR",
        "errno = 0",
        "fstat (file_fd, &opened_st)",
        "S_ISREG",
        "S_ISDIR",
        "S_ISLNK",
        "fsync (fd)",
        "stats->file_fsync_calls++",
        "stats->directory_fsync_calls++",
        "stats->parent_fsync_calls++",
    ):
        if required not in module_c:
            fail(f"shared S1 semantics drifted: {required}")

    for required in (
        '#include "snapshot_durability.h"',
        "atm_snapshot_durability_sync_tree",
    ):
        if required not in benchmark:
            fail(f"M1 benchmark lost shared durability use: {required}")

    for forbidden in (
        "static gboolean\nfsync_retry (",
        "static gboolean\nsync_tree_directory (",
    ):
        if forbidden in benchmark:
            fail("M1 benchmark reintroduced a private S1 implementation")

    benchmark_workflow = read(BENCHMARK_WORKFLOW)
    for required in (
        "'src/snapshot_durability.c'",
        "'src/snapshot_durability.h'",
        "src/snapshot_durability.c",
        "tests/snapshot_durability_benchmark.c",
    ):
        if required not in benchmark_workflow:
            fail(
                "snapshot-durability-benchmark.yml does not use shared "
                f"durability module: {required}"
            )

    for workflow_path in REPLAY_WORKFLOWS:
        workflow = read(workflow_path)

        for required in (
            "'src/snapshot_durability.c'",
            "'src/snapshot_durability.h'",
            "-c src/snapshot_durability.c",
            "/tmp/snapshot_durability.o",
            "tests/a1_snapshot_replay_helper.c",
        ):
            if required not in workflow:
                fail(
                    f"{workflow_path.name} does not use shared durability "
                    f"module: {required}"
                )

    print(
        "snapshot durability helper wiring validation passed: "
        "M1/M4/M5/M6/M8/M9 share the S1 native implementation"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
