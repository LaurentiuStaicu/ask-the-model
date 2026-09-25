#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"
UPSTREAM_DIR="${2:?upstream log-writes directory required}"
UPSTREAM_COMMIT="${3:?upstream commit required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m3-XXXXXX)"
DATA_IMAGE="$WORK_ROOT/data.img"
LOG_IMAGE="$WORK_ROOT/log.img"
REPLAY_IMAGE="$WORK_ROOT/replay.img"
ORIGINAL_MOUNT="$WORK_ROOT/original-mnt"
REPLAY_MOUNT="$WORK_ROOT/replay-mnt"
MAPPER_NAME="atm-a1-m3-${GITHUB_RUN_ID:-$$}"
MAPPER_PATH="/dev/mapper/$MAPPER_NAME"

DATA_LOOP=""
LOG_LOOP=""
REPLAY_LOOP=""
MAPPER_CREATED=0
ORIGINAL_MOUNTED=0
REPLAY_MOUNTED=0

cleanup() {
    set +e

    if [[ "$REPLAY_MOUNTED" -eq 1 ]]; then
        sudo umount "$REPLAY_MOUNT"
        REPLAY_MOUNTED=0
    fi

    if [[ "$ORIGINAL_MOUNTED" -eq 1 ]]; then
        sudo umount "$ORIGINAL_MOUNT"
        ORIGINAL_MOUNTED=0
    fi

    if [[ "$MAPPER_CREATED" -eq 1 ]]; then
        sudo dmsetup remove --retry "$MAPPER_NAME"
        MAPPER_CREATED=0
    fi

    for loop in "$REPLAY_LOOP" "$LOG_LOOP" "$DATA_LOOP"; do
        if [[ -n "$loop" ]]; then
            sudo losetup -d "$loop"
        fi
    done

    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT

mkdir -p     "$(dirname "$OUTPUT")"     "$ORIGINAL_MOUNT"     "$REPLAY_MOUNT"

test -x "$UPSTREAM_DIR/replay-log"

sudo modprobe dm-log-writes >/dev/null 2>&1 || true
sudo dmsetup targets | awk '{print $1}' | grep -Fxq "log-writes"

truncate -s 64M "$DATA_IMAGE"
truncate -s 256M "$LOG_IMAGE"
truncate -s 64M "$REPLAY_IMAGE"

DATA_LOOP="$(sudo losetup --find --show "$DATA_IMAGE")"
LOG_LOOP="$(sudo losetup --find --show "$LOG_IMAGE")"
REPLAY_LOOP="$(sudo losetup --find --show "$REPLAY_IMAGE")"

SECTORS="$(sudo blockdev --getsz "$DATA_LOOP")"
TABLE="0 $SECTORS log-writes $DATA_LOOP $LOG_LOOP"

sudo dmsetup create "$MAPPER_NAME" --table "$TABLE"
MAPPER_CREATED=1

sudo mkfs.ext4 -F -m 0 "$MAPPER_PATH" >/dev/null
sudo dmsetup message "$MAPPER_NAME" 0 mark mkfs

sudo mount "$MAPPER_PATH" "$ORIGINAL_MOUNT"
ORIGINAL_MOUNTED=1

sudo python3 - "$ORIGINAL_MOUNT" <<'PY'
import hashlib
import os
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
path = root / "atm-a1-replay.txt"

payload = bytearray()
for index in range(16384):
    payload.extend(
        f"atm-a1-m3-replay-line-{index:05d}\n".encode("ascii")
    )

fd = os.open(
    path,
    os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
    0o600,
)
try:
    view = memoryview(payload)
    written = 0
    while written < len(view):
        written += os.write(fd, view[written:])
    os.fsync(fd)
finally:
    os.close(fd)

dir_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
try:
    os.fsync(dir_fd)
finally:
    os.close(dir_fd)

digest = hashlib.sha256(payload).hexdigest()
(root / "expected.sha256").write_text(
    digest + "\n",
    encoding="ascii",
)
fd = os.open(
    root / "expected.sha256",
    os.O_RDONLY,
)
try:
    os.fsync(fd)
finally:
    os.close(fd)

dir_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
try:
    os.fsync(dir_fd)
finally:
    os.close(dir_fd)
PY

EXPECTED_SHA="$(sudo cat "$ORIGINAL_MOUNT/expected.sha256")"
ORIGINAL_SHA="$(sudo sha256sum "$ORIGINAL_MOUNT/atm-a1-replay.txt" | awk '{print $1}')"
test "$ORIGINAL_SHA" = "$EXPECTED_SHA"

sudo dmsetup message "$MAPPER_NAME" 0 mark fsync
sudo sync

sudo umount "$ORIGINAL_MOUNT"
ORIGINAL_MOUNTED=0
sudo dmsetup remove --retry "$MAPPER_NAME"
MAPPER_CREATED=0

sudo "$UPSTREAM_DIR/replay-log"     --log "$LOG_LOOP"     --find     --end-mark mkfs     >"$WORK_ROOT/mkfs-entry.txt"

sudo "$UPSTREAM_DIR/replay-log"     --log "$LOG_LOOP"     --find     --end-mark fsync     >"$WORK_ROOT/fsync-entry.txt"

sudo "$UPSTREAM_DIR/replay-log"     --log "$LOG_LOOP"     --replay "$REPLAY_LOOP"     --end-mark fsync

set +e
sudo e2fsck -fy "$REPLAY_LOOP"     >"$WORK_ROOT/e2fsck.txt" 2>&1
E2FSCK_RC=$?
set -e

if [[ "$E2FSCK_RC" -gt 2 ]]; then
    cat "$WORK_ROOT/e2fsck.txt" >&2
    exit "$E2FSCK_RC"
fi

sudo mount -o ro "$REPLAY_LOOP" "$REPLAY_MOUNT"
REPLAY_MOUNTED=1

REPLAY_SHA="$(sudo sha256sum "$REPLAY_MOUNT/atm-a1-replay.txt" | awk '{print $1}')"
RECORDED_EXPECTED_SHA="$(sudo cat "$REPLAY_MOUNT/expected.sha256")"

test "$REPLAY_SHA" = "$EXPECTED_SHA"
test "$RECORDED_EXPECTED_SHA" = "$EXPECTED_SHA"

MKFS_ENTRY="$(tr -d '[:space:]' <"$WORK_ROOT/mkfs-entry.txt")"
FSYNC_ENTRY="$(tr -d '[:space:]' <"$WORK_ROOT/fsync-entry.txt")"

python3 - "$OUTPUT"     "$UPSTREAM_COMMIT"     "$EXPECTED_SHA"     "$REPLAY_SHA"     "$MKFS_ENTRY"     "$FSYNC_ENTRY"     "$E2FSCK_RC" <<'PY'
import json
import platform
import sys

(
    output,
    upstream_commit,
    expected_sha,
    replay_sha,
    mkfs_entry,
    fsync_entry,
    e2fsck_rc,
) = sys.argv[1:]

record = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m3-block-replay-smoke-v1",
    "status": "qualified",
    "production_durability_authorized": False,
    "kernel": {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
    },
    "upstream": {
        "repository": "josefbacik/log-writes",
        "commit": upstream_commit,
    },
    "marks": {
        "mkfs_entry": int(mkfs_entry),
        "fsync_entry": int(fsync_entry),
    },
    "replay": {
        "end_mark": "fsync",
        "e2fsck_exit_code": int(e2fsck_rc),
        "expected_sha256": expected_sha,
        "replayed_sha256": replay_sha,
        "content_match": expected_sha == replay_sha,
    },
    "scope": (
        "Block-replay semantics smoke only. This proves the qualification "
        "runner can reconstruct a synchronized ext4 file through a named "
        "dm-log-writes mark; it does not yet exercise Ask the Model "
        "snapshot promotion or the repository restart oracle."
    ),
}

with open(output, "w", encoding="utf-8") as handle:
    json.dump(record, handle, indent=2, sort_keys=True)
    handle.write("\n")
PY

python3 -m json.tool "$OUTPUT"
