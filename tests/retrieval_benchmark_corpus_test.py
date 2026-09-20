#!/usr/bin/env python3
"""Validate the frozen R5 retrieval benchmark corpus contract."""

from __future__ import annotations

import json
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

from evaluate_retrieval_benchmark import validate_benchmark  # noqa: E402

BENCHMARK_PATH = ROOT / "benchmarks" / "retrieval-v1" / "benchmark.json"

EXPECTED_CORPUS = {
    "ewd": ("0.1.0", "795a6b8e42e2a19497f686a89e7c3a6056e6930c"),
    "cbd": ("0.1.0", "e6e3b3077b7d78e3f37d963e0734e9084e8a62ab"),
    "rmd": ("0.1.0", "a8b5ff89a951d3194fdedb59771bc79937fe7bc7"),
}

REQUIRED_TOPIC_TYPES = {
    "exact_entity_lookup",
    "current_status_boundary",
    "structure_mechanism",
    "evidence_provenance",
    "numeric_tabular",
    "negative_limitation",
    "unsupported_premise",
    "cross_repository_comparison",
    "multi_turn_follow_up",
}

REQUIRED_LANGUAGES = {"en", "ro", "mixed"}


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    benchmark = json.loads(BENCHMARK_PATH.read_text(encoding="utf-8"))
    validate_benchmark(benchmark)

    corpus = {
        item["repository_id"]: (
            item["repository_version"],
            item["snapshot_sha"],
        )
        for item in benchmark["corpus"]
    }
    require(
        corpus == EXPECTED_CORPUS,
        "The frozen R5 corpus must remain pinned to the reviewed EWD/CBD/RMD snapshots.",
    )

    topics = benchmark["topics"]
    topic_types = {topic["topic_type"] for topic in topics}
    languages = {topic["language"] for topic in topics}

    require(
        REQUIRED_TOPIC_TYPES <= topic_types,
        "The benchmark no longer covers every required R5 topic type.",
    )
    require(
        REQUIRED_LANGUAGES <= languages,
        "The benchmark must retain English, Romanian and mixed-language coverage.",
    )
    require(
        any(topic["expect_unsupported"] for topic in topics),
        "The benchmark needs at least one explicit unsupported-premise topic.",
    )
    require(
        any(
            topic["expected_outcome"] == "needs_clarification"
            for topic in topics
        ),
        "The benchmark needs at least one explicit clarification outcome.",
    )

    need_languages: dict[str, set[str]] = defaultdict(set)
    conversations: dict[str, list[dict]] = defaultdict(list)

    for topic in topics:
        need_languages[topic["information_need_id"]].add(topic["language"])

        conversation_id = topic.get("conversation_id")
        if conversation_id is not None:
            conversations[conversation_id].append(topic)

        for judgment in topic["judgments"]:
            prefix = judgment["repository_id"] + ":"
            require(
                judgment["logical_source_id"].startswith(prefix),
                f"{topic['topic_id']}: qrel logical ID has the wrong repository prefix.",
            )

        expected_exact = topic.get("expected_exact")
        if expected_exact is not None:
            target = (
                expected_exact["repository_id"],
                expected_exact["logical_source_id"],
            )
            judged = {
                (judgment["repository_id"], judgment["logical_source_id"])
                for judgment in topic["judgments"]
                if judgment["grade"] == 3 and judgment["required"]
            }
            require(
                target in judged,
                f"{topic['topic_id']}: exact target must also be grade-3 required evidence.",
            )

    paired_needs = sum(
        1
        for seen_languages in need_languages.values()
        if {"en", "ro"} <= seen_languages
    )
    require(
        paired_needs >= 10,
        "The benchmark must retain enough paired Romanian/English information needs for the language-gap metric.",
    )

    require(
        len(conversations) >= 3,
        "The benchmark must retain several deterministic multi-turn conversations.",
    )

    for conversation_id, turns in conversations.items():
        turns.sort(key=lambda topic: topic["turn_index"])
        indices = [topic["turn_index"] for topic in turns]
        require(
            indices == list(range(1, len(turns) + 1)),
            f"{conversation_id}: turn indices must be contiguous from 1.",
        )

        splits = {topic["split"] for topic in turns}
        scopes = {
            tuple(topic["active_repositories"])
            for topic in turns
        }
        require(
            len(splits) == 1,
            f"{conversation_id}: all turns must remain in one benchmark split.",
        )
        require(
            len(scopes) == 1,
            f"{conversation_id}: the frozen active repository scope changed across turns.",
        )

    print(
        "PASS retrieval benchmark corpus:",
        f"{len(topics)} topics,",
        f"{paired_needs} paired RO/EN needs,",
        f"{len(conversations)} conversations.",
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
