#!/usr/bin/env python3

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
TRAINING_PATH = ROOT / "benchmarks" / "capacity-v2" / "corpus.json"
HOLDOUT_PATH = ROOT / "benchmarks" / "capacity-v3" / "corpus.json"

SHA40 = re.compile(r"^[0-9a-f]{40}$")


def fail(message: str) -> None:
    raise SystemExit(f"capacity-v3 corpus validation failed: {message}")


def load(path: pathlib.Path):
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def flatten(corpus, expected_count_per_repo):
    by_repo = {}
    all_keys = set()

    repositories = corpus.get("repositories")
    if not isinstance(repositories, list):
        fail("repositories must be a list")

    for repository in repositories:
        rid = repository.get("repository_id")
        samples = repository.get("samples")

        if rid not in {"ewd", "cbd", "rmd"}:
            fail(f"unexpected repository_id: {rid!r}")
        if rid in by_repo:
            fail(f"duplicate repository_id: {rid}")
        if not isinstance(samples, list):
            fail(f"{rid}.samples must be a list")
        if len(samples) != expected_count_per_repo:
            fail(
                f"{rid} must contain exactly "
                f"{expected_count_per_repo} samples"
            )

        labels = set()
        shas = set()

        for sample in samples:
            label = sample.get("label")
            sha = sample.get("sha")

            if not isinstance(label, str) or not label:
                fail(f"{rid} has invalid sample label")
            if label in labels:
                fail(f"{rid} has duplicate label: {label}")
            labels.add(label)

            if not isinstance(sha, str) or SHA40.fullmatch(sha) is None:
                fail(f"{rid}/{label} has invalid SHA")
            if sha in shas:
                fail(f"{rid} has duplicate SHA: {sha}")
            shas.add(sha)

            key = (rid, sha)
            if key in all_keys:
                fail(f"duplicate corpus key: {key}")
            all_keys.add(key)

        by_repo[rid] = {
            "labels": labels,
            "shas": shas,
        }

    if set(by_repo) != {"ewd", "cbd", "rmd"}:
        fail("corpus must contain exactly EWD/CBD/RMD")

    return by_repo, all_keys


def main() -> int:
    training = load(TRAINING_PATH)
    holdout = load(HOLDOUT_PATH)

    if holdout.get("schema_version") != 1:
        fail("holdout schema_version must remain 1")

    if holdout.get("training_corpus") != (
        "benchmarks/capacity-v2/corpus.json"
    ):
        fail("holdout must reference the frozen M4 training corpus")

    policy = holdout.get("selection_policy")
    if not isinstance(policy, dict):
        fail("selection_policy is missing")

    if policy.get("holdout_commit_ranks") != [4, 12, 20, 40]:
        fail("holdout rank policy drifted")

    if policy.get("no_refit") is not True:
        fail("M5 must remain no-refit")

    if policy.get("overlap_forbidden") is not True:
        fail("training/holdout overlap must remain forbidden")

    training_by_repo, training_keys = flatten(training, 6)
    holdout_by_repo, holdout_keys = flatten(holdout, 4)

    overlap = training_keys & holdout_keys
    if overlap:
        fail(f"training/holdout overlap: {sorted(overlap)}")

    expected_labels = {
        "holdout-r4",
        "holdout-r12",
        "holdout-r20",
        "holdout-r40",
    }

    for rid, record in holdout_by_repo.items():
        if record["labels"] != expected_labels:
            fail(f"{rid} holdout labels drifted")

        if record["shas"] & training_by_repo[rid]["shas"]:
            fail(f"{rid} holdout SHA overlaps training")

    print(
        "capacity-v3 corpus validation passed: "
        "18 training + 12 disjoint no-refit holdouts"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
