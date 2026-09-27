#!/usr/bin/env python3

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
EVIDENCE = ROOT / "benchmarks" / "c1-reclamation-v1" / "evidence.json"


def fail(message: str) -> None:
    raise SystemExit(f"C1-M1 reclamation evidence validation failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def require_marker(text: str, marker: str, context: str) -> None:
    if marker not in text:
        fail(f"{context} lost marker: {marker}")


def main() -> int:
    evidence = json.loads(EVIDENCE.read_text(encoding="utf-8"))

    require(evidence.get("schema_version") == 1, "schema version drifted")
    require(
        evidence.get("evidence_id") == "atm-c1-reclamation-space-accounting-v1",
        "evidence id drifted",
    )
    require(evidence.get("status") == "qualification-only", "status drifted")
    require(
        evidence.get("production_thresholds_selected") is False,
        "C1-M1 must not select production thresholds",
    )
    require(
        evidence.get("qualified_head")
        == "e4f8bd321917115d481b1a78911ac0cb4bb53602",
        "qualified head drifted",
    )
    require(evidence.get("actions_run_id") == 36299959441, "run id drifted")
    require(evidence.get("artifact_id") == 10924209240, "artifact id drifted")
    require(
        evidence.get("artifact_zip_sha256")
        == "9c6ce7c105ef281def326997e3e75070d84a4136d63b71473b4173331787c8be",
        "artifact digest drifted",
    )

    contract = evidence.get("measurement_contract", {})
    require(
        contract.get("trash_allocation_metric")
        == "capacity_measurement.allocated_tree_bytes",
        "trash allocation metric drifted",
    )
    require(
        contract.get("filesystem_free_metric")
        == "capacity_measurement.available_bytes",
        "filesystem free-space metric drifted",
    )
    require(
        contract.get("exact_identity_required") is True,
        "exact trash identity is no longer required",
    )
    require(
        contract.get("normal_snapshot_preservation_required") is True,
        "normal snapshot preservation is no longer required",
    )

    observations = evidence.get("observations")
    require(
        isinstance(observations, list) and len(observations) == 2,
        "expected exactly tmpfs and ext4 observations",
    )

    by_fs = {
        item.get("filesystem"): item
        for item in observations
    }
    require(set(by_fs) == {"tmpfs", "ext4"}, "filesystem set drifted")

    expected = {
        "tmpfs": {
            "allocated": 11534336,
            "available_before": 54525952,
            "available_after": 66060288,
            "available_delta": 11534336,
            "inodes_before": 4084,
            "inodes_after": 4088,
        },
        "ext4": {
            "allocated": 11542528,
            "available_before": 75464704,
            "available_after": 87007232,
            "available_delta": 11542528,
            "inodes_before": 24554,
            "inodes_after": 24558,
        },
    }

    for filesystem, values in expected.items():
        item = by_fs[filesystem]
        context = f"{filesystem} observation"
        require(item.get("fragment_size") == 4096, f"{context} fragment size drifted")
        require(
            item.get("trash_logical_regular_bytes_before") == 11534336,
            f"{context} logical bytes drifted",
        )
        require(
            item.get("trash_allocated_tree_bytes_before") == values["allocated"],
            f"{context} allocated bytes drifted",
        )
        require(item.get("trash_entries_before") == 3, f"{context} entries drifted")
        require(item.get("trash_regular_files_before") == 2, f"{context} file count drifted")
        require(item.get("trash_directories_before") == 1, f"{context} directory count drifted")
        require(
            item.get("available_bytes_before") == values["available_before"],
            f"{context} available-before drifted",
        )
        require(
            item.get("available_bytes_after") == values["available_after"],
            f"{context} available-after drifted",
        )
        require(
            item.get("available_bytes_delta") == values["available_delta"],
            f"{context} available-byte delta drifted",
        )
        require(
            item.get("available_bytes_delta")
            == item.get("trash_allocated_tree_bytes_before"),
            f"{context} controlled fixture did not observe full measured allocation reclaim",
        )
        require(item.get("inode_budget_known") is True, f"{context} inode budget unknown")
        require(
            item.get("available_inodes_before") == values["inodes_before"],
            f"{context} inode-before drifted",
        )
        require(
            item.get("available_inodes_after") == values["inodes_after"],
            f"{context} inode-after drifted",
        )
        require(item.get("available_inodes_delta") == 4, f"{context} inode delta drifted")
        require(
            item.get("purge_regular_files_removed") == 2,
            f"{context} purge file count drifted",
        )
        require(
            item.get("purge_directories_removed") == 2,
            f"{context} purge directory count drifted",
        )
        require(
            item.get("purge_directory_fsync_calls") == 3,
            f"{context} purge fsync count drifted",
        )
        require(item.get("trash_absent_after") is True, f"{context} trash remained")
        require(
            item.get("normal_snapshot_preserved") is True,
            f"{context} normal snapshot was not preserved",
        )

    rules = evidence.get("interpretation_rules", [])
    require(
        any("not a filesystem-independent accounting identity" in rule for rule in rules),
        "portable accounting caveat is missing",
    )
    require(
        any("must not become a runtime purge threshold" in rule for rule in rules),
        "runtime-threshold prohibition is missing",
    )
    require(
        any("No startup, background" in rule for rule in rules),
        "hidden-trigger prohibition is missing",
    )

    probe = (ROOT / "tests" / "c1_reclamation_probe.c").read_text(encoding="utf-8")
    for marker in (
        "atm_capacity_measure_tree (",
        "atm_capacity_measure_filesystem (",
        "atm_repository_gc_purge_trash_entry (",
        "available_delta <= 0",
        "normal_snapshot_preserved",
        "trash_absent_after",
    ):
        require_marker(probe, marker, "reclamation probe")

    workflow = (
        ROOT / ".github" / "workflows" / "c1-m1-reclamation-space-accounting.yml"
    ).read_text(encoding="utf-8")
    for marker in (
        "mount -t tmpfs",
        "mkfs.ext4 -F -m 0 -b 4096",
        ".available_bytes_delta > 0",
        "production_thresholds_selected: false",
        "Upload reclamation evidence",
    ):
        require_marker(workflow, marker, "M1 qualification workflow")

    archive_extract = (ROOT / "src" / "archive_extract.c").read_text(encoding="utf-8")
    require_marker(
        archive_extract,
        "archive_entry_hardlink (entry)",
        "archive hard-link inspection",
    )
    require_marker(
        archive_extract,
        "Repository archive contains a symbolic or hard link.",
        "archive link rejection",
    )

    print(
        "C1-M1 reclamation evidence validation passed: controlled tmpfs/ext4 "
        "I5 purge reclaimed the measured trash allocation, preserved the normal "
        "snapshot namespace, and selected no production threshold"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
