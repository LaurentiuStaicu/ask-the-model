#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"
UPSTREAM_DIR="${2:?upstream log-writes directory required}"
UPSTREAM_COMMIT="${3:?upstream commit required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m2-XXXXXX)"
DATA_IMAGE="$WORK_ROOT/data.img"
LOG_IMAGE="$WORK_ROOT/log.img"
MOUNT_DIR="$WORK_ROOT/mnt"
MAPPER_NAME="atm-a1-m2-${GITHUB_RUN_ID:-$$}"
MAPPER_PATH="/dev/mapper/$MAPPER_NAME"

DATA_LOOP=""
LOG_LOOP=""
MAPPER_CREATED=0
MOUNTED=0

write_result() {
    local status="$1"
    local reason="$2"
    local target_available="$3"
    local loop_available="$4"
    local mapping_created="$5"
    local ext4_mounted="$6"
    local replay_log_built="$7"
    local mark_found="$8"

    python3 - "$OUTPUT" "$status" "$reason" \
        "$target_available" "$loop_available" "$mapping_created" \
        "$ext4_mounted" "$replay_log_built" "$mark_found" \
        "$UPSTREAM_COMMIT" <<'PY'
import json
import os
import platform
import sys

(
    output,
    status,
    reason,
    target_available,
    loop_available,
    mapping_created,
    ext4_mounted,
    replay_log_built,
    mark_found,
    upstream_commit,
) = sys.argv[1:]

def as_bool(value):
    return value == "true"

record = {
    "schema_version": 1,
    "probe_id": "atm-a1-m2-dm-log-writes-capability-v1",
    "status": status,
    "reason": reason,
    "production_durability_authorized": False,
    "kernel": {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
    },
    "runner": {
        "github_actions": os.environ.get("GITHUB_ACTIONS") == "true",
        "runner_os": os.environ.get("RUNNER_OS"),
        "runner_arch": os.environ.get("RUNNER_ARCH"),
    },
    "upstream": {
        "repository": "josefbacik/log-writes",
        "commit": upstream_commit,
    },
    "checks": {
        "dm_log_writes_target_available": as_bool(target_available),
        "loop_device_available": as_bool(loop_available),
        "mapping_created": as_bool(mapping_created),
        "ext4_mounted": as_bool(ext4_mounted),
        "replay_log_built": as_bool(replay_log_built),
        "fsync_mark_found": as_bool(mark_found),
    },
    "scope": (
        "Capability probe only: no replay-to-crash-boundary result and "
        "no physical-power-loss durability claim."
    ),
}

with open(output, "w", encoding="utf-8") as handle:
    json.dump(record, handle, indent=2, sort_keys=True)
    handle.write("\n")
PY
}

cleanup() {
    set +e

    if [[ "$MOUNTED" -eq 1 ]]; then
        sudo umount "$MOUNT_DIR"
        MOUNTED=0
    fi

    if [[ "$MAPPER_CREATED" -eq 1 ]]; then
        sudo dmsetup remove --retry "$MAPPER_NAME"
        MAPPER_CREATED=0
    fi

    if [[ -n "$DATA_LOOP" ]]; then
        sudo losetup -d "$DATA_LOOP"
        DATA_LOOP=""
    fi

    if [[ -n "$LOG_LOOP" ]]; then
        sudo losetup -d "$LOG_LOOP"
        LOG_LOOP=""
    fi

    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT

mkdir -p "$(dirname "$OUTPUT")" "$MOUNT_DIR"

if [[ ! -x "$UPSTREAM_DIR/replay-log" ]]; then
    write_result         "unsupported"         "Pinned replay-log userspace binary was not built."         false false false false false false
    exit 0
fi
REPLAY_LOG_BUILT=true

sudo modprobe dm-log-writes >/dev/null 2>&1 || true

if ! sudo dmsetup targets | awk '{print $1}' | grep -Fxq "log-writes"; then
    write_result         "unsupported"         "Kernel/device-mapper log-writes target is unavailable on this runner."         false false false false "$REPLAY_LOG_BUILT" false
    exit 0
fi
TARGET_AVAILABLE=true

truncate -s 64M "$DATA_IMAGE"
truncate -s 128M "$LOG_IMAGE"

if ! DATA_LOOP="$(sudo losetup --find --show "$DATA_IMAGE")"; then
    write_result         "unsupported"         "Runner could not allocate the disposable data loop device."         "$TARGET_AVAILABLE" false false false "$REPLAY_LOG_BUILT" false
    exit 0
fi

if ! LOG_LOOP="$(sudo losetup --find --show "$LOG_IMAGE")"; then
    write_result         "unsupported"         "Runner could not allocate the disposable log loop device."         "$TARGET_AVAILABLE" false false false "$REPLAY_LOG_BUILT" false
    exit 0
fi
LOOP_AVAILABLE=true

SECTORS="$(sudo blockdev --getsz "$DATA_LOOP")"
TABLE="0 $SECTORS log-writes $DATA_LOOP $LOG_LOOP"

if ! sudo dmsetup create "$MAPPER_NAME" --table "$TABLE"; then
    write_result         "unsupported"         "Runner exposed log-writes but could not create a disposable mapping."         "$TARGET_AVAILABLE" "$LOOP_AVAILABLE" false false "$REPLAY_LOG_BUILT" false
    exit 0
fi
MAPPER_CREATED=1

sudo mkfs.ext4 -F -m 0 "$MAPPER_PATH" >/dev/null
sudo dmsetup message "$MAPPER_NAME" 0 mark mkfs

if ! sudo mount "$MAPPER_PATH" "$MOUNT_DIR"; then
    write_result         "unsupported"         "Disposable log-writes mapping could not be mounted as ext4."         "$TARGET_AVAILABLE" "$LOOP_AVAILABLE" true false "$REPLAY_LOG_BUILT" false
    exit 0
fi
MOUNTED=1

sudo python3 - "$MOUNT_DIR" <<'PY'
import os
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
path = root / "atm-a1-probe.txt"
payload = (b"atm-a1-m2-capability\n" * 4096)

fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
try:
    os.write(fd, payload)
    os.fsync(fd)
finally:
    os.close(fd)

dir_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
try:
    os.fsync(dir_fd)
finally:
    os.close(dir_fd)
PY

sudo dmsetup message "$MAPPER_NAME" 0 mark fsync
sudo sync

sudo umount "$MOUNT_DIR"
MOUNTED=0
sudo dmsetup remove --retry "$MAPPER_NAME"
MAPPER_CREATED=0

if ! sudo "$UPSTREAM_DIR/replay-log"         --log "$LOG_LOOP"         --find         --end-mark fsync         >"$WORK_ROOT/replay-find.txt" 2>&1; then
    cat "$WORK_ROOT/replay-find.txt" >&2 || true
    write_result         "probe-failed"         "replay-log could not locate the fsync mark in the dm-log-writes log."         "$TARGET_AVAILABLE" "$LOOP_AVAILABLE" true true "$REPLAY_LOG_BUILT" false
    exit 1
fi

cat "$WORK_ROOT/replay-find.txt"

write_result     "supported"     "Runner supports the minimum dm-log-writes + replay-log mark workflow."     "$TARGET_AVAILABLE" "$LOOP_AVAILABLE" true true "$REPLAY_LOG_BUILT" true

python3 -m json.tool "$OUTPUT"
