#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"
UPSTREAM_DIR="${2:?upstream log-writes directory required}"
UPSTREAM_COMMIT="${3:?upstream commit required}"
HELPER="${4:?A1 replay helper path required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m5-XXXXXX)"
RESULTS_DIR="$WORK_ROOT/results"
mkdir -p "$RESULTS_DIR" "$(dirname "$OUTPUT")"

cleanup() {
    set +e
    for mapper in $(sudo dmsetup ls --target log-writes 2>/dev/null | awk '/atm-a1-m5-/{print $1}'); do
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

run_candidate() {
    local strategy="$1"
    local slug="$2"

    local work="$WORK_ROOT/$slug"
    local data_image="$work/data.img"
    local log_image="$work/log.img"
    local replay_image="$work/replay.img"
    local original_mount="$work/original"
    local replay_mount="$work/replay"
    local mapper_name="atm-a1-m5-${GITHUB_RUN_ID:-$$}-$slug"
    local mapper_path="/dev/mapper/$mapper_name"
    local data_loop=""
    local log_loop=""
    local replay_loop=""
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

    # Establish exactly the same known-good old authority baseline as M4.
    sync -f "$original_mount"
    sudo dmsetup message "$mapper_name" 0 mark baseline

    "$HELPER" \
        --promote-new-candidate \
        "$original_mount" \
        "$strategy" \
        >"$work/candidate.json"

    local mark_name="${slug}_after_authority"
    sudo dmsetup message "$mapper_name" 0 mark "$mark_name"

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

    "$HELPER" --verify "$replay_mount" \
        >"$work/verify.json"
    python3 -m json.tool "$work/verify.json" >/dev/null
    python3 -m json.tool "$work/candidate.json" >/dev/null

    sudo umount "$replay_mount"
    replay_mounted=0

    python3 - \
        "$RESULTS_DIR/$slug.json" \
        "$strategy" \
        "$baseline_entry" \
        "$scenario_entry" \
        "$e2fsck_rc" \
        "$work/candidate.json" \
        "$work/verify.json" <<'PY'
import json
import pathlib
import sys

(
    output,
    strategy,
    baseline_entry,
    scenario_entry,
    e2fsck_rc,
    candidate_path,
    verify_path,
) = sys.argv[1:]

with open(candidate_path, encoding="utf-8") as handle:
    candidate = json.load(handle)
with open(verify_path, encoding="utf-8") as handle:
    verification = json.load(handle)

record = {
    "strategy": strategy,
    "baseline_entry": int(baseline_entry),
    "scenario_entry": int(scenario_entry),
    "e2fsck_exit_code": int(e2fsck_rc),
    "barrier_counters": {
        "file_fsync_calls": candidate["file_fsync_calls"],
        "directory_fsync_calls": candidate["directory_fsync_calls"],
        "syncfs_calls": candidate["syncfs_calls"],
        "parent_fsync_calls": candidate["parent_fsync_calls"],
    },
    "verification": verification,
    "candidate_satisfied": (
        verification.get("classification") ==
        "NEW_AUTHORITY_VALID"
        and verification.get("seal_match") is True
        and verification.get("qualified") is True
    ),
}

pathlib.Path(output).write_text(
    json.dumps(record, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY

    scenario_cleanup
}

run_candidate "S1_TARGETED_FSYNC" "s1"
run_candidate "S2_SYNCFS" "s2"

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

if {item["strategy"] for item in results} != {
    "S1_TARGETED_FSYNC",
    "S2_SYNCFS",
}:
    raise SystemExit("A1-M5 candidate set is incomplete")

artifact = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m5-candidate-replay-v1",
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
    "candidate_protocol": {
        "S1_TARGETED_FSYNC": (
            "fsync every staging regular file and directory bottom-up, "
            "rename, fsync promoted snapshot parent, then activate Control DB"
        ),
        "S2_SYNCFS": (
            "syncfs staging filesystem, rename, fsync promoted snapshot "
            "parent, then activate Control DB"
        ),
        "old_authority_synced_before_baseline_mark": True,
        "fresh_process_seal_verifier_after_replay": True,
        "target_mark": "immediately after Control DB activation",
    },
    "results": results,
    "limitations": [
        (
            "M5 compares candidate ordering in the deterministic seal-aware "
            "A1 replay fixture; it is not a production runtime patch."
        ),
        (
            "M1 remains the EWD/CBD/RMD comparative performance evidence."
        ),
        (
            "Passing M5 does not by itself select a production barrier; "
            "boundary qualification and explicit policy review remain."
        ),
    ],
}

pathlib.Path(output).write_text(
    json.dumps(artifact, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)

print(pathlib.Path(output).read_text(encoding="utf-8"))
PY
