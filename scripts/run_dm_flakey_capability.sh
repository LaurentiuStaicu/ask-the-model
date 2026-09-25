#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m7-XXXXXX)"
DATA_IMAGE="$WORK_ROOT/data.img"
BASELINE_MOUNT="$WORK_ROOT/live"
VERIFY_MOUNT="$WORK_ROOT/verify"
MAPPER_NAME="atm-a1-m7-${GITHUB_RUN_ID:-$$}"
MAPPER_PATH="/dev/mapper/$MAPPER_NAME"
DATA_LOOP=""
MAPPER_CREATED=0
LIVE_MOUNTED=0
VERIFY_MOUNTED=0

cleanup() {
    set +e
    if [[ "$VERIFY_MOUNTED" -eq 1 ]]; then
        sudo umount "$VERIFY_MOUNT" >/dev/null 2>&1 || true
    fi
    if [[ "$LIVE_MOUNTED" -eq 1 ]]; then
        sudo umount "$BASELINE_MOUNT" >/dev/null 2>&1 || true
    fi
    if [[ "$MAPPER_CREATED" -eq 1 ]]; then
        sudo dmsetup remove --retry "$MAPPER_NAME" >/dev/null 2>&1 || true
    fi
    if [[ -n "$DATA_LOOP" ]]; then
        sudo losetup -d "$DATA_LOOP" >/dev/null 2>&1 || true
    fi
    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT

mkdir -p "$BASELINE_MOUNT" "$VERIFY_MOUNT" "$(dirname "$OUTPUT")"

sudo modprobe dm-flakey >/dev/null 2>&1 || true
TARGET_AVAILABLE=false
if sudo dmsetup targets | awk '{print $1}' | grep -Fxq "flakey"; then
    TARGET_AVAILABLE=true
fi

if [[ "$TARGET_AVAILABLE" != true ]]; then
    python3 - "$OUTPUT" <<'PY'
import json
import pathlib
import platform
import sys

output = sys.argv[1]
record = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m7-dm-flakey-capability-v1",
    "status": "unsupported",
    "production_durability_authorized": False,
    "kernel": {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
    },
    "checks": {
        "dm_flakey_target_available": False,
    },
    "scope": (
        "Capability probe only. The qualification runner does not expose "
        "the dm-flakey target, so application error-injection work must "
        "move to a different runner rather than weakening the experiment."
    ),
}
pathlib.Path(output).write_text(
    json.dumps(record, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
print(pathlib.Path(output).read_text(encoding="utf-8"))
PY
    exit 0
fi

truncate -s 64M "$DATA_IMAGE"
DATA_LOOP="$(sudo losetup --find --show "$DATA_IMAGE")"
SECTORS="$(sudo blockdev --getsz "$DATA_LOOP")"

sudo dmsetup create "$MAPPER_NAME"     --table "0 $SECTORS linear $DATA_LOOP 0"
MAPPER_CREATED=1

sudo mkfs.ext4 -F -m 0 "$MAPPER_PATH" >/dev/null
sudo mount "$MAPPER_PATH" "$BASELINE_MOUNT"
LIVE_MOUNTED=1
sudo chown "$(id -u):$(id -g)" "$BASELINE_MOUNT"

python3 - "$BASELINE_MOUNT" <<'PY'
import hashlib
import os
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
path = root / "baseline.bin"
payload = b"atm-a1-m7-dm-flakey-baseline\n" * 4096

fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
try:
    view = memoryview(payload)
    offset = 0
    while offset < len(view):
        offset += os.write(fd, view[offset:])
    os.fsync(fd)
finally:
    os.close(fd)

dir_fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY)
try:
    os.fsync(dir_fd)
finally:
    os.close(dir_fd)

print(hashlib.sha256(payload).hexdigest())
PY
BASELINE_SHA="$(sha256sum "$BASELINE_MOUNT/baseline.bin" | awk '{print $1}')"

sync -f "$BASELINE_MOUNT"

sudo dmsetup suspend "$MAPPER_NAME"
sudo dmsetup reload "$MAPPER_NAME"     --table "0 $SECTORS flakey $DATA_LOOP 0 0 600 1 error_writes"
sudo dmsetup resume "$MAPPER_NAME"

READ_SHA="$(sha256sum "$BASELINE_MOUNT/baseline.bin" | awk '{print $1}')"
READ_SURVIVED=false
if [[ "$READ_SHA" == "$BASELINE_SHA" ]]; then
    READ_SURVIVED=true
fi

set +e
ERROR_ERRNO="$(
python3 - "$BASELINE_MOUNT" <<'PY'
import errno
import os
import pathlib
import sys

root = pathlib.Path(sys.argv[1])
path = root / "must-fail.bin"

try:
    fd = os.open(
        path,
        os.O_WRONLY | os.O_CREAT | os.O_TRUNC,
        0o600,
    )
    try:
        os.write(fd, b"x" * 4096)
        os.fsync(fd)
    finally:
        os.close(fd)
except OSError as exc:
    print(exc.errno if exc.errno is not None else -1)
    raise SystemExit(0 if exc.errno == errno.EIO else 2)

print(0)
raise SystemExit(3)
PY
)"
ERROR_PROBE_RC=$?
set -e

ERROR_WRITES_TRIGGERED=false
if [[ "$ERROR_PROBE_RC" -eq 0 && "$ERROR_ERRNO" -eq 5 ]]; then
    ERROR_WRITES_TRIGGERED=true
fi

sudo dmsetup suspend "$MAPPER_NAME"
sudo dmsetup reload "$MAPPER_NAME"     --table "0 $SECTORS linear $DATA_LOOP 0"
sudo dmsetup resume "$MAPPER_NAME"

set +e
sudo umount "$BASELINE_MOUNT"
UMOUNT_RC=$?
set -e
if [[ "$UMOUNT_RC" -ne 0 ]]; then
    exit "$UMOUNT_RC"
fi
LIVE_MOUNTED=0

sudo dmsetup remove --retry "$MAPPER_NAME"
MAPPER_CREATED=0

set +e
sudo e2fsck -fy "$DATA_LOOP" >"$WORK_ROOT/e2fsck.txt" 2>&1
E2FSCK_RC=$?
set -e
if [[ "$E2FSCK_RC" -gt 2 ]]; then
    cat "$WORK_ROOT/e2fsck.txt" >&2
    exit "$E2FSCK_RC"
fi

sudo mount -o ro "$DATA_LOOP" "$VERIFY_MOUNT"
VERIFY_MOUNTED=1
RESTORED_SHA="$(sudo sha256sum "$VERIFY_MOUNT/baseline.bin" | awk '{print $1}')"
BASELINE_PRESERVED=false
if [[ "$RESTORED_SHA" == "$BASELINE_SHA" ]]; then
    BASELINE_PRESERVED=true
fi
sudo umount "$VERIFY_MOUNT"
VERIFY_MOUNTED=0

python3 -     "$OUTPUT"     "$TARGET_AVAILABLE"     "$READ_SURVIVED"     "$ERROR_WRITES_TRIGGERED"     "$ERROR_ERRNO"     "$E2FSCK_RC"     "$BASELINE_SHA"     "$RESTORED_SHA"     "$BASELINE_PRESERVED" <<'PY'
import json
import pathlib
import platform
import sys

(
    output,
    target_available,
    read_survived,
    error_writes_triggered,
    error_errno,
    e2fsck_rc,
    baseline_sha,
    restored_sha,
    baseline_preserved,
) = sys.argv[1:]

checks = {
    "dm_flakey_target_available": target_available == "true",
    "mounted_linear_mapping_reloaded_to_flakey": True,
    "reads_survive_error_writes_mode": read_survived == "true",
    "write_fsync_returns_eio": error_writes_triggered == "true",
    "mapping_restored_to_linear": True,
    "baseline_content_preserved_after_recovery": (
        baseline_preserved == "true"
    ),
}

record = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m7-dm-flakey-capability-v1",
    "status": "supported" if all(checks.values()) else "failed",
    "production_durability_authorized": False,
    "kernel": {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
    },
    "method": {
        "initial_target": "linear",
        "injected_target": "flakey",
        "flakey_up_interval_seconds": 0,
        "flakey_down_interval_seconds": 600,
        "feature": "error_writes",
        "reload_while_ext4_mounted": True,
    },
    "checks": checks,
    "observations": {
        "write_probe_errno": int(error_errno),
        "e2fsck_exit_code": int(e2fsck_rc),
        "baseline_sha256": baseline_sha,
        "restored_sha256": restored_sha,
    },
    "scope": (
        "Capability probe only. It proves whether the reviewed runner can "
        "reload a mounted disposable mapping into dm-flakey error_writes, "
        "retain reads, surface EIO on write+fsync, restore the mapping and "
        "recover the original baseline. It does not exercise AtM candidate "
        "barriers and authorizes no production durability behavior."
    ),
}

pathlib.Path(output).write_text(
    json.dumps(record, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
print(pathlib.Path(output).read_text(encoding="utf-8"))

if record["status"] == "failed":
    raise SystemExit(1)
PY
