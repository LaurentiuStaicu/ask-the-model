#!/usr/bin/env python3

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
INGEST_C = ROOT / "src" / "repository_ingest.c"
INGEST_H = ROOT / "src" / "repository_ingest.h"
NATIVE_VALA = ROOT / "src" / "RepositoryNative.vala"
LIFECYCLE = ROOT / "src" / "RepositoryLifecycleService.vala"
MESON = ROOT / "meson.build"

CAPACITY_WORKFLOWS = [
    ROOT / ".github/workflows/capacity-measurement-real.yml",
    ROOT / ".github/workflows/capacity-historical-amplification.yml",
    ROOT / ".github/workflows/capacity-out-of-sample.yml",
    ROOT / ".github/workflows/capacity-structural-amplification.yml",
    ROOT / ".github/workflows/capacity-enospc-real.yml",
    ROOT / ".github/workflows/capacity-inode-real.yml",
]


def fail(message: str) -> None:
    raise SystemExit(
        f"durable ingest primitive wiring validation failed: {message}"
    )


def read(path: pathlib.Path) -> str:
    return path.read_text(encoding="utf-8")


def require_order(text: str, items: list[str], context: str) -> None:
    position = -1
    for item in items:
        found = text.find(item, position + 1)
        if found < 0:
            fail(f"{context} lost ordered step: {item}")
        position = found


def main() -> int:
    ingest_c = read(INGEST_C)
    ingest_h = read(INGEST_H)
    native_vala = read(NATIVE_VALA)
    lifecycle = read(LIFECYCLE)
    meson = read(MESON)

    for required in (
        '#include "snapshot_durability.h"',
        '#include "snapshot_namespace_durability.h"',
        '#include "snapshot_seal.h"',
        "repository_ingest_archive_internal",
        "atm_repository_ingest_archive_durable",
        "out_pre_barrier_seal",
        "Durable repository ingest requires an existing real data root.",
    ):
        if required not in ingest_c:
            fail(f"repository ingest lost durable primitive: {required}")

    if "atm_repository_ingest_archive_durable" not in ingest_h:
        fail("durable ingest public C prototype is missing")
    for required in (
        "Durable snapshot-authority ingest.",
        "data_root must already exist as a real",
        "fsyncs the destination parent",
        "fsyncs the staging source parent",
        "does not publish Control DB authority",
    ):
        if required not in ingest_h:
            fail(f"durable ingest public contract lost: {required}")

    require_order(
        ingest_c,
        [
            "atm_repository_validate_snapshot",
            "atm_snapshot_seal_compute",
            "atm_snapshot_durability_sync_tree",
            "atm_snapshot_namespace_prepare_final_parent",
            "atm_repository_promote_snapshot",
            "atm_snapshot_durability_sync_parent",
            "atm_snapshot_durability_sync_parent",
        ],
        "durable ingest",
    )

    baseline_wrapper = ingest_c.find(
        "atm_repository_ingest_archive_cancellable ("
    )
    durable_wrapper = ingest_c.find(
        "atm_repository_ingest_archive_durable ("
    )
    if baseline_wrapper < 0 or durable_wrapper < 0:
        fail("baseline/durable wrapper pair is incomplete")

    baseline_slice = ingest_c[
        baseline_wrapper:
        durable_wrapper
    ]
    if "FALSE,\n        NULL," not in baseline_slice:
        fail("baseline ingest no longer selects the non-durable internal path")

    durable_slice = ingest_c[durable_wrapper:]
    if "TRUE,\n        out_pre_barrier_seal," not in durable_slice:
        fail("durable ingest no longer selects the durable internal path")

    for required in (
        'cname = "atm_repository_ingest_archive_durable"',
        "public static extern bool ingest_archive_durable (",
        "out string pre_barrier_seal",
    ):
        if required not in native_vala:
            fail(f"RepositoryNative durable-ingest bridge lost: {required}")

    for required in (
        "bool durable_ingest,",
        "if (durable_ingest) {",
        "RepositoryNative.ingest_archive_durable (",
        "} else if (!RepositoryNative.ingest_archive (",
        "durable_pre_barrier_seal !=",
        "Durable repository pre-barrier seal does not match the promoted snapshot.",
        "reject_preexisting_final_target (",
        "Optimizations ON refuses an unqualified pre-existing final repository snapshot target.",
    ):
        if required not in lifecycle:
            fail(f"ON-only lifecycle durable-ingest wiring lost: {required}")

    require_order(
        lifecycle,
        [
            "RepositoryNative.ingest_archive_durable (",
            "RepositoryNative.compute_snapshot_seal (",
            "durable_pre_barrier_seal !=",
            "RepositoryNative.ensure_index (",
            "RepositoryNative.compute_snapshot_seal (",
            "state_store.set_current (",
        ],
        "ON durable lifecycle seal chain",
    )

    if (
        "null,\n                        expected_seal,\n                        false,\n"
        "                        optimized_operation"
    ) not in lifecycle:
        fail("grounding path must never select durable ingest")

    if (
        "archive_path,\n                                null,\n"
        "                                optimized_operation,\n"
        "                                optimized_operation"
    ) not in lifecycle:
        fail("download/update lost operation-snapshot durable routing")

    if "RepositoryNative.ingest_archive (" not in lifecycle:
        fail("Optimizations OFF baseline ingest path was lost")

    if "runtime_integration_selected" not in read(
        ROOT / "qualification" / "durability-policy-v1.json"
    ):
        fail("durability policy runtime gate marker is missing")

    for required in (
        "'src/snapshot_durability.c'",
        "'src/snapshot_namespace_durability.c'",
        "'src/snapshot_seal.c'",
        "'src/repository_ingest.c'",
    ):
        if required not in meson:
            fail(f"Meson lost durable ingest link dependency: {required}")

    for workflow_path in CAPACITY_WORKFLOWS:
        workflow = read(workflow_path)
        for required in (
            "'src/snapshot_seal.c'",
            "'src/snapshot_seal.h'",
            "'src/snapshot_durability.c'",
            "'src/snapshot_durability.h'",
            "'src/snapshot_namespace_durability.c'",
            "'src/snapshot_namespace_durability.h'",
            "src/snapshot_seal.c \\",
            "src/snapshot_durability.c \\",
            "src/snapshot_namespace_durability.c \\",
            "src/repository_ingest.c \\",
        ):
            if required not in workflow:
                fail(
                    f"{workflow_path.name} lost durable ingest dependency: "
                    f"{required}"
                )

    print(
        "durable ingest primitive wiring validation passed: "
        "ON-only lifecycle durable ingest wired; OFF baseline preserved; seal chain locked"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
