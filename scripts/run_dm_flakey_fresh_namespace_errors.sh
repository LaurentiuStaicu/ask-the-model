#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"
HELPER="${2:?A1 fresh namespace helper required}"
PARENT_FSYNC_PROBE="${3:?parent fsync EIO probe JSON required}"
SOURCE_FSYNC_PROBE="${4:?source fsync EIO probe JSON required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m11-XXXXXX)"
RESULTS_DIR="$WORK_ROOT/results"
mkdir -p "$RESULTS_DIR" "$(dirname "$OUTPUT")"

cleanup_root() {
    local mapper_name="$1"
    local live_mount="$2"
    local verify_mount="$3"
    local loop_dev="$4"

    set +e
    sudo umount "$verify_mount" >/dev/null 2>&1 || true
    sudo umount "$live_mount" >/dev/null 2>&1 || true
    sudo dmsetup remove --retry "$mapper_name" >/dev/null 2>&1 || true
    if [[ -n "$loop_dev" ]]; then
        sudo losetup -d "$loop_dev" >/dev/null 2>&1 || true
    fi
    set -e
}

ACTIVE_MAPPER=""
ACTIVE_LIVE_MOUNT=""
ACTIVE_VERIFY_MOUNT=""
ACTIVE_LOOP=""

cleanup() {
    if [[ -n "$ACTIVE_MAPPER" || -n "$ACTIVE_LOOP" ]]; then
        cleanup_root             "$ACTIVE_MAPPER"             "$ACTIVE_LIVE_MOUNT"             "$ACTIVE_VERIFY_MOUNT"             "$ACTIVE_LOOP"
    fi
    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT

test -x "$HELPER"
sudo modprobe dm-flakey >/dev/null 2>&1 || true
sudo dmsetup targets | awk '{print $1}' | grep -Fxq "flakey"

run_scenario() {
    local checkpoint="$1"
    local boundary="$2"
    local expected_error_context="$3"

    local work="$WORK_ROOT/$boundary"
    local data_image="$work/data.img"
    local live_mount="$work/live"
    local verify_mount="$work/verify"
    local mapper_name="atm-a1-m11-${GITHUB_RUN_ID:-$$}-${boundary//_/-}"
    local mapper_path="/dev/mapper/$mapper_name"
    local loop_dev=""
    local live_mounted=0
    local verify_mounted=0

    ACTIVE_MAPPER="$mapper_name"
    ACTIVE_LIVE_MOUNT="$live_mount"
    ACTIVE_VERIFY_MOUNT="$verify_mount"
    ACTIVE_LOOP=""

    mkdir -p "$work" "$live_mount" "$verify_mount"
    truncate -s 128M "$data_image"

    loop_dev="$(sudo losetup --find --show "$data_image")"
    ACTIVE_LOOP="$loop_dev"

    local sectors
    sectors="$(sudo blockdev --getsz "$loop_dev")"

    sudo dmsetup create "$mapper_name"         --table "0 $sectors linear $loop_dev 0"

    sudo mkfs.ext4 -F -m 0 "$mapper_path" >/dev/null
    sudo mount "$mapper_path" "$live_mount"
    live_mounted=1
    sudo chown "$(id -u):$(id -g)" "$live_mount"

    "$HELPER" --initialize-empty "$live_mount"         >"$work/empty.json"
    python3 -m json.tool "$work/empty.json" >/dev/null

    # Establish one durable empty-authority/data-root baseline. No
    # Repositories hierarchy exists at this point.
    sync -f "$live_mount"

    local checkpoint_pipe="$work/checkpoint.pipe"
    local control_pipe="$work/control.pipe"
    mkfifo "$checkpoint_pipe" "$control_pipe"

    exec {CHECKPOINT_FD}<>"$checkpoint_pipe"
    exec {CONTROL_FD}<>"$control_pipe"

    set +e
    "$HELPER"         --promote-fresh-namespace-candidate-boundary         "$live_mount"         S1_DEST_SOURCE         "$checkpoint"         3>"$checkpoint_pipe"         4<"$control_pipe"         >"$work/helper.stdout"         2>"$work/helper.stderr" &
    local helper_pid=$!
    set -e

    local observed_checkpoint=""
    IFS= read -r observed_checkpoint <&$CHECKPOINT_FD
    test "$observed_checkpoint" = "$checkpoint"

    # Preserve M8's fault methodology: switching to error_writes must not
    # flush or synchronize the filesystem before the barrier under test.
    sudo dmsetup suspend --noflush --nolockfs "$mapper_name"
    sudo dmsetup reload "$mapper_name"         --table "0 $sectors flakey $loop_dev 0 1 600 1 error_writes"
    sudo dmsetup resume "$mapper_name"
    sudo dmsetup table "$mapper_name" | grep -Fq " flakey "
    sleep 2

    printf 'C' >&$CONTROL_FD

    set +e
    wait "$helper_pid"
    local helper_rc=$?
    set -e

    exec {CHECKPOINT_FD}>&-
    exec {CONTROL_FD}>&-

    if [[ "$helper_rc" -eq 0 ]]; then
        printf '%s\n'             "M11 expected S1_DEST_SOURCE failure under error_writes at $boundary"             >&2
        cat "$work/helper.stdout" >&2 || true
        exit 1
    fi

    if ! grep -Eiq         'input/output error|i/o error|disk i/o error'         "$work/helper.stderr"; then
        printf '%s\n'             "M11 candidate failed without an explicit I/O error at $boundary"             >&2
        cat "$work/helper.stderr" >&2 || true
        exit 1
    fi

    if ! grep -Fqi "$expected_error_context" "$work/helper.stderr"; then
        printf '%s\n' \
            "M11b error occurred outside the exact fsync context at $boundary" \
            >&2
        cat "$work/helper.stderr" >&2 || true
        exit 1
    fi

    sudo dmsetup suspend --noflush --nolockfs "$mapper_name"
    sudo dmsetup reload "$mapper_name"         --table "0 $sectors linear $loop_dev 0"
    sudo dmsetup resume "$mapper_name"
    sudo dmsetup table "$mapper_name" | grep -Fq " linear "

    set +e
    sudo umount "$live_mount"
    local umount_rc=$?
    set -e
    if [[ "$umount_rc" -ne 0 ]]; then
        cleanup_root             "$mapper_name"             "$live_mount"             "$verify_mount"             "$loop_dev"
        exit "$umount_rc"
    fi
    live_mounted=0

    sudo dmsetup remove --retry "$mapper_name"

    set +e
    sudo e2fsck -fy "$loop_dev" >"$work/e2fsck.txt" 2>&1
    local e2fsck_rc=$?
    set -e
    if [[ "$e2fsck_rc" -gt 2 ]]; then
        cat "$work/e2fsck.txt" >&2
        cleanup_root             "$mapper_name"             "$live_mount"             "$verify_mount"             "$loop_dev"
        exit "$e2fsck_rc"
    fi

    sudo mount "$loop_dev" "$verify_mount"
    verify_mounted=1

    "$HELPER" --verify-fresh "$verify_mount"         >"$work/verify.json"
    python3 -m json.tool "$work/verify.json" >/dev/null

    local result_path="$RESULTS_DIR/$boundary.json"

    python3 -         "$result_path"         "$boundary"         "$checkpoint"         "$helper_rc"         "$e2fsck_rc"         "$work/helper.stderr"         "$work/verify.json"         "$expected_error_context" <<'PY'
import json
import pathlib
import sys

(
    output,
    boundary,
    checkpoint,
    helper_rc,
    e2fsck_rc,
    stderr_path,
    verify_path,
    expected_error_context,
) = sys.argv[1:]

with open(verify_path, encoding="utf-8") as handle:
    verify = json.load(handle)

stderr = pathlib.Path(stderr_path).read_text(
    encoding="utf-8",
    errors="replace",
).strip()

record = {
    "strategy": "S1_DEST_SOURCE",
    "boundary": boundary,
    "checkpoint": checkpoint,
    "helper_exit_code": int(helper_rc),
    "candidate_failed": int(helper_rc) != 0,
    "io_error_observed": any(
        token in stderr.lower()
        for token in (
            "input/output error",
            "i/o error",
            "disk i/o error",
        )
    ),
    "expected_error_context": expected_error_context,
    "error_context_match": expected_error_context.lower() in stderr.lower(),
    "e2fsck_exit_code": int(e2fsck_rc),
    "classification": verify["classification"],
    "active_generation_id": verify["active_generation_id"],
    "active_repository_sha": verify.get("active_repository_sha"),
    "qualified": verify["qualified"],
    "reason_code": verify["reason_code"],
    "final_snapshot_exists": verify["final_snapshot_exists"],
    "staging_exists": verify["staging_exists"],
    "namespace_clean": verify["namespace_clean"],
    "expected_classification": "EMPTY_AUTHORITY_VALID",
}

record["authority_fail_closed"] = (
    record["candidate_failed"]
    and record["io_error_observed"]
    and record["error_context_match"]
    and record["classification"] == "EMPTY_AUTHORITY_VALID"
    and record["active_generation_id"] == 0
    and record["active_repository_sha"] is None
    and record["qualified"] is True
)

pathlib.Path(output).write_text(
    json.dumps(record, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY

    sudo umount "$verify_mount"
    verify_mounted=0
    sudo losetup -d "$loop_dev"
    loop_dev=""

    ACTIVE_MAPPER=""
    ACTIVE_LIVE_MOUNT=""
    ACTIVE_VERIFY_MOUNT=""
    ACTIVE_LOOP=""

    python3 -m json.tool "$result_path" >/dev/null
}

run_scenario \
    namespace_repository_before_child_fsync \
    destination_hierarchy_child_fsync \
    "Could not fsync snapshot namespace child directory"

run_scenario \
    fresh_post_rename_pre_destination_parent_fsync \
    destination_parent_fsync \
    "Could not fsync promoted snapshot parent"


python3 - "$OUTPUT" "$RESULTS_DIR" "$PARENT_FSYNC_PROBE" "$SOURCE_FSYNC_PROBE" <<'PY'
import json
import pathlib
import platform
import sys

output, results_dir, parent_probe_path, source_probe_path = sys.argv[1:]
results = []

with open(parent_probe_path, encoding="utf-8") as handle:
    parent_probe = json.load(handle)

with open(source_probe_path, encoding="utf-8") as handle:
    source_probe = json.load(handle)

for path in sorted(pathlib.Path(results_dir).glob("*.json")):
    with path.open(encoding="utf-8") as handle:
        results.append(json.load(handle))

boundaries = {
    "destination_hierarchy_child_fsync",
    "destination_parent_fsync",
}

if len(results) != 2:
    raise SystemExit("A1-M11b must contain exactly two dm-flakey EIO scenarios")
if {item["boundary"] for item in results} != boundaries:
    raise SystemExit("A1-M11b EIO boundary set is incomplete")
if {item["strategy"] for item in results} != {"S1_DEST_SOURCE"}:
    raise SystemExit("A1-M11b must test only S1_DEST_SOURCE")

artifact = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m11b-exact-namespace-fsync-eio-v2",
    "status": "measurement-only",
    "production_namespace_selected": False,
    "kernel": {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
    },
    "protocol": {
        "device_mapper_target": "flakey",
        "feature": "error_writes",
        "flakey_up_interval_seconds": 1,
        "flakey_down_interval_seconds": 600,
        "empty_authority_synced_before_each_scenario": True,
        "fault_teardown_suspend_noflush_nolockfs": True,
        "fresh_process_verifier_after_recovery": True,
        "candidate": "S1_DEST_SOURCE",
        "dm_flakey_boundaries": [
            "destination_hierarchy_child_fsync",
            "destination_parent_fsync",
        ],
        "parent_fsync_probe": "synthetic-fsync-return-eio-on-exact-call",
        "source_fsync_probe": "full-flow-synthetic-fsync-return-eio-on-call-6",
        "exact_fsync_context_required": True,
        "no_syncfs_fallback": True,
    },
    "results": results,
    "namespace_parent_fsync_probe": parent_probe,
    "source_parent_fsync_probe": source_probe,
    "all_authority_fail_closed": all(
        item["authority_fail_closed"]
        for item in results
    ),
    "all_e2fsck_recoverable": all(
        item["e2fsck_exit_code"] <= 2
        for item in results
    ),
    "all_exact_error_contexts": all(
        item["error_context_match"]
        for item in results
    ),
    "namespace_parent_fsync_probe_qualified": (
        parent_probe.get("qualified") is True
        and parent_probe.get("error_is_eio") is True
        and parent_probe.get("error_context_match") is True
        and parent_probe.get("fsync_calls") == 4
    ),
    "source_parent_fsync_probe_qualified": (
        source_probe.get("qualified") is True
        and source_probe.get("error_context_match") is True
        and source_probe.get("failed_fsync_call") == 6
        and source_probe.get("classification") == "EMPTY_AUTHORITY_VALID"
        and source_probe.get("active_generation_id") == 0
        and source_probe.get("active_repository_sha") is None
    ),
    "limitations": [
        (
            "M11b qualifies block-layer EIO at the exact hierarchy-child "
            "and destination-parent fsync contexts on ext4. The containing-"
            "parent namespace fsync and the post-rename source-parent fsync "
            "use deterministic exact-syscall probes because a preceding "
            "fsync can drain the shared journal and leave no block write "
            "for dm-flakey to fail at those exact calls."
        ),
        (
            "M10 remains the process-crash replay evidence for namespace "
            "ordering and fresh-authority classification."
        ),
        (
            "Passing M11b does not authorize automatic orphan deletion or "
            "replace the separate authority-wide recovery-exclusion problem."
        ),
    ],
}

if not artifact["all_authority_fail_closed"]:
    raise SystemExit("A1-M11b observed an fsync EIO that did not fail closed")
if not artifact["all_e2fsck_recoverable"]:
    raise SystemExit("A1-M11b produced a non-recoverable filesystem image")
if not artifact["all_exact_error_contexts"]:
    raise SystemExit("A1-M11b did not isolate every dm-flakey fsync context")
if not artifact["namespace_parent_fsync_probe_qualified"]:
    raise SystemExit("A1-M11b exact namespace parent fsync EIO probe failed")
if not artifact["source_parent_fsync_probe_qualified"]:
    raise SystemExit("A1-M11b exact source-parent fsync EIO probe failed")

pathlib.Path(output).write_text(
    json.dumps(artifact, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

print(pathlib.Path(output).read_text(encoding="utf-8"))
PY
