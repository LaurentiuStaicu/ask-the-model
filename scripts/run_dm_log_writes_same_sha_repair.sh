#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"
UPSTREAM_DIR="${2:?upstream log-writes directory required}"
UPSTREAM_COMMIT="${3:?upstream commit required}"
HELPER="${4:?A1 replay helper path required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m9-XXXXXX)"
RESULTS_DIR="$WORK_ROOT/results"
mkdir -p "$RESULTS_DIR" "$(dirname "$OUTPUT")"

cleanup() {
    set +e
    for mapper in $(sudo dmsetup ls --target log-writes 2>/dev/null | awk '/atm-a1-m9-/{print $1}'); do
        sudo dmsetup remove --retry "$mapper" >/dev/null 2>&1 || true
    done
    for loop in $(sudo losetup -a | awk -F: -v root="$WORK_ROOT" '$0 ~ root {print $1}'); do
        sudo losetup -d "$loop" >/dev/null 2>&1 || true
    done
    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT

test -x "$UPSTREAM_DIR/replay-log"
test -x "$HELPER"

sudo modprobe dm-log-writes >/dev/null 2>&1 || true
sudo dmsetup targets | awk '{print $1}' | grep -Fxq "log-writes"

run_boundary() {
    local strategy="$1"
    local strategy_slug="$2"
    local checkpoint="$3"
    local boundary_slug="$4"
    local expected_classification="$5"
    local expected_generation="$6"

    local scenario="${strategy_slug}_${boundary_slug}"
    local work="$WORK_ROOT/$scenario"
    local data_image="$work/data.img"
    local log_image="$work/log.img"
    local replay_image="$work/replay.img"
    local original_mount="$work/original"
    local replay_mount="$work/replay"
    local mapper_name="atm-a1-m9-${GITHUB_RUN_ID:-$$}-$scenario"
    local mapper_path="/dev/mapper/$mapper_name"
    local data_loop=""
    local log_loop=""
    local replay_loop=""
    local child_pid=""
    local mapper_created=0
    local original_mounted=0
    local replay_mounted=0

    scenario_cleanup() {
        set +e
        if [[ "$replay_mounted" -eq 1 ]]; then
            sudo umount "$replay_mount" >/dev/null 2>&1 || true
            replay_mounted=0
        fi
        if [[ "$original_mounted" -eq 1 ]]; then
            sudo umount "$original_mount" >/dev/null 2>&1 || true
            original_mounted=0
        fi
        if [[ "$mapper_created" -eq 1 ]]; then
            sudo dmsetup remove --retry "$mapper_name" >/dev/null 2>&1 || true
            mapper_created=0
        fi
        for loop in "$replay_loop" "$log_loop" "$data_loop"; do
            if [[ -n "$loop" ]]; then
                sudo losetup -d "$loop" >/dev/null 2>&1 || true
            fi
        done
        if [[ -n "$child_pid" ]]; then
            kill -9 "$child_pid" >/dev/null 2>&1 || true
            wait "$child_pid" >/dev/null 2>&1 || true
        fi
        set -e
    }

    mkdir -p "$work" "$original_mount" "$replay_mount"

    truncate -s 128M "$data_image"
    truncate -s 384M "$log_image"
    truncate -s 128M "$replay_image"

    data_loop="$(sudo losetup --find --show "$data_image")"
    log_loop="$(sudo losetup --find --show "$log_image")"
    replay_loop="$(sudo losetup --find --show "$replay_image")"

    local sectors
    sectors="$(sudo blockdev --getsz "$data_loop")"

    sudo dmsetup create "$mapper_name" \
        --table "0 $sectors log-writes $data_loop $log_loop"
    mapper_created=1

    sudo mkfs.ext4 -F -m 0 "$mapper_path" >/dev/null
    sudo dmsetup message "$mapper_name" 0 mark mkfs

    sudo mount "$mapper_path" "$original_mount"
    original_mounted=1
    sudo chown "$(id -u):$(id -g)" "$original_mount"

    "$HELPER" --initialize-invalid-repair "$original_mount" \
        >"$work/invalid-baseline.json"

    # The same-SHA baseline deliberately contains durable corrupted bytes
    # while Control DB still stores the previous valid seal.
    sync -f "$original_mount"
    sudo dmsetup message "$mapper_name" 0 mark baseline

    local checkpoint_fifo="$work/checkpoint.pipe"
    local control_fifo="$work/control.pipe"
    mkfifo "$checkpoint_fifo" "$control_fifo"
    exec 3<>"$checkpoint_fifo"
    exec 4<>"$control_fifo"

    "$HELPER" \
        --repair-same-sha-candidate-boundary \
        "$original_mount" \
        "$strategy" \
        "$checkpoint" \
        3>&3 4<&4 \
        >"$work/repair.json" \
        2>"$work/repair.stderr" &
    child_pid=$!

    local observed_checkpoint
    IFS= read -r -u 3 observed_checkpoint
    test "$observed_checkpoint" = "$checkpoint"

    local mark_name="$scenario"
    sudo dmsetup message "$mapper_name" 0 mark "$mark_name"

    kill -9 "$child_pid"
    wait "$child_pid" >/dev/null 2>&1 || true
    child_pid=""

    exec 3>&-
    exec 4>&-

    # Teardown happens after the target mark and therefore cannot become
    # part of the replayed repair durability observation.
    sudo umount "$original_mount"
    original_mounted=0
    sudo dmsetup remove --retry "$mapper_name"
    mapper_created=0

    local baseline_entry
    local scenario_entry
    baseline_entry="$(sudo "$UPSTREAM_DIR/replay-log" \
        --log "$log_loop" \
        --find \
        --end-mark baseline)"
    scenario_entry="$(sudo "$UPSTREAM_DIR/replay-log" \
        --log "$log_loop" \
        --find \
        --end-mark "$mark_name")"
    test "$scenario_entry" -gt "$baseline_entry"

    sudo "$UPSTREAM_DIR/replay-log" \
        --log "$log_loop" \
        --replay "$replay_loop" \
        --end-mark "$mark_name"

    set +e
    sudo e2fsck -fy "$replay_loop" \
        >"$work/e2fsck.txt" 2>&1
    local e2fsck_rc=$?
    set -e

    if [[ "$e2fsck_rc" -gt 2 ]]; then
        cat "$work/e2fsck.txt" >&2
        scenario_cleanup
        return "$e2fsck_rc"
    fi

    sudo mount "$replay_loop" "$replay_mount"
    replay_mounted=1

    local verify_json="$work/verify.json"
    "$HELPER" --verify-repair "$replay_mount" >"$verify_json"
    python3 -m json.tool "$verify_json" >/dev/null

    sudo umount "$replay_mount"
    replay_mounted=0

    python3 - \
        "$RESULTS_DIR/$scenario.json" \
        "$strategy" \
        "$checkpoint" \
        "$boundary_slug" \
        "$expected_classification" \
        "$expected_generation" \
        "$baseline_entry" \
        "$scenario_entry" \
        "$e2fsck_rc" \
        "$verify_json" <<'PY'
import json
import pathlib
import sys

(
    output,
    strategy,
    checkpoint,
    boundary,
    expected_classification,
    expected_generation,
    baseline_entry,
    scenario_entry,
    e2fsck_rc,
    verify_path,
) = sys.argv[1:]

with open(verify_path, encoding="utf-8") as handle:
    verification = json.load(handle)

expected_generation = int(expected_generation)
pre_authority = expected_classification == "REPAIR_REQUIRED"

record = {
    "strategy": strategy,
    "checkpoint": checkpoint,
    "boundary": boundary,
    "expected_classification": expected_classification,
    "expected_generation": expected_generation,
    "baseline_entry": int(baseline_entry),
    "scenario_entry": int(scenario_entry),
    "e2fsck_exit_code": int(e2fsck_rc),
    "verification": verification,
    "expected_match": (
        verification.get("classification") ==
        expected_classification
        and verification.get("active_generation_id") ==
        expected_generation
    ),
    "fail_closed": (
        pre_authority
        and verification.get("classification") == "REPAIR_REQUIRED"
        and verification.get("qualified") is False
    ),
    "repaired_valid": (
        not pre_authority
        and verification.get("classification") ==
        "REPAIRED_AUTHORITY_VALID"
        and verification.get("seal_match") is True
        and verification.get("qualified") is True
    ),
}

record["repair_invariant_satisfied"] = (
    record["expected_match"]
    and (
        record["fail_closed"]
        if pre_authority
        else record["repaired_valid"]
    )
)

pathlib.Path(output).write_text(
    json.dumps(record, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY

    scenario_cleanup
}

for strategy_record in \
    "S1_TARGETED_FSYNC:s1" \
    "S2_SYNCFS:s2"; do
    strategy="${strategy_record%%:*}"
    slug="${strategy_record##*:}"

    run_boundary \
        "$strategy" "$slug" \
        repair_post_quarantine \
        post_quarantine \
        REPAIR_REQUIRED \
        1

    run_boundary \
        "$strategy" "$slug" \
        repair_post_barrier_pre_rename \
        post_barrier_pre_rename \
        REPAIR_REQUIRED \
        1

    run_boundary \
        "$strategy" "$slug" \
        repair_post_rename_pre_parent_fsync \
        post_rename_pre_parent_fsync \
        REPAIR_REQUIRED \
        1

    run_boundary \
        "$strategy" "$slug" \
        repair_post_parent_fsync_pre_authority \
        post_parent_fsync_pre_authority \
        REPAIR_REQUIRED \
        1

    run_boundary \
        "$strategy" "$slug" \
        repair_after_authority \
        after_authority \
        REPAIRED_AUTHORITY_VALID \
        2
done

python3 - "$OUTPUT" "$RESULTS_DIR" "$UPSTREAM_COMMIT" <<'PY'
import json
import pathlib
import platform
import sys

output, results_dir, upstream_commit = sys.argv[1:]
results = []

for path in sorted(pathlib.Path(results_dir).glob("*.json")):
    with path.open(encoding="utf-8") as handle:
        results.append(json.load(handle))

strategies = {"S1_TARGETED_FSYNC", "S2_SYNCFS"}
boundaries = {
    "post_quarantine",
    "post_barrier_pre_rename",
    "post_rename_pre_parent_fsync",
    "post_parent_fsync_pre_authority",
    "after_authority",
}

if len(results) != 10:
    raise SystemExit("A1-M9 must contain exactly ten scenarios")
if {item["strategy"] for item in results} != strategies:
    raise SystemExit("A1-M9 strategy set is incomplete")
if {item["boundary"] for item in results} != boundaries:
    raise SystemExit("A1-M9 repair boundary set is incomplete")

for strategy in strategies:
    subset = [item for item in results if item["strategy"] == strategy]
    if {item["boundary"] for item in subset} != boundaries:
        raise SystemExit(f"{strategy}: incomplete repair boundary matrix")

artifact = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m9-same-sha-repair-replay-v1",
    "status": "measurement-only",
    "production_barrier_selected": False,
    "kernel": {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
    },
    "upstream": {
        "repository": "josefbacik/log-writes",
        "commit": upstream_commit,
    },
    "protocol": {
        "baseline_is_durable_corrupted_same_sha": True,
        "control_db_retains_pre_corruption_seal": True,
        "fresh_process_repair_verifier_after_replay": True,
        "clean_unmount_not_used_as_target_mark": True,
        "quarantine_parent_fsync_added": False,
        "candidate_set": [
            "S1_TARGETED_FSYNC",
            "S2_SYNCFS",
        ],
        "boundary_order": [
            "post_quarantine",
            "post_barrier_pre_rename",
            "post_rename_pre_parent_fsync",
            "post_parent_fsync_pre_authority",
            "after_authority",
        ],
    },
    "results": results,
    "all_repair_invariants_satisfied": all(
        item["repair_invariant_satisfied"]
        for item in results
    ),
    "all_pre_authority_fail_closed": all(
        item["fail_closed"]
        for item in results
        if item["boundary"] != "after_authority"
    ),
    "all_after_authority_repaired_valid": all(
        item["repaired_valid"]
        for item in results
        if item["boundary"] == "after_authority"
    ),
    "limitations": [
        (
            "M9 qualifies same-SHA repair/quarantine power-loss ordering "
            "in the deterministic ext4 Tier-2 fixture."
        ),
        (
            "The invalid baseline is deliberate: pre-authority replay is "
            "expected to remain unqualified rather than old-valid."
        ),
        (
            "M9 does not add a quarantine fsync; it tests whether fail-closed "
            "recovery makes one unnecessary for authority correctness."
        ),
        (
            "Passing M9 still does not by itself select a production barrier."
        ),
    ],
}

pathlib.Path(output).write_text(
    json.dumps(artifact, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

print(pathlib.Path(output).read_text(encoding="utf-8"))

if not (
    artifact["all_repair_invariants_satisfied"]
    and artifact["all_pre_authority_fail_closed"]
    and artifact["all_after_authority_repaired_valid"]
):
    raise SystemExit(1)
PY
