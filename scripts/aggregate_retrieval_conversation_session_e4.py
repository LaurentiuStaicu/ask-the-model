#!/usr/bin/env python3
"""Validate and aggregate paired E4 conversation-scoped SQLite session runs."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import re
import statistics
from pathlib import Path

RUNNERS = 10
REPOSITORIES = {"ewd", "cbd", "rmd"}
SESSION_RE = re.compile(
    r"^ATM_E4_SESSION repository=(ewd|cbd|rmd) "
    r"event=(turn_open|turn_open_failed|open|open_failed|reuse|invalidate) "
    r"elapsed_us=(\d+)$",
    re.MULTILINE,
)
CACHE_CLOSE_RE = re.compile(
    r"^ATM_E4_SESSION_CACHE_CLOSE sessions=(\d+) opens=(\d+) "
    r"reuses=(\d+) invalidations=(\d+) elapsed_us=(\d+)$",
    re.MULTILINE,
)
CONVERSATION_TEARDOWN_RE = re.compile(
    r"^ATM_E4_CONVERSATION_TEARDOWN caches=(\d+) elapsed_us=(\d+)$",
    re.MULTILINE,
)

INDEX_RE = re.compile(
    r"^ATM_E3_INDEX repository=(ewd|cbd|rmd) "
    r"ensure=(rebuilt|reused) elapsed_us=(\d+)$",
    re.MULTILINE,
)
TOPIC_RE = re.compile(
    r"^ATM_E3_TOPIC topic_id=([^ ]+) pipeline_us=(\d+)$",
    re.MULTILINE,
)


def percentile(values: list[float], p: float) -> float:
    ordered = sorted(values)
    if not ordered:
        raise ValueError("cannot calculate percentile of an empty sample")
    position = (len(ordered) - 1) * p
    lower = math.floor(position)
    upper = math.ceil(position)
    if lower == upper:
        return ordered[lower]
    return ordered[lower] + (
        ordered[upper] - ordered[lower]
    ) * (position - lower)


def summary(values: list[float]) -> dict[str, float]:
    if not values:
        raise SystemExit("cannot summarize an empty measurement set")
    return {
        "samples": len(values),
        "median": statistics.median(values),
        "p95": percentile(values, 0.95),
        "min": min(values),
        "max": max(values),
    }


def semantic_payload(run: dict) -> list[dict]:
    return [
        {
            "topic_id": topic["topic_id"],
            "outcome": topic["outcome"],
            "results": topic["results"],
            "context_sources": topic["context_sources"],
            "evidence_bytes": topic["evidence_bytes"],
        }
        for topic in run["topics"]
    ]


def semantic_digest(payload: list[dict]) -> str:
    encoded = json.dumps(
        payload,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def load_mode(
    directory: Path,
    expected_run_id: str,
    expected_topic_ids: list[str],
    expected_corpus: list[dict],
    mode: str,
) -> tuple[
    dict,
    str,
    str,
    dict[str, float],
    list[tuple[str, str, int]],
    list[tuple[int, int, int, int, int]],
    tuple[int, int],
]:
    run_path = directory / "run.json"
    log_path = directory / "runner.log"
    if not run_path.is_file() or not log_path.is_file():
        raise SystemExit(f"{mode}: missing run.json or runner.log under {directory}")

    run = json.loads(run_path.read_text(encoding="utf-8"))
    log = log_path.read_text(encoding="utf-8")

    if run.get("run_id") != expected_run_id:
        raise SystemExit(f"{mode}: run identity mismatch: {run.get('run_id')!r}")
    if run.get("corpus") != expected_corpus:
        raise SystemExit(f"{mode}: pinned corpus mismatch")
    topic_ids = [topic.get("topic_id") for topic in run.get("topics", [])]
    if topic_ids != expected_topic_ids:
        raise SystemExit(f"{mode}: topic identity/order mismatch")

    pipeline_matches = TOPIC_RE.findall(log)
    if len(pipeline_matches) != len(expected_topic_ids):
        raise SystemExit(
            f"{mode}: expected {len(expected_topic_ids)} pipeline timing records, "
            f"found {len(pipeline_matches)}"
        )
    pipeline_us: dict[str, float] = {}
    for topic_id, elapsed in pipeline_matches:
        if topic_id in pipeline_us:
            raise SystemExit(f"{mode}: duplicate pipeline timing for {topic_id}")
        if int(elapsed) <= 0:
            raise SystemExit(f"{mode}: non-positive pipeline timing for {topic_id}")
        pipeline_us[topic_id] = int(elapsed) / 1000.0
    if set(pipeline_us) != set(expected_topic_ids):
        raise SystemExit(f"{mode}: pipeline topic set mismatch")

    session_events = [
        (repository_id, event, int(elapsed))
        for repository_id, event, elapsed in SESSION_RE.findall(log)
    ]
    if not session_events:
        raise SystemExit(f"{mode}: no E4 session telemetry was recorded")

    index_matches = INDEX_RE.findall(log)
    if len(index_matches) != len(REPOSITORIES):
        raise SystemExit(f"{mode}: expected one index ensure record per repository")
    index_by_repository = {}
    for repository_id, ensure, elapsed in index_matches:
        if repository_id in index_by_repository:
            raise SystemExit(f"{mode}: duplicate index ensure for {repository_id}")
        if ensure != "reused":
            raise SystemExit(
                f"{mode}: expected prebuilt shared index for {repository_id}, got {ensure}"
            )
        if int(elapsed) <= 0:
            raise SystemExit(f"{mode}: non-positive index ensure duration for {repository_id}")
        index_by_repository[repository_id] = ensure
    if set(index_by_repository) != REPOSITORIES:
        raise SystemExit(f"{mode}: index repository set mismatch")

    close_matches = [
        tuple(map(int, match))
        for match in CACHE_CLOSE_RE.findall(log)
    ]
    teardown_matches = [
        tuple(map(int, match))
        for match in CONVERSATION_TEARDOWN_RE.findall(log)
    ]
    if len(teardown_matches) != 1:
        raise SystemExit(
            f"{mode}: expected one persistent conversation teardown record, "
            f"found {len(teardown_matches)}"
        )
    return (
        run,
        semantic_digest(semantic_payload(run)),
        log,
        pipeline_us,
        session_events,
        close_matches,
        teardown_matches[0],
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--benchmark", required=True, type=Path)
    parser.add_argument("--runs-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    benchmark = json.loads(args.benchmark.read_text(encoding="utf-8"))
    expected_topic_ids = [topic["topic_id"] for topic in benchmark["topics"]]
    expected_corpus = benchmark["corpus"]
    if len(expected_topic_ids) != 33:
        raise SystemExit(f"the frozen R5 corpus must contain 33 topics, got {len(expected_topic_ids)}")

    topic_by_id = {topic["topic_id"]: topic for topic in benchmark["topics"]}
    conversation_topics: dict[str, list[str]] = {}
    for topic in benchmark["topics"]:
        conversation_id = topic.get("conversation_id")
        if conversation_id is not None:
            conversation_topics.setdefault(conversation_id, []).append(topic["topic_id"])
    conversation_topic_ids = {
        topic_id
        for topic_ids in conversation_topics.values()
        for topic_id in topic_ids
    }
    if len(conversation_topic_ids) != 8 or len(conversation_topics) != 4:
        raise SystemExit("the frozen R5 conversation fixture no longer has four two-turn conversations")

    single_turn_topic_ids = [
        topic_id for topic_id in expected_topic_ids
        if topic_id not in conversation_topic_ids
    ]
    expected_cache_closes = len(single_turn_topic_ids) + len(conversation_topics)

    environment_records = []
    orders = {"baseline-first": 0, "candidate-first": 0}
    baseline_digests = set()
    candidate_digests = set()
    baseline_pipeline = {topic_id: [] for topic_id in expected_topic_ids}
    candidate_pipeline = {topic_id: [] for topic_id in expected_topic_ids}
    baseline_retrieval = {topic_id: [] for topic_id in expected_topic_ids}
    candidate_retrieval = {topic_id: [] for topic_id in expected_topic_ids}
    baseline_context = {topic_id: [] for topic_id in expected_topic_ids}
    candidate_context = {topic_id: [] for topic_id in expected_topic_ids}
    paired_pipeline_delta = {topic_id: [] for topic_id in expected_topic_ids}
    conversation_pipeline_delta: dict[str, list[float]] = {
        conversation_id: [] for conversation_id in conversation_topics
    }

    event_counts = {
        "baseline": {
            repository_id: {"turn_open": 0, "turn_open_failed": 0}
            for repository_id in REPOSITORIES
        },
        "candidate": {
            repository_id: {
                "open": 0,
                "open_failed": 0,
                "reuse": 0,
                "invalidate": 0,
            }
            for repository_id in REPOSITORIES
        },
    }
    event_latencies = {
        "baseline": {repository_id: [] for repository_id in REPOSITORIES},
        "candidate_open": {repository_id: [] for repository_id in REPOSITORIES},
        "candidate_reuse": {repository_id: [] for repository_id in REPOSITORIES},
    }
    candidate_close_totals = {
        "sessions": 0,
        "opens": 0,
        "reuses": 0,
        "invalidations": 0,
        "elapsed_us": 0,
    }
    candidate_close_elapsed_ms = []
    baseline_total_work_ms = []
    candidate_total_work_including_teardown_ms = []
    baseline_conversation_teardown_ms = []
    candidate_conversation_teardown_ms = []
    paired_total_work_delta_percent = []
    candidate_close_rows = 0
    baseline_request_total = 0
    candidate_request_total = 0

    for runner_id in range(1, RUNNERS + 1):
        runner_directory = args.runs_dir / f"runner-{runner_id}"
        environment_path = runner_directory / "environment.json"
        prepare_log = runner_directory / "prepare.log"
        if not environment_path.is_file() or not prepare_log.is_file():
            raise SystemExit(f"runner {runner_id:02d}: missing environment.json or prepare.log")
        environment = json.loads(environment_path.read_text(encoding="utf-8"))
        if environment.get("runner_id") != runner_id:
            raise SystemExit(f"runner {runner_id:02d}: environment runner ID mismatch")
        if environment.get("sqlite_version") is None:
            raise SystemExit(f"runner {runner_id:02d}: SQLite version is missing")
        if environment.get("execution_order") not in orders:
            raise SystemExit(f"runner {runner_id:02d}: invalid execution order")
        orders[environment["execution_order"]] += 1
        environment_records.append(environment)

        prep_text = prepare_log.read_text(encoding="utf-8")
        prep_indexes = INDEX_RE.findall(prep_text)
        if len(prep_indexes) != len(REPOSITORIES):
            raise SystemExit(f"runner {runner_id:02d}: prepare phase missing repository index records")
        if {repo: ensure for repo, ensure, _ in prep_indexes} != {
            repo: "rebuilt" for repo in REPOSITORIES
        }:
            raise SystemExit(f"runner {runner_id:02d}: prepare phase did not build all pinned indexes")

        modes = {}
        for mode in ("baseline", "candidate"):
            run, digest, log, pipeline_us, events, close_rows, conversation_teardown = load_mode(
                runner_directory / mode,
                f"e4-runner-{runner_id:02d}-{mode}",
                expected_topic_ids,
                expected_corpus,
                mode,
            )
            modes[mode] = {
                "run": run,
                "digest": digest,
                "log": log,
                "pipeline_us": pipeline_us,
                "events": events,
                "close_rows": close_rows,
                "conversation_teardown": conversation_teardown,
            }
            if mode == "baseline":
                baseline_digests.add(digest)
            else:
                candidate_digests.add(digest)

        baseline = modes["baseline"]
        candidate = modes["candidate"]
        if baseline["digest"] != candidate["digest"]:
            raise SystemExit(
                f"runner {runner_id:02d}: baseline/candidate semantic output differs"
            )

        for mode, data in modes.items():
            for repository_id, event, elapsed in data["events"]:
                event_counts[mode][repository_id][event] += 1
                if mode == "baseline" and event == "turn_open":
                    event_latencies["baseline"][repository_id].append(elapsed / 1000.0)
                elif mode == "candidate" and event == "open":
                    event_latencies["candidate_open"][repository_id].append(elapsed / 1000.0)
                elif mode == "candidate" and event == "reuse":
                    event_latencies["candidate_reuse"][repository_id].append(elapsed / 1000.0)

        if any(
            event_counts["baseline"][repo]["turn_open_failed"] > 0
            for repo in REPOSITORIES
        ):
            raise SystemExit(f"runner {runner_id:02d}: baseline session open failed")
        if any(
            event_counts["candidate"][repo]["open_failed"] > 0
            for repo in REPOSITORIES
        ):
            raise SystemExit(f"runner {runner_id:02d}: candidate session open failed")

        unexpected_baseline = [
            event for _, event, _ in baseline["events"]
            if event not in {"turn_open", "turn_open_failed"}
        ]
        unexpected_candidate = [
            event for _, event, _ in candidate["events"]
            if event not in {"open", "open_failed", "reuse", "invalidate"}
        ]
        if unexpected_baseline or unexpected_candidate:
            raise SystemExit(f"runner {runner_id:02d}: session event appeared in the wrong mode")

        baseline_by_repository = {
            repository_id: sum(
                1 for repo, event, _ in baseline["events"]
                if repo == repository_id and event == "turn_open"
            )
            for repository_id in REPOSITORIES
        }
        candidate_by_repository = {
            repository_id: sum(
                1 for repo, event, _ in candidate["events"]
                if repo == repository_id and event in {"open", "reuse"}
            )
            for repository_id in REPOSITORIES
        }
        if baseline_by_repository != candidate_by_repository:
            raise SystemExit(
                f"runner {runner_id:02d}: per-repository baseline/candidate request counts differ"
            )
        baseline_requests = sum(baseline_by_repository.values())
        candidate_requests = sum(candidate_by_repository.values())
        if baseline_requests != candidate_requests:
            raise SystemExit(
                f"runner {runner_id:02d}: baseline/candidate repository-session request counts differ"
            )
        baseline_request_total += baseline_requests
        candidate_request_total += candidate_requests

        if len(baseline["close_rows"]) != 0:
            raise SystemExit(f"runner {runner_id:02d}: baseline unexpectedly emitted cache-close records")
        if len(candidate["close_rows"]) != expected_cache_closes:
            raise SystemExit(
                f"runner {runner_id:02d}: expected {expected_cache_closes} candidate cache-close records, "
                f"found {len(candidate['close_rows'])}"
            )

        runner_close_totals = {
            "sessions": 0,
            "opens": 0,
            "reuses": 0,
            "invalidations": 0,
            "elapsed_us": 0,
        }
        for sessions, opens, reuses, invalidations, elapsed_us in candidate["close_rows"]:
            if sessions > 3:
                raise SystemExit(f"runner {runner_id:02d}: conversation cache exceeded 3 repositories")
            if sessions != opens - invalidations:
                raise SystemExit(
                    f"runner {runner_id:02d}: cache close count does not reconcile opens and invalidations"
                )
            if elapsed_us < 0:
                raise SystemExit(f"runner {runner_id:02d}: negative cache-close duration")
            runner_close_totals["sessions"] += sessions
            runner_close_totals["opens"] += opens
            runner_close_totals["reuses"] += reuses
            runner_close_totals["invalidations"] += invalidations
            runner_close_totals["elapsed_us"] += elapsed_us
            candidate_close_elapsed_ms.append(elapsed_us / 1000.0)

        candidate_event_totals = {
            "opens": sum(candidate["events"][i][2] >= 0 for i in range(len(candidate["events"])) if candidate["events"][i][1] == "open"),
            "reuses": sum(candidate["events"][i][2] >= 0 for i in range(len(candidate["events"])) if candidate["events"][i][1] == "reuse"),
            "invalidations": sum(1 for _, event, _ in candidate["events"] if event == "invalidate"),
        }
        if runner_close_totals["opens"] != candidate_event_totals["opens"]:
            raise SystemExit(f"runner {runner_id:02d}: cache open counters do not reconcile with event log")
        if runner_close_totals["reuses"] != candidate_event_totals["reuses"]:
            raise SystemExit(f"runner {runner_id:02d}: cache reuse counters do not reconcile with event log")
        if runner_close_totals["invalidations"] != candidate_event_totals["invalidations"]:
            raise SystemExit(f"runner {runner_id:02d}: cache invalidation counters do not reconcile with event log")

        for key, value in runner_close_totals.items():
            candidate_close_totals[key] += value
        candidate_close_rows += len(candidate["close_rows"])

        baseline_teardown_caches, baseline_teardown_us = baseline["conversation_teardown"]
        candidate_teardown_caches, candidate_teardown_us = candidate["conversation_teardown"]
        if (
            baseline_teardown_caches != len(conversation_topics)
            or candidate_teardown_caches != len(conversation_topics)
        ):
            raise SystemExit(
                f"runner {runner_id:02d}: expected {len(conversation_topics)} persistent conversation states "
                "to be destroyed in both modes"
            )
        baseline_teardown_ms = baseline_teardown_us / 1000.0
        candidate_teardown_ms = candidate_teardown_us / 1000.0
        baseline_conversation_teardown_ms.append(baseline_teardown_ms)
        candidate_conversation_teardown_ms.append(candidate_teardown_ms)

        baseline_total_with_teardown_ms = (
            sum(baseline["pipeline_us"].values()) + baseline_teardown_ms
        )
        candidate_total_with_teardown_ms = (
            sum(candidate["pipeline_us"].values()) + candidate_teardown_ms
        )
        baseline_total_work_ms.append(baseline_total_with_teardown_ms)
        candidate_total_work_including_teardown_ms.append(candidate_total_with_teardown_ms)
        paired_total_work_delta_percent.append(
            (candidate_total_with_teardown_ms - baseline_total_with_teardown_ms)
            / baseline_total_with_teardown_ms * 100.0
        )

        for topic_id in expected_topic_ids:
            base_topic = baseline["run"]["topics"][expected_topic_ids.index(topic_id)]
            candidate_topic = candidate["run"]["topics"][expected_topic_ids.index(topic_id)]
            base_pipeline = baseline["pipeline_us"][topic_id]
            candidate_pipeline_ms = candidate["pipeline_us"][topic_id]
            baseline_pipeline[topic_id].append(base_pipeline)
            candidate_pipeline[topic_id].append(candidate_pipeline_ms)

            base_retrieval = base_topic.get("latency_ms")
            candidate_retrieval_value = candidate_topic.get("latency_ms")
            base_context = base_topic.get("context_assembly_latency_ms")
            candidate_context_value = candidate_topic.get("context_assembly_latency_ms")
            values = [
                base_retrieval,
                candidate_retrieval_value,
                base_context,
                candidate_context_value,
            ]
            if any(not isinstance(value, (int, float)) for value in values):
                raise SystemExit(f"runner {runner_id:02d}: missing stage latency for {topic_id}")
            if base_pipeline <= 0 or candidate_pipeline_ms <= 0:
                raise SystemExit(f"runner {runner_id:02d}: invalid pipeline timing for {topic_id}")

            baseline_retrieval[topic_id].append(float(base_retrieval))
            candidate_retrieval[topic_id].append(float(candidate_retrieval_value))
            baseline_context[topic_id].append(float(base_context))
            candidate_context[topic_id].append(float(candidate_context_value))
            paired_pipeline_delta[topic_id].append(
                (candidate_pipeline_ms - base_pipeline) / base_pipeline * 100.0
            )

            conversation_id = topic_by_id[topic_id].get("conversation_id")
            if conversation_id is not None:
                conversation_pipeline_delta[conversation_id].append(
                    (candidate_pipeline_ms - base_pipeline) / base_pipeline * 100.0
                )

    if orders != {"baseline-first": 5, "candidate-first": 5}:
        raise SystemExit(f"unbalanced baseline/candidate execution order: {orders}")
    if len({item["sqlite_version"] for item in environment_records}) != 1:
        raise SystemExit("SQLite versions differ across independent runners")
    if len({item.get("runner_identity") for item in environment_records}) != RUNNERS:
        raise SystemExit("runner identities are not ten distinct values")
    if len(baseline_digests) != 1 or len(candidate_digests) != 1:
        raise SystemExit("semantic output differs across independent runners")
    if baseline_digests != candidate_digests:
        raise SystemExit("baseline and candidate cross-runner semantic digests differ")
    if baseline_request_total != candidate_request_total:
        raise SystemExit("total baseline and candidate session request counts differ")
    if not any(
        event_counts["candidate"][repo]["reuse"] > 0
        for repo in REPOSITORIES
    ):
        raise SystemExit("candidate did not reuse any conversation-scoped query session")

    output = {
        "schema_version": 1,
        "experiment": "E4",
        "benchmark_id": benchmark["benchmark_id"],
        "runner_count": RUNNERS,
        "sqlite_version": environment_records[0]["sqlite_version"],
        "execution_order": orders,
        "semantic_equivalence": True,
        "semantic_sha256": next(iter(baseline_digests)),
        "topics": len(expected_topic_ids),
        "conversation_count": len(conversation_topics),
        "conversation_topics": len(conversation_topic_ids),
        "single_turn_topics": len(single_turn_topic_ids),
        "session_requests": {
            "baseline_turn_local": baseline_request_total,
            "candidate_conversation_scoped": candidate_request_total,
        },
        "session_event_counts_by_repository": event_counts,
        "candidate_cache_close": {
            "cache_instances_closed": candidate_close_rows,
            "totals": candidate_close_totals,
            "elapsed_ms_per_cache": summary(candidate_close_elapsed_ms),
        },
        "persistent_conversation_state_teardown_ms": {
            "baseline_turn_local": summary(baseline_conversation_teardown_ms),
            "candidate_conversation_scoped": summary(candidate_conversation_teardown_ms),
        },
        "paired_pipeline_plus_conversation_teardown_ms": {
            "baseline": summary(baseline_total_work_ms),
            "candidate": summary(candidate_total_work_including_teardown_ms),
            "paired_delta_percent_candidate_vs_baseline": summary(
                paired_total_work_delta_percent
            ),
        },
        "session_open_validation_ms_by_repository": {
            repository_id: {
                "baseline_turn_open": summary(event_latencies["baseline"][repository_id]),
                "candidate_first_open": summary(event_latencies["candidate_open"][repository_id]),
                "candidate_reuse_lookup": (
                    summary(event_latencies["candidate_reuse"][repository_id])
                    if event_latencies["candidate_reuse"][repository_id]
                    else None
                ),
            }
            for repository_id in sorted(REPOSITORIES)
        },
        "estimated_session_open_validation_ms_avoided": {
            repository_id: (
                sum(event_latencies["baseline"][repository_id])
                - sum(event_latencies["candidate_open"][repository_id])
            )
            for repository_id in sorted(REPOSITORIES)
        },
        "topic_latency_ms": {
            topic_id: {
                "baseline": {
                    "pipeline": summary(baseline_pipeline[topic_id]),
                    "conversation_retrieval": summary(baseline_retrieval[topic_id]),
                    "grounding_context_assembly": summary(baseline_context[topic_id]),
                },
                "candidate": {
                    "pipeline": summary(candidate_pipeline[topic_id]),
                    "conversation_retrieval": summary(candidate_retrieval[topic_id]),
                    "grounding_context_assembly": summary(candidate_context[topic_id]),
                },
                "paired_pipeline_delta_percent_candidate_vs_baseline": summary(
                    paired_pipeline_delta[topic_id]
                ),
            }
            for topic_id in expected_topic_ids
        },
        "one_turn_topics": {
            "baseline_pipeline": summary([
                value for topic_id in single_turn_topic_ids for value in baseline_pipeline[topic_id]
            ]),
            "candidate_pipeline": summary([
                value for topic_id in single_turn_topic_ids for value in candidate_pipeline[topic_id]
            ]),
        },
        "multi_turn_conversations": {
            conversation_id: {
                "topic_ids": topic_ids,
                "paired_pipeline_delta_percent_candidate_vs_baseline": summary(
                    conversation_pipeline_delta[conversation_id]
                ),
            }
            for conversation_id, topic_ids in conversation_topics.items()
        },
        "interpretation": {
            "baseline": "one validated read-only SQLite query session per selected repository per retrieval turn",
            "candidate": "one lazily opened validated read-only SQLite query session per repository/index path, private to one frozen conversation",
            "candidate_cache_lifetime": "conversation-state lifetime; cache-close counters reconcile with open, reuse and invalidation telemetry",
            "pipeline_boundary": "E3 append_topic_run wall time, including retrieval, context assembly and benchmark topic result-record construction",
            "production_policy": "unchanged; conversation-scoped reuse is opt-in for this benchmark only",
        },
        "runners": environment_records,
    }

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(output, indent=2) + "\n", encoding="utf-8")
    print(f"PASS: E4 aggregate validated {RUNNERS}/{RUNNERS} independent paired runners.")
    print(f"PASS: baseline/candidate semantic SHA-256 {output['semantic_sha256']}")
    print(
        "Pipeline + persistent conversation teardown baseline/candidate median: "
        f"{output['paired_pipeline_plus_conversation_teardown_ms']['baseline']['median']:.3f}/"
        f"{output['paired_pipeline_plus_conversation_teardown_ms']['candidate']['median']:.3f} ms"
    )
    print(
        "Session requests baseline/candidate: "
        f"{baseline_request_total}/{candidate_request_total}"
    )
    print(
        "Candidate cache opens/reuses/invalidations: "
        f"{candidate_close_totals['opens']}/"
        f"{candidate_close_totals['reuses']}/"
        f"{candidate_close_totals['invalidations']}"
    )
    for repository_id in sorted(REPOSITORIES):
        base = output["session_open_validation_ms_by_repository"][repository_id]["baseline_turn_open"]
        cand = output["session_open_validation_ms_by_repository"][repository_id]["candidate_first_open"]
        print(
            f"{repository_id} session-open baseline/candidate median: "
            f"{base['median']:.3f}/{cand['median']:.3f} ms"
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
