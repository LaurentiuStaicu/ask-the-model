#!/usr/bin/env python3

import json
import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
CORPUS_PATH = ROOT / "benchmarks" / "capacity-v2" / "corpus.json"
R5_PATH = ROOT / "benchmarks" / "retrieval-v1" / "benchmark.json"

SHA40 = re.compile(r"^[0-9a-f]{40}$")


def fail(message: str) -> None:
    raise SystemExit(
        f"capacity-v2 corpus validation failed: {message}"
    )


def load(path: pathlib.Path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def main() -> int:
    corpus = load(CORPUS_PATH)
    r5 = load(R5_PATH)

    if corpus.get("schema_version") != 1:
        fail("schema_version must remain 1")

    if corpus.get("measurement_id") != (
        "atm-c0-m4-historical-amplification-v1"
    ):
        fail("measurement_id drifted")

    repositories = corpus.get("repositories")
    if not isinstance(repositories, list) or len(repositories) != 3:
        fail("corpus must contain exactly 3 repositories")

    pinned = {
        item["repository_id"]: item["snapshot_sha"]
        for item in r5["corpus"]
    }

    expected_ids = {"ewd", "cbd", "rmd"}
    observed_ids = set()
    global_shas = set()

    for repository in repositories:
        rid = repository.get("repository_id")
        if rid not in expected_ids:
            fail(f"unexpected repository_id: {rid!r}")
        if rid in observed_ids:
            fail(f"duplicate repository_id: {rid}")
        observed_ids.add(rid)

        samples = repository.get("samples")
        if not isinstance(samples, list) or len(samples) != 6:
            fail(f"{rid} must contain exactly 6 samples")

        labels = set()
        repo_shas = set()
        pinned_count = 0

        for sample in samples:
            label = sample.get("label")
            sha = sample.get("sha")

            if not isinstance(label, str) or not label:
                fail(f"{rid} contains an invalid sample label")
            if label in labels:
                fail(f"{rid} duplicate sample label: {label}")
            labels.add(label)

            if not isinstance(sha, str) or SHA40.fullmatch(sha) is None:
                fail(f"{rid}/{label} SHA is not lowercase 40-hex")
            if sha in repo_shas:
                fail(f"{rid} duplicate SHA: {sha}")
            repo_shas.add(sha)

            if sha in global_shas:
                fail(f"SHA reused across repository records: {sha}")
            global_shas.add(sha)

            if label == "pinned-m1":
                pinned_count += 1
                if sha != pinned[rid]:
                    fail(
                        f"{rid} pinned-m1 SHA does not match retrieval-v1"
                    )

        if pinned_count != 1:
            fail(f"{rid} must contain exactly one pinned-m1 sample")

        expected_labels = {
            "recent-r1",
            "recent-r8",
            "recent-r15",
            "recent-r22",
            "recent-r30",
            "pinned-m1",
        }
        if labels != expected_labels:
            fail(f"{rid} sample labels drifted: {sorted(labels)}")

    if observed_ids != expected_ids:
        fail("repository set must remain exactly EWD/CBD/RMD")

    if len(global_shas) != 18:
        fail("corpus must freeze exactly 18 unique SHAs")

    policy = corpus.get("selection_policy")
    if not isinstance(policy, dict):
        fail("selection_policy is missing")
    if policy.get("recent_commit_ranks") != [1, 8, 15, 22, 30]:
        fail("stratified recent commit ranks drifted")
    if policy.get("pinned_baseline_included") is not True:
        fail("pinned baseline inclusion must remain explicit")

    expected_temporal_order = [
        "pinned-m1",
        "recent-r30",
        "recent-r22",
        "recent-r15",
        "recent-r8",
        "recent-r1",
    ]
    if policy.get("temporal_order_labels") != expected_temporal_order:
        fail("verified temporal order drifted")
    if policy.get("ancestry_verified") is not True:
        fail("temporal ancestry must remain explicitly verified")
    ancestry_contract = str(policy.get("ancestry_contract", "")).lower()
    if "ancestor" not in ancestry_contract or "behind_by=0" not in ancestry_contract:
        fail("temporal ancestry contract is incomplete")

    print(
        "capacity-v2 corpus validation passed: "
        "18 exact SHAs + pinned R5 anchors"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
