#!/usr/bin/env python3

import json
from pathlib import Path

PATH = Path("benchmarks/presentation-v1/evidence.json")

EXPECTED_SCHEMA = "atm-presentation-performance-evidence/1"
EXPECTED_SHA = "20a484b095cd9e565aeb63c97015c083f6c61e57"
EXPECTED_RUN = 36139661109
EXPECTED_SIZES = [1024, 4096, 16384, 65536, 262144, 1048576]
EXPECTED_ATTEMPTS = {
    1: (10866077220, "sha256:6c4cefd16dff0f0fc2f43fc71871b345887cad366bf8b9d28cc9b32d7e1806da"),
    2: (10866117386, "sha256:0a03fab1d3b4160d991926fee5a2f62414d106459b303da321f9bdc791cefe24"),
    3: (10866287374, "sha256:9bd2a2f89adde162cb9f968de010948771a62d8f440fbcb19f7e51b5b3f903a9"),
}


def fail(message: str) -> None:
    raise SystemExit(f"presentation performance evidence invalid: {message}")


def main() -> None:
    data = json.loads(PATH.read_text(encoding="utf-8"))

    if data.get("schema") != EXPECTED_SCHEMA:
        fail("schema")
    if data.get("status") != "frozen-measurement":
        fail("status")
    if data.get("scope") != "qualification-only":
        fail("scope")

    source = data.get("source", {})
    if source.get("pull_request") != 276:
        fail("pull request")
    if source.get("tested_head_sha") != EXPECTED_SHA:
        fail("tested head SHA")
    if source.get("workflow_run_id") != EXPECTED_RUN:
        fail("workflow run")
    if source.get("runner") != "ubuntu-24.04":
        fail("runner")

    contract = data.get("measurement_contract", {})
    if contract.get("input_sizes_bytes") != EXPECTED_SIZES:
        fail("input sizes")
    iterations = contract.get("iterations_by_size", {})

    attempts = data.get("attempts")
    if not isinstance(attempts, list) or len(attempts) != 3:
        fail("attempt count")

    one_mib_medians = []
    one_mib_p95s = []

    for attempt in attempts:
        attempt_no = attempt.get("attempt")
        if attempt_no not in EXPECTED_ATTEMPTS:
            fail("attempt number")

        artifact_id, artifact_digest = EXPECTED_ATTEMPTS[attempt_no]
        if attempt.get("artifact_id") != artifact_id:
            fail(f"artifact id for attempt {attempt_no}")
        if attempt.get("artifact_digest") != artifact_digest:
            fail(f"artifact digest for attempt {attempt_no}")

        measurements = attempt.get("measurements")
        if not isinstance(measurements, list) or len(measurements) != len(EXPECTED_SIZES):
            fail(f"measurement count for attempt {attempt_no}")

        observed_sizes = [row.get("input_bytes") for row in measurements]
        if observed_sizes != EXPECTED_SIZES:
            fail(f"measurement sizes for attempt {attempt_no}")

        for row in measurements:
            size = row["input_bytes"]
            if row.get("iterations") != iterations.get(str(size)):
                fail(f"iterations for {size} bytes")
            for field in ("output_bytes", "median_us", "p95_us", "max_us"):
                value = row.get(field)
                if not isinstance(value, int) or value <= 0:
                    fail(f"{field} for {size} bytes")
            if not (row["median_us"] <= row["p95_us"] <= row["max_us"]):
                fail(f"latency ordering for {size} bytes")

        one_mib = measurements[-1]
        one_mib_medians.append(one_mib["median_us"])
        one_mib_p95s.append(one_mib["p95_us"])

    review = data.get("derived_review", {})
    if review.get("observed_scaling") != "approximately-linear-over-measured-range":
        fail("scaling review")
    if review.get("one_mib_median_us_min") != min(one_mib_medians):
        fail("1 MiB median minimum")
    if review.get("one_mib_median_us_max") != max(one_mib_medians):
        fail("1 MiB median maximum")
    if review.get("one_mib_p95_us_max") != max(one_mib_p95s):
        fail("1 MiB p95 maximum")

    for field in (
        "production_latency_threshold_selected",
        "production_input_budget_selected",
        "coverage_guided_fuzzing_required_for_runtime",
    ):
        if review.get(field) is not False:
            fail(field)

    print("presentation performance evidence: OK")


if __name__ == "__main__":
    main()
