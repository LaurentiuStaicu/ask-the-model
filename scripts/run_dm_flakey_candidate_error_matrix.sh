#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"
HELPER="${2:?A1 replay helper path required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m8-XXXXXX)"
RESULTS_DIR="$WORK_ROOT/results"
mkdir -p "$RESULTS_DIR" "$(dirname "$OUTPUT")"

cleanup() {
    set +e

    for mapper in $(sudo dmsetup ls 2>/dev/null | awk '/atm-a1-m8-/{print $1}'); do
        sudo dmsetup remove --retry "$mapper" >/dev/null 2>&1 || true
    done

    for loop in $(sudo losetup -a | awk -F: -v root="$WORK_ROOT" '$0 ~ root {print $1}'); do
        sudo losetup -d "$loop" >/dev/null 2>&1 || true
    done

    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT

test -x "$HELPER"

sudo modprobe dm-flakey >/dev/null 2>&1 || true
sudo dmsetup targets | awk '{print $1}' | grep -Fxq "flakey"

run_error_scenario() {
    local strategy="$1"
    local strategy_slug="$2"
    local checkpoint="$3"
    local stage="$4"

    local scenario="${strategy_slug}_${stage}"
    local work="$WORK_ROOT/$scenario"
    local data_image="$work/data.img"
    local live_mount="$work/live"
    local verify_mount="$work/verify"
    local mapper_name="atm-a1-m8-${GITHUB_RUN_ID:-$$}-$scenario"
    local mapper_path="/dev/mapper/$mapper_name"

    local data_loop=""
    local mapper_created=0
    local live_mounted=0
    local verify_mounted=0
    local child_pid=""

    scenario_cleanup() {
        set +e

        if [[ "$verify_mounted" -eq 1 ]]; then
            sudo umount "$verify_mount" >/dev/null 2>&1 || true
            verify_mounted=0
        fi

        if [[ "$live_mounted" -eq 1 ]]; then
            sudo umount "$live_mount" >/dev/null 2>&1 || true
            live_mounted=0
        fi

        if [[ "$mapper_created" -eq 1 ]]; then
            sudo dmsetup remove --retry "$mapper_name" >/dev/null 2>&1 || true
            mapper_created=0
        fi

        if [[ -n "$data_loop" ]]; then
            sudo losetup -d "$data_loop" >/dev/null 2>&1 || true
        fi

        if [[ -n "$child_pid" ]]; then
            kill -9 "$child_pid" >/dev/null 2>&1 || true
            wait "$child_pid" >/dev/null 2>&1 || true
        fi

        set -e
    }

    mkdir -p "$work" "$live_mount" "$verify_mount"

    truncate -s 128M "$data_image"
    data_loop="$(sudo losetup --find --show "$data_image")"
    local sectors
    sectors="$(sudo blockdev --getsz "$data_loop")"

    sudo dmsetup create "$mapper_name"         --table "0 $sectors linear $data_loop 0"
    mapper_created=1

    sudo mkfs.ext4 -F -m 0 "$mapper_path" >/dev/null
    sudo mount "$mapper_path" "$live_mount"
    live_mounted=1
    sudo chown "$(id -u):$(id -g)" "$live_mount"

    "$HELPER" --initialize-old "$live_mount"         >"$work/old.json"

    # The old authority is the known-good durable baseline. Candidate writes
    # happen only after this point.
    sync -f "$live_mount"

    local checkpoint_fifo="$work/checkpoint.pipe"
    local control_fifo="$work/control.pipe"

    mkfifo "$checkpoint_fifo" "$control_fifo"
    exec 3<>"$checkpoint_fifo"
    exec 4<>"$control_fifo"

    "$HELPER"         --promote-new-candidate-boundary         "$live_mount"         "$strategy"         "$checkpoint"         3>&3 4<&4         >"$work/helper.stdout"         2>"$work/helper.stderr" &
    child_pid=$!

    local observed_checkpoint
    IFS= read -r -u 3 observed_checkpoint
    test "$observed_checkpoint" = "$checkpoint"

    # Enter the fault state without asking the mounted filesystem to sync.
    # A normal suspend would itself flush the exact dirty state that this
    # scenario is meant to subject to error_writes.
    sudo dmsetup suspend --noflush --nolockfs "$mapper_name"
    sudo dmsetup reload "$mapper_name"         --table "0 $sectors flakey $data_loop 0 1 600 1 error_writes"

    # The flakey interval begins when the table is loaded. Keep the mapper
    # suspended until the 1-second healthy interval has elapsed so no dirty
    # candidate state can reach the device before error_writes is active.
    sleep 2

    sudo dmsetup resume "$mapper_name"

    local flakey_active=false
    if sudo dmsetup table "$mapper_name" |
        grep -Fq " flakey "; then
        flakey_active=true
    fi

    printf 'C' >&4
    exec 4>&-

    set +e
    wait "$child_pid"
    local helper_rc=$?
    set -e
    child_pid=""

    exec 3>&-

    local helper_failed=false
    if [[ "$helper_rc" -ne 0 ]]; then
        helper_failed=true
    fi

    # Leave fault mode without re-synchronizing the errored filesystem as
    # part of the device-mapper suspend operation. This is harness teardown,
    # not candidate behavior.
    sudo dmsetup suspend --noflush --nolockfs "$mapper_name"
    sudo dmsetup reload "$mapper_name"         --table "0 $sectors linear $data_loop 0"
    sudo dmsetup resume "$mapper_name"

    local linear_restored=false
    if sudo dmsetup table "$mapper_name" |
        grep -Fq " linear "; then
        linear_restored=true
    fi

    # Once the fault has been observed and the mapping is healthy again,
    # allow normal unmount/recovery to settle any non-authoritative dirty
    # state before the fresh-process authority check.
    set +e
    sync -f "$live_mount" >/dev/null 2>&1
    local post_fault_sync_rc=$?
    sudo umount "$live_mount"
    local umount_rc=$?
    set -e

    if [[ "$umount_rc" -ne 0 ]]; then
        scenario_cleanup
        return "$umount_rc"
    fi
    live_mounted=0

    sudo dmsetup remove --retry "$mapper_name"
    mapper_created=0

    set +e
    sudo e2fsck -fy "$data_loop"         >"$work/e2fsck.txt" 2>&1
    local e2fsck_rc=$?
    set -e

    if [[ "$e2fsck_rc" -gt 2 ]]; then
        cat "$work/e2fsck.txt" >&2
        scenario_cleanup
        return "$e2fsck_rc"
    fi

    sudo mount -o ro "$data_loop" "$verify_mount"
    verify_mounted=1

    "$HELPER" --verify "$verify_mount"         >"$work/verify.json"
    python3 -m json.tool "$work/verify.json" >/dev/null

    sudo umount "$verify_mount"
    verify_mounted=0

    python3 -         "$RESULTS_DIR/$scenario.json"         "$strategy"         "$stage"         "$checkpoint"         "$flakey_active"         "$helper_rc"         "$helper_failed"         "$linear_restored"         "$post_fault_sync_rc"         "$e2fsck_rc"         "$work/helper.stderr"         "$work/verify.json" <<'PY'
import json
import pathlib
import sys

(
    output,
    strategy,
    stage,
    checkpoint,
    flakey_active,
    helper_rc,
    helper_failed,
    linear_restored,
    post_fault_sync_rc,
    e2fsck_rc,
    stderr_path,
    verify_path,
) = sys.argv[1:]

with open(verify_path, encoding="utf-8") as handle:
    verification = json.load(handle)

stderr = pathlib.Path(stderr_path).read_text(
    encoding="utf-8",
    errors="replace",
).strip()

expected_error_marker = {
    "barrier": "candidate barrier failed:",
    "parent_fsync": "candidate parent fsync failed:",
    "authority": "candidate authority activation failed:",
}[stage]
failure_stage_match = expected_error_marker in stderr.lower()

fail_closed = (
    flakey_active == "true"
    and helper_failed == "true"
    and failure_stage_match
    and linear_restored == "true"
    and verification.get("classification") == "OLD_AUTHORITY_VALID"
    and verification.get("active_repository_sha")
        == "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    and verification.get("seal_match") is True
    and verification.get("qualified") is True
)

record = {
    "strategy": strategy,
    "stage": stage,
    "checkpoint": checkpoint,
    "fault_feature": "error_writes",
    "flakey_table_active": flakey_active == "true",
    "helper_exit_code": int(helper_rc),
    "helper_failed": helper_failed == "true",
    "expected_error_marker": expected_error_marker,
    "failure_stage_match": failure_stage_match,
    "helper_stderr": stderr[:1000],
    "linear_table_restored": linear_restored == "true",
    "post_fault_sync_exit_code": int(post_fault_sync_rc),
    "e2fsck_exit_code": int(e2fsck_rc),
    "verification": verification,
    "fail_closed": fail_closed,
}

pathlib.Path(output).write_text(
    json.dumps(record, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY

    scenario_cleanup
}

for strategy_record in     "S1_TARGETED_FSYNC:s1"     "S2_SYNCFS:s2"; do
    strategy="${strategy_record%%:*}"
    slug="${strategy_record##*:}"

    run_error_scenario         "$strategy"         "$slug"         candidate_pre_barrier         barrier

    run_error_scenario         "$strategy"         "$slug"         candidate_post_rename_pre_parent_fsync         parent_fsync

    run_error_scenario         "$strategy"         "$slug"         candidate_post_parent_fsync_pre_authority         authority
done

python3 - "$OUTPUT" "$RESULTS_DIR" <<'PY'
import json
import pathlib
import platform
import sys

output, results_dir = sys.argv[1:]
results = []

for path in sorted(pathlib.Path(results_dir).glob("*.json")):
    with path.open(encoding="utf-8") as handle:
        results.append(json.load(handle))

strategies = {"S1_TARGETED_FSYNC", "S2_SYNCFS"}
stages = {"barrier", "parent_fsync", "authority"}

if len(results) != 6:
    raise SystemExit("A1-M8 must contain exactly six scenarios")
if {item["strategy"] for item in results} != strategies:
    raise SystemExit("A1-M8 strategy set is incomplete")
if {item["stage"] for item in results} != stages:
    raise SystemExit("A1-M8 stage set is incomplete")

for strategy in strategies:
    subset = [
        item for item in results
        if item["strategy"] == strategy
    ]
    if {item["stage"] for item in subset} != stages:
        raise SystemExit(
            f"{strategy}: incomplete error-injection matrix"
        )

artifact = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m8-writeback-error-v1",
    "status": "measurement-only",
    "production_barrier_selected": False,
    "kernel": {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
    },
    "fault_model": {
        "device_mapper_target": "flakey",
        "feature": "error_writes",
        "healthy_interval_seconds": 1,
        "unreliable_interval_seconds": 600,
        "fault_entry_suspend_noflush_nolockfs": True,
        "fault_exit_suspend_noflush_nolockfs": True,
    },
    "expected_contract": {
        "helper_must_fail": True,
        "active_authority_after_recovery": "OLD_AUTHORITY_VALID",
        "old_snapshot_seal_must_match": True,
        "control_db_must_not_advance": True,
    },
    "results": results,
    "all_fail_closed": all(
        item["fail_closed"] for item in results
    ),
    "limitations": [
        (
            "M8 injects explicit write errors on one disposable ext4 "
            "filesystem; it does not model silent write loss."
        ),
        (
            "M8 is qualification-only and changes no production runtime."
        ),
        (
            "A candidate that passes M8 still requires explicit policy "
            "selection using M1 performance and scope evidence."
        ),
    ],
}

pathlib.Path(output).write_text(
    json.dumps(artifact, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

print(pathlib.Path(output).read_text(encoding="utf-8"))
PY
