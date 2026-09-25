#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]

def fail(message):
    raise SystemExit(f"C1-I0 validation failed: {message}")

def read(path):
    return (ROOT / path).read_text(encoding="utf-8")

def require(text, needle, label):
    if needle not in text:
        fail(f"{label} missing: {needle}")

def forbid(text, needle, label):
    if needle in text:
        fail(f"{label} contains forbidden I0 behavior: {needle}")

def main():
    conversation_c = read("src/conversation_store.c")
    conversation_h = read("src/conversation_store.h")
    control_c = read("src/control_state.c")
    control_h = read("src/control_state.h")
    lifecycle = read("src/RepositoryLifecycleService.vala")
    storage = read("src/repository_storage.c")
    conversation_test = read("tests/conversation_store_test.c")
    control_test = read("tests/control_state_test.c")

    for needle in (
        "atm_conversation_store_list_repository_generation_ids_readonly",
        "SQLITE_OPEN_READONLY",
        "SQLITE_OPEN_NOFOLLOW",
        "PRAGMA query_only=ON;",
        "SELECT DISTINCT repository_generation_id ",
    ):
        require(conversation_c, needle, "conversation read-only roots")

    for needle in (
        "atm_control_state_active_generation_id_readonly",
        "atm_control_state_list_generation_snapshot_references_readonly",
        "atm_control_state_load_repository_values_at_generation_readonly",
        "open_existing_control_state_readonly",
        "SQLITE_OPEN_READONLY",
        "SQLITE_OPEN_NOFOLLOW",
        "PRAGMA query_only=ON;",
    ):
        require(control_c, needle, "Control DB read-only roots")

    for needle in (
        "atm_conversation_store_list_repository_generation_ids_readonly",
    ):
        require(conversation_h, needle, "conversation public API")

    for needle in (
        "AtmControlStateSnapshotReference",
        "atm_control_state_active_generation_id_readonly",
        "atm_control_state_list_generation_snapshot_references_readonly",
        "atm_control_state_load_repository_values_at_generation_readonly",
        "atm_control_state_snapshot_references_free",
    ):
        require(control_h, needle, "Control DB public API")

    for needle in (
        "/conversation-store/readonly-repository-generation-roots",
        "atm_conversation_store_list_repository_generation_ids_readonly",
        "atm-conversation-store/1",
    ):
        require(conversation_test, needle, "conversation tests")

    for needle in (
        "/control-state/readonly-generation-snapshot-roots",
        "atm_control_state_active_generation_id_readonly",
        "atm_control_state_list_generation_snapshot_references_readonly",
        "atm_control_state_load_repository_values_at_generation_readonly",
        "atm-control-state/1",
    ):
        require(control_test, needle, "Control DB tests")

    for needle in (
        "atm_repository_isolate_snapshot_to_trash",
        "atm_repository_purge_trash_entry",
        "repository_snapshot_gc",
        "run_snapshot_gc",
    ):
        forbid(storage, needle, "repository storage")
        forbid(lifecycle, needle, "repository lifecycle")

    for needle in (
        "atm_conversation_store_list_repository_generation_ids_readonly",
        "atm_control_state_active_generation_id_readonly",
        "atm_control_state_list_generation_snapshot_references_readonly",
        "atm_control_state_load_repository_values_at_generation_readonly",
    ):
        forbid(lifecycle, needle, "repository lifecycle")

    print("C1-I0 read-only boundary: PASS")
    return 0

if __name__ == "__main__":
    sys.exit(main())
