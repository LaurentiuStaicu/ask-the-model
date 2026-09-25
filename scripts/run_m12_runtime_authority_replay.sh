#!/usr/bin/env bash
set -euo pipefail

OUTPUT="${1:?output JSON path required}"
HELPER="${2:?M12 helper path required}"

WORK_ROOT="$(mktemp -d /tmp/atm-a1-m12-XXXXXX)"
RESULTS_DIR="$WORK_ROOT/results"
mkdir -p "$RESULTS_DIR" "$(dirname "$OUTPUT")"

cleanup() {
    set +e
    jobs -pr | xargs -r kill -9 >/dev/null 2>&1 || true
    rm -rf "$WORK_ROOT"
}
trap cleanup EXIT

test -x "$HELPER"

run_boundary() {
    local checkpoint="$1"
    local boundary="$2"
    local expected="$3"
    local work="$WORK_ROOT/$boundary"
    local root="$work/root"
    local child_pid=""

    mkdir -p "$work" "$root"

    "$HELPER" --initialize "$root"         >"$work/initialize.json"
    python3 -m json.tool "$work/initialize.json" >/dev/null

    local checkpoint_fifo="$work/checkpoint.pipe"
    local control_fifo="$work/control.pipe"
    mkfifo "$checkpoint_fifo" "$control_fifo"
    exec 3<>"$checkpoint_fifo"
    exec 4<>"$control_fifo"

    "$HELPER"         --run-boundary         "$root"         "$checkpoint"         3>&3 4<&4         >"$work/pipeline.json"         2>"$work/pipeline.stderr" &
    child_pid=$!

    local observed=""
    if ! IFS= read -r -t 60 -u 3 observed; then
        cat "$work/pipeline.stderr" >&2 || true
        kill -9 "$child_pid" >/dev/null 2>&1 || true
        wait "$child_pid" >/dev/null 2>&1 || true
        exec 3>&-
        exec 4>&-
        echo "M12 helper did not reach checkpoint $checkpoint" >&2
        return 1
    fi

    test "$observed" = "$checkpoint"

    kill -9 "$child_pid" >/dev/null 2>&1 || true
    wait "$child_pid" >/dev/null 2>&1 || true
    child_pid=""

    exec 3>&-
    exec 4>&-

    "$HELPER" --verify "$root"         >"$work/verify.json"
    python3 -m json.tool "$work/verify.json" >/dev/null

    python3 -         "$RESULTS_DIR/$boundary.json"         "$checkpoint"         "$boundary"         "$expected"         "$work/verify.json" <<'PY'
import json
import pathlib
import sys

output, checkpoint, boundary, expected, verify_path = sys.argv[1:]

with open(verify_path, encoding="utf-8") as handle:
    verification = json.load(handle)

record = {
    "checkpoint": checkpoint,
    "boundary": boundary,
    "expected_classification": expected,
    "verification": verification,
    "classification_match": (
        verification.get("classification") == expected
    ),
    "authority_target_satisfied": (
        verification.get("classification") == expected
        and verification.get("qualified") is True
    ),
}

if not record["authority_target_satisfied"]:
    raise SystemExit(
        f"{boundary}: authority classification mismatch: "
        f"{verification}"
    )

pathlib.Path(output).write_text(
    json.dumps(record, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY
}

boundaries=(
    "runtime_post_durable_ingest_pre_preindex_seal:post_durable_ingest_pre_preindex_seal:EMPTY_AUTHORITY_VALID"
    "runtime_post_preindex_seal_pre_index:post_preindex_seal_pre_index:EMPTY_AUTHORITY_VALID"
    "runtime_post_index_pre_postindex_seal:post_index_pre_postindex_seal:EMPTY_AUTHORITY_VALID"
    "runtime_post_postindex_seal_pre_state_capacity:post_postindex_seal_pre_state_capacity:EMPTY_AUTHORITY_VALID"
    "runtime_post_state_capacity_pre_authority:post_state_capacity_pre_authority:EMPTY_AUTHORITY_VALID"
    "runtime_after_authority:after_authority:NEW_AUTHORITY_VALID"
)

for record in "${boundaries[@]}"; do
    checkpoint="${record%%:*}"
    rest="${record#*:}"
    boundary="${rest%%:*}"
    expected="${rest##*:}"

    run_boundary         "$checkpoint"         "$boundary"         "$expected"
done

mismatch_root="$WORK_ROOT/seal-mismatch/root"
mkdir -p "$mismatch_root"
"$HELPER" --initialize "$mismatch_root"     >"$WORK_ROOT/seal-mismatch-initialize.json"
"$HELPER" --seal-mismatch "$mismatch_root"     >"$WORK_ROOT/seal-mismatch.json"
"$HELPER" --verify "$mismatch_root"     >"$WORK_ROOT/seal-mismatch-verify.json"

python3 -m json.tool "$WORK_ROOT/seal-mismatch.json" >/dev/null
python3 -m json.tool "$WORK_ROOT/seal-mismatch-verify.json" >/dev/null

python3 -     "$OUTPUT"     "$RESULTS_DIR"     "$WORK_ROOT/seal-mismatch.json"     "$WORK_ROOT/seal-mismatch-verify.json" <<'PY'
import json
import pathlib
import platform
import sys

output, results_dir, mismatch_path, mismatch_verify_path = sys.argv[1:]

results = []
for path in sorted(pathlib.Path(results_dir).glob("*.json")):
    with path.open(encoding="utf-8") as handle:
        results.append(json.load(handle))

with open(mismatch_path, encoding="utf-8") as handle:
    mismatch = json.load(handle)
with open(mismatch_verify_path, encoding="utf-8") as handle:
    mismatch_verification = json.load(handle)

expected_boundaries = {
    "post_durable_ingest_pre_preindex_seal",
    "post_preindex_seal_pre_index",
    "post_index_pre_postindex_seal",
    "post_postindex_seal_pre_state_capacity",
    "post_state_capacity_pre_authority",
    "after_authority",
}

if {item["boundary"] for item in results} != expected_boundaries:
    raise SystemExit("M12 boundary matrix is incomplete")
if len(results) != 6:
    raise SystemExit("M12 must contain exactly six interruption scenarios")
if not all(item["authority_target_satisfied"] for item in results):
    raise SystemExit("M12 authority invariant failed")
if mismatch.get("seal_mismatch_detected") is not True:
    raise SystemExit("M12 seal mismatch was not detected")
if mismatch.get("authority_published") is not False:
    raise SystemExit("M12 seal mismatch unexpectedly published authority")
if mismatch_verification.get("classification") != "EMPTY_AUTHORITY_VALID":
    raise SystemExit("M12 seal mismatch did not preserve empty authority")
if mismatch_verification.get("qualified") is not True:
    raise SystemExit("M12 seal mismatch verifier did not qualify empty authority")

artifact = {
    "schema_version": 1,
    "measurement_id": "atm-a1-m12-runtime-authority-replay-v1",
    "status": "measurement-only",
    "runtime_fault_qualification_complete": False,
    "kernel": {
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
    },
    "protocol": {
        "process_interruption_only": True,
        "physical_power_loss_claim": False,
        "network_download_excluded": True,
        "local_sequence": [
            "MUTATION_LEASE",
            "DURABLE_INGEST_S1_DEST_SOURCE",
            "PROMOTED_PRE_INDEX_SEAL_MATCHES_PRE_BARRIER_SEAL",
            "COORDINATED_RETRIEVAL_INDEX",
            "POST_INDEX_SEAL_AND_COUNTS_MATCH",
            "STATE_CAPACITY_PREFLIGHT",
            "GUARDED_CONTROL_DB_PUBLICATION",
        ],
        "boundary_order": [
            "post_durable_ingest_pre_preindex_seal",
            "post_preindex_seal_pre_index",
            "post_index_pre_postindex_seal",
            "post_postindex_seal_pre_state_capacity",
            "post_state_capacity_pre_authority",
            "after_authority",
        ],
    },
    "results": results,
    "seal_mismatch": {
        "execution": mismatch,
        "verification": mismatch_verification,
    },
    "summary": {
        "all_pre_authority_interruptions_empty_valid": all(
            item["verification"]["classification"]
            == "EMPTY_AUTHORITY_VALID"
            for item in results
            if item["boundary"] != "after_authority"
        ),
        "after_authority_new_valid": next(
            item["verification"]["classification"]
            == "NEW_AUTHORITY_VALID"
            and item["verification"]["qualified"] is True
            for item in results
            if item["boundary"] == "after_authority"
        ),
        "seal_mismatch_fail_closed": (
            mismatch.get("seal_mismatch_detected") is True
            and mismatch_verification.get("classification")
            == "EMPTY_AUTHORITY_VALID"
            and mismatch_verification.get("qualified") is True
        ),
    },
    "limitations": [
        (
            "M12 qualifies process-interruption authority sequencing in the "
            "local post-download pipeline. It is not a physical power-loss "
            "durability experiment; M10/M11b cover the selected filesystem "
            "durability barriers separately."
        ),
        (
            "HTTP download is excluded because it occurs before snapshot "
            "authority preparation and is not part of the new I1d2 durability "
            "publication sequence."
        ),
        (
            "Automatic orphan recovery remains unselected and is not exercised."
        ),
    ],
}

pathlib.Path(output).write_text(
    json.dumps(artifact, indent=2, sort_keys=True) + "\n",
    encoding="utf-8",
)
PY
