#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"
HELPER="${2:?A1 snapshot replay helper required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m8-XXXXXX)"
RESULTS_DIR="$WORK_ROOT/results"
mkdir -p "$RESULTS_DIR" "$(dirname "$OUTPUT")"

cleanup_root() {
    local mapper_name="$1"
    local mount_path="$2"
    local verify_mount="$3"
    local loop_dev="$4"

    set +e
    sudo umount "$verify_mount" >/dev/null 2>&1 || true
    sudo umount "$mount_path" >/dev/null 2>&1 || true
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
        cleanup_root \
            "$ACTIVE_MAPPER" \
            "$ACTIVE_LIVE_MOUNT" \
            "$ACTIVE_VERIFY_MOUNT" \
            "$ACTIVE_LOOP"
    fi
    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT

sudo modprobe dm-flakey >/dev/null 2>&1 || true
sudo dmsetup targets | awk '{print $1}' | grep -Fxq "flakey"

run_scenario() {
    local strategy="$1"
    local slug="$2"
    local checkpoint="$3"
    local boundary="$4"

    local work="$WORK_ROOT/$slug-$boundary"
    local data_image="$work/data.img"
    local live_mount="$work/live"
    local verify_mount="$work/verify"
    local mapper_name="atm-a1-m8-${GITHUB_RUN_ID:-$$}-$slug-${boundary//_/-}"
    local mapper_path="/dev/mapper/$mapper_name"
    local loop_dev=""
    local mapper_created=0
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

    sudo dmsetup create "$mapper_name" \
        --table "0 $sectors linear $loop_dev 0"
    mapper_created=1

    sudo mkfs.ext4 -F -m 0 "$mapper_path" >/dev/null
    sudo mount "$mapper_path" "$live_mount"
    live_mounted=1
    sudo chown "$(id -u):$(id -g)" "$live_mount"

    "$HELPER" --initialize-old "$live_mount" \
        >"$work/old.json"
    python3 -m json.tool "$work/old.json" >/dev/null

    # The old authority is the durable baseline that must survive every
    # candidate error scenario.
    sync -f "$live_mount"

    local checkpoint_pipe="$work/checkpoint.pipe"
    local control_pipe="$work/control.pipe"
    mkfifo "$checkpoint_pipe" "$control_pipe"

    exec {CHECKPOINT_FD}<>"$checkpoint_pipe"
    exec {CONTROL_FD}<>"$control_pipe"

    set +e
    "$HELPER" \
        --promote-new-candidate-boundary \
        "$live_mount" \
        "$strategy" \
        "$checkpoint" \
        3>"$checkpoint_pipe" \
        4<"$control_pipe" \
        >"$work/helper.stdout" \
        2>"$work/helper.stderr" &
    local helper_pid=$!
    set -e

    local observed_checkpoint=""
    IFS= read -r observed_checkpoint <&$CHECKPOINT_FD
    test "$observed_checkpoint" = "$checkpoint"

    # Do not let the table switch itself synchronize the filesystem or
    # flush outstanding I/O: the candidate durability operation under test
    # must be the first durability boundary after this checkpoint.
    sudo dmsetup suspend --noflush --nolockfs "$mapper_name"
    sudo dmsetup reload "$mapper_name" \
        --table "0 $sectors flakey $loop_dev 0 1 600 1 error_writes"
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
        printf '%s\n' "M8 expected candidate failure under error_writes" >&2
        cat "$work/helper.stdout" >&2 || true
        exit 1
    fi

    if ! grep -Eiq         'input/output error|i/o error|disk i/o error'         "$work/helper.stderr"; then
        printf '%s\n' "M8 candidate failed without an explicit I/O error" >&2
        cat "$work/helper.stderr" >&2 || true
        exit 1
    fi

    sudo dmsetup suspend --noflush --nolockfs "$mapper_name"
    sudo dmsetup reload "$mapper_name" \
        --table "0 $sectors linear $loop_dev 0"
    sudo dmsetup resume "$mapper_name"
    sudo dmsetup table "$mapper_name" | grep -Fq " linear "

    set +e
    sudo umount "$live_mount"
    local umount_rc=$?
    set -e
    if [[ "$umount_rc" -ne 0 ]]; then
        cleanup_root "$mapper_name" "$live_mount" "$verify_mount" "$loop_dev"
        exit "$umount_rc"
    fi
    live_mounted=0

    sudo dmsetup remove --retry "$mapper_name"
    mapper_created=0

    set +e
    sudo e2fsck -fy "$loop_dev" >"$work/e2fsck.txt" 2>&1
    local e2fsck_rc=$?
    set -e
    if [[ "$e2fsck_rc" -gt 2 ]]; then
        cat "$work/e2fsck.txt" >&2
        cleanup_root "$mapper_name" "$live_mount" "$verify_mount" "$loop_dev"
        exit "$e2fsck_rc"
    fi

    sudo mount "$loop_dev" "$verify_mount"
    verify_mounted=1

    "$HELPER" --verify "$verify_mount" >"$work/verify.json"
    python3 -m json.tool "$work/verify.json" >/dev/null

    local result_path="$RESULTS_DIR/$slug-$boundary.json"

    python3 - \
        "$result_path" \
        "$strategy" \
        "$boundary" \
        "$checkpoint" \
        "$helper_rc" \
        "$e2fsck_rc" \
        "$work/helper.stderr" \
        "$work/verify.json" <<'PY'
import json
import pathlib
import sys

(
    output,
    strategy,
    boundary,
    checkpoint,
    helper_rc,
    e2fsck_rc,
    stderr_path,
    verify_path,
) = sys.argv[1:]

with open(verify_path, encoding="utf-8") as handle:
    verify = json.load(handle)

stderr = pathlib.Path(stderr_path).read_text(
    encoding="utf-8",
    errors="replace",
).strip()

record = {
    "strategy": strategy,
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
    "e2fsck_exit_code": int(e2fsck_rc),
    "classification": verify["classification"],
    "active_repository_sha": verify.get("active_repository_sha"),
    "seal_match": verify["seal_match"],
    "qualified": verify["qualified"],
    "reason_code": verify["reason_code"],
    "old_snapshot_exists": verify["old_snapshot_exists"],
    "new_snapshot_exists": verify["new_snapshot_exists"],
    "new_staging_exists": verify["new_staging_exists"],
    "expected_classification": "OLD_AUTHORITY_VALID",
}
record["expected_match"] = (
    record["classification"] == record["expected_classification"]
    and record["seal_match"] is True
    and record["qualified"] is True
)
record["fail_closed"] = (
    record["candidate_failed"]
    and record["io_error_observed"]
    and record["expected_match"]
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

for strategy in S1_TARGETED_FSYNC S2_SYNCFS; do
    case "$strategy" in
        S1_TARGETED_FSYNC) slug="s1" ;;
        S2_SYNCFS) slug="s2" ;;
    esac

    run_scenario \
        "$strategy" "$slug" \
        candidate_pre_barrier \
        pre_rename_barrier

    run_scenario \
        "$strategy" "$slug" \
        candidate_post_rename_pre_parent_fsync \
        promoted_parent_fsync

    run_scenario \
        "$strategy" "$slug" \
        candidate_post_parent_fsync_pre_authority \
        control_db_activation
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
boundaries = {
    "pre_rename_barrier",
    "promoted_parent_fsync",
    "control_db_activation",
}

if len(results) != 6:
    raise SystemExit("A1-M8 must contain exactly six scenarios")
if {item["strategy"] for item in results} != strategies:
    raise SystemExit("A1-M8 strategy set is incomplete")
if {item["boundary"] for item in results} != boundaries:
    raise SystemExit("A1-M8 boundary set is incomplete")

for strategy in strategies:
    subset = [item for item in results if item["strategy"] == strategy]
    if {item["boundary"] for item in subset} != boundaries:
        raise SystemExit(f"{strategy}: incomplete EIO boundary matrix")

artifact = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m8-candidate-error-injection-v1",
    "status": "measurement-only",
    "production_barrier_selected": False,
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
        "old_authority_synced_before_each_scenario": True,
        "fault_teardown_suspend_noflush_nolockfs": True,
        "fresh_process_seal_verifier_after_recovery": True,
        "candidate_set": [
            "S1_TARGETED_FSYNC",
            "S2_SYNCFS",
        ],
        "boundary_order": [
            "pre_rename_barrier",
            "promoted_parent_fsync",
            "control_db_activation",
        ],
    },
    "results": results,
    "all_candidates_failed_on_eio": all(
        item["candidate_failed"] and item["io_error_observed"]
        for item in results
    ),
    "all_old_authority_preserved": all(
        item["expected_match"]
        for item in results
    ),
    "all_fail_closed": all(
        item["fail_closed"]
        for item in results
    ),
    "limitations": [
        (
            "M8 is deterministic dm-flakey EIO qualification on the hosted "
            "runner, not a physical device failure or desktop latency test."
        ),
        (
            "M8 evaluates candidate error propagation and authority safety; "
            "M1 remains the EWD/CBD/RMD comparative performance evidence."
        ),
        (
            "Passing M8 does not by itself select S1 or S2 for production."
        ),
    ],
}

pathlib.Path(output).write_text(
    json.dumps(artifact, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

print(pathlib.Path(output).read_text(encoding="utf-8"))

if not (
    artifact["all_candidates_failed_on_eio"]
    and artifact["all_old_authority_preserved"]
    and artifact["all_fail_closed"]
):
    raise SystemExit(1)
PY
