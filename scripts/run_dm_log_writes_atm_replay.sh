#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"
UPSTREAM_DIR="${2:?upstream log-writes directory required}"
UPSTREAM_COMMIT="${3:?upstream commit required}"
HELPER="${4:?A1 replay helper path required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m4-XXXXXX)"
RESULTS_DIR="$WORK_ROOT/results"
mkdir -p "$RESULTS_DIR" "$(dirname "$OUTPUT")"

cleanup() {
    set +e
    for mapper in $(sudo dmsetup ls --target log-writes 2>/dev/null | awk '/atm-a1-m4-/{print $1}'); do
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

run_scenario() {
    local scenario="$1"
    local checkpoint="$2"
    local expected_classification="$3"

    local work="$WORK_ROOT/$scenario"
    local data_image="$work/data.img"
    local log_image="$work/log.img"
    local replay_image="$work/replay.img"
    local original_mount="$work/original"
    local replay_mount="$work/replay"
    local mapper_name="atm-a1-m4-${GITHUB_RUN_ID:-$$}-${scenario//_/-}"
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

    "$HELPER" --initialize-old "$original_mount" \
        >"$work/old.json"

    # Establish one explicitly durable old-authority baseline. All later
    # scenario marks model the new promotion relative to this known-good
    # starting point.
    sync -f "$original_mount"
    sudo dmsetup message "$mapper_name" 0 mark baseline

    local mark_name="$scenario"

    if [[ "$checkpoint" == "after_authority" ]]; then
        "$HELPER" --promote-new "$original_mount" no_checkpoint \
            >"$work/new.json"
        sudo dmsetup message "$mapper_name" 0 mark "$mark_name"
    else
        local checkpoint_fifo="$work/checkpoint.pipe"
        local control_fifo="$work/control.pipe"

        mkfifo "$checkpoint_fifo" "$control_fifo"
        exec 3<>"$checkpoint_fifo"
        exec 4<>"$control_fifo"

        "$HELPER" --promote-new "$original_mount" "$checkpoint" \
            3>&3 4<&4 >"$work/new.json" 2>"$work/new.stderr" &
        child_pid=$!

        local observed_checkpoint
        IFS= read -r -u 3 observed_checkpoint
        test "$observed_checkpoint" = "$checkpoint"

        sudo dmsetup message "$mapper_name" 0 mark "$mark_name"

        kill -9 "$child_pid"
        wait "$child_pid" >/dev/null 2>&1 || true
        child_pid=""

        exec 3>&-
        exec 4>&-
    fi

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
    "$HELPER" --verify "$replay_mount" >"$verify_json"
    python3 -m json.tool "$verify_json" >/dev/null

    sudo umount "$replay_mount"
    replay_mounted=0

    python3 - \
        "$RESULTS_DIR/$scenario.json" \
        "$scenario" \
        "$checkpoint" \
        "$expected_classification" \
        "$baseline_entry" \
        "$scenario_entry" \
        "$e2fsck_rc" \
        "$verify_json" <<'PY'
import json
import pathlib
import sys

(
    output,
    scenario,
    checkpoint,
    expected_classification,
    baseline_entry,
    scenario_entry,
    e2fsck_rc,
    verify_path,
) = sys.argv[1:]

with open(verify_path, encoding="utf-8") as handle:
    verification = json.load(handle)

record = {
    "scenario": scenario,
    "checkpoint": checkpoint,
    "expected_classification": expected_classification,
    "baseline_entry": int(baseline_entry),
    "scenario_entry": int(scenario_entry),
    "e2fsck_exit_code": int(e2fsck_rc),
    "verification": verification,
    "expected_match": (
        verification.get("classification") ==
        expected_classification
    ),
    "strong_invariant_satisfied": (
        verification.get("classification") in {
            "OLD_AUTHORITY_VALID",
            "NEW_AUTHORITY_VALID",
        }
        and verification.get("seal_match") is True
    ),
}

pathlib.Path(output).write_text(
    json.dumps(record, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY

    scenario_cleanup
}

run_scenario \
    pre_rename \
    snapshot_pre_rename \
    OLD_AUTHORITY_VALID

run_scenario \
    post_rename \
    snapshot_post_rename \
    OLD_AUTHORITY_VALID

# Baseline/current runtime: no snapshot durability barrier is added here.
# The helper completes the current promotion + Control DB activation path,
# then the mark is emitted immediately, before any explicit filesystem sync.
# Whatever the replay oracle observes is retained as evidence.
run_scenario \
    after_authority \
    after_authority \
    NEW_AUTHORITY_VALID

python3 - "$OUTPUT" "$RESULTS_DIR" "$UPSTREAM_COMMIT" <<'PY'
import json
import platform
import pathlib
import sys

output, results_dir, upstream_commit = sys.argv[1:]
results = []

for path in sorted(pathlib.Path(results_dir).glob("*.json")):
    with path.open(encoding="utf-8") as handle:
        results.append(json.load(handle))

if {item["scenario"] for item in results} != {
    "pre_rename",
    "post_rename",
    "after_authority",
}:
    raise SystemExit("A1-M4 scenario set is incomplete")

for item in results:
    if item["e2fsck_exit_code"] not in {0, 1, 2}:
        raise SystemExit(
            f"{item['scenario']}: unaccepted e2fsck result"
        )
    if not item["strong_invariant_satisfied"]:
        # This is an observed result, not a workflow failure. The purpose
        # of M4 is to discover whether current baseline semantics survive
        # Tier-2 replay, including a potentially unsafe after-authority
        # outcome.
        pass

artifact = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m4-atm-replay-oracle-v1",
    "status": "measurement-only",
    "production_durability_authorized": False,
    "strategy": "S3_CURRENT_BASELINE",
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
        "old_authority_synced_before_baseline_mark": True,
        "new_snapshot_uses_real_snapshot_seal": True,
        "fresh_process_verifier_after_replay": True,
        "candidate_durability_barrier_added": False,
        "clean_unmount_not_used_as_target_mark": True,
    },
    "scenarios": results,
    "limitations": [
        (
            "M4 qualifies the AtM replay oracle against the current S3 "
            "baseline only; it does not yet test S1 targeted fsync or "
            "S2 syncfs as candidate production barriers."
        ),
        (
            "The fixture is deterministic and seal-aware but is not a "
            "full EWD/CBD/RMD repository snapshot."
        ),
        (
            "No production durability behavior is selected or changed."
        ),
    ],
}

pathlib.Path(output).write_text(
    json.dumps(artifact, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

print(pathlib.Path(output).read_text(encoding="utf-8"))
PY
