#!/usr/bin/env python3

import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def fail(message: str) -> None:
    raise SystemExit(f"D1 context policy validation failed: {message}")


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def require_marker(text: str, marker: str, context: str) -> int:
    index = text.find(marker)
    if index < 0:
        fail(f"{context} lost marker: {marker}")
    return index


def main() -> int:
    policy = json.loads(
        (ROOT / "qualification" / "d1-context-policy-v1.json").read_text(
            encoding="utf-8"
        )
    )
    d0 = json.loads(
        (
            ROOT
            / "qualification"
            / "d0-retrieval-context-rebaseline-v1.json"
        ).read_text(encoding="utf-8")
    )

    require(policy.get("schema_version") == 1, "schema version drifted")
    require(
        policy.get("policy_id") == "atm-d1-source-cap4-runtime-v1",
        "policy id drifted",
    )
    require(
        policy.get("status") in {
            "implementation-under-qualification",
            "qualified-runtime-ready",
        },
        "D1 policy status is invalid",
    )

    baseline = policy.get("baseline_policy", {})
    require(
        baseline
        == {
            "max_results_per_repository": 6,
            "max_context_sources": 12,
            "max_context_bytes": 32768,
        },
        "OFF baseline policy drifted",
    )

    optimized = policy.get("optimized_policy", {})
    require(
        optimized
        == {
            "gate": "OPTIMIZATIONS_ON_OPERATION_SNAPSHOT",
            "max_results_per_repository": 6,
            "max_context_sources": 4,
            "max_context_bytes": 32768,
            "d0_frontier_policy": "candidate-sources4",
        },
        "ON optimized policy drifted",
    )

    frontier = d0.get("observational_frontier", {})
    require(
        frontier.get("policy") == "candidate-sources4",
        "D0 frontier is no longer source-cap 4",
    )
    require(
        frontier.get("promotion_eligible_observational") is True,
        "D0 frontier is no longer observationally eligible",
    )
    require(
        frontier.get("topic_regression_count") == 0,
        "D0 frontier now has topic regressions",
    )
    require(
        frontier.get("max_results_per_repository") == 6
        and frontier.get("max_context_sources") == 4
        and frontier.get("max_context_bytes") == 32768,
        "D0 frontier parameters drifted",
    )

    retrieval_policy = (
        ROOT / "src" / "retrieval_policy.h"
    ).read_text(encoding="utf-8")
    for marker in (
        "#define ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY 6",
        "#define ATM_PRODUCTION_GROUNDING_MAX_SOURCES 12",
        "#define ATM_OPTIMIZED_GROUNDING_MAX_SOURCES 4",
        "#define ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES (32 * 1024)",
    ):
        require_marker(retrieval_policy, marker, "retrieval policy")

    application = (ROOT / "src" / "Application.vala").read_text(
        encoding="utf-8"
    )
    send_start = require_marker(
        application,
        "private async void send_prompt (",
        "Application Send operation",
    )
    send_end = application.find(
        "\n        private ",
        send_start + 1,
    )
    if send_end < 0:
        send_end = len(application)
    send = application[send_start:send_end]

    snapshot_index = require_marker(
        send,
        "optimization_policy.snapshot_enabled ();",
        "Send optimization snapshot",
    )
    first_yield = send.find("yield ")
    require(first_yield >= 0, "Send has no async boundary")
    require(
        snapshot_index < first_yield,
        "optimization mode is not snapshotted before the first async boundary",
    )
    require_marker(
        send,
        "state.session.prepare_turn (\n"
        "                        prompt,\n"
        "                        optimized_operation,",
        "Send explicit snapshot propagation",
    )
    require(
        send.count("optimization_policy.snapshot_enabled ()") == 1,
        "Send must snapshot optimization mode exactly once",
    )

    session = (
        ROOT / "src" / "ConversationSession.vala"
    ).read_text(encoding="utf-8")
    for marker in (
        "public bool prepare_turn (\n"
        "            string query,\n"
        "            bool optimized_operation,",
        "grounding.prepare_turn (\n"
        "                query,\n"
        "                optimized_operation,",
    ):
        require_marker(session, marker, "ConversationSession propagation")

    grounding_vala = (
        ROOT / "src" / "ConversationGrounding.vala"
    ).read_text(encoding="utf-8")
    for marker in (
        "public static extern bool prepare_turn (\n"
        "            void* state,\n"
        "            string query,\n"
        "            bool optimized_operation,",
        "public bool prepare_turn (\n"
        "            string query,\n"
        "            bool optimized_operation,",
        "ConversationGroundingNative.prepare_turn (\n"
        "                    state,\n"
        "                    query,\n"
        "                    optimized_operation,",
    ):
        require_marker(
            grounding_vala,
            marker,
            "ConversationGrounding propagation",
        )

    grounding_c = (
        ROOT / "src" / "conversation_grounding.c"
    ).read_text(encoding="utf-8")
    for marker in (
        "gboolean optimized_operation,",
        "optimized_operation\n"
        "            ? ATM_OPTIMIZED_GROUNDING_MAX_SOURCES\n"
        "            : ATM_PRODUCTION_GROUNDING_MAX_SOURCES;",
        "ATM_PRODUCTION_RETRIEVAL_RESULTS_PER_REPOSITORY,",
        "ATM_PRODUCTION_GROUNDING_MAX_CONTEXT_BYTES,",
    ):
        require_marker(grounding_c, marker, "native grounding policy")

    state_boundary = policy.get("state_boundary", {})
    for key in (
        "backend_widget_read_forbidden",
        "environment_override_forbidden",
        "persisted_mode_forbidden",
        "conversation_level_mode_pin_forbidden",
        "restored_conversation_uses_current_send_snapshot",
    ):
        require(state_boundary.get(key) is True, f"state boundary {key} drifted")

    for backend_path in (
        "src/ConversationSession.vala",
        "src/ConversationGrounding.vala",
        "src/conversation_grounding.c",
    ):
        backend = (ROOT / backend_path).read_text(encoding="utf-8")
        require(
            "Gtk.Switch" not in backend
            and "optimization_switch" not in backend
            and "OptimizationPolicy" not in backend,
            f"{backend_path} acquired hidden optimization UI/policy ownership",
        )

    tests = (
        ROOT / "tests" / "conversation_grounding_test.c"
    ).read_text(encoding="utf-8")
    for marker in (
        '"/conversation-grounding/d1-operation-source-cap"',
        "test_operation_snapshot_selects_context_source_cap",
        '"shared optimization frontier evidence context policy"',
        "FALSE,\n            &has_grounding,",
        "TRUE,\n            &has_grounding,",
        "ATM_OPTIMIZED_GROUNDING_MAX_SOURCES",
        '"[S6]"',
        '"[S4]"',
        '"[S5]"',
        '"OFF baseline provenance [S6]."',
        '"ON optimized provenance [S4]."',
        "off_citation->repository_id",
        "off_citation->snapshot_sha",
        "on_citation->repository_id",
        "on_citation->snapshot_sha",
    ):
        require_marker(tests, marker, "D1 OFF/ON grounding qualification")

    implementation = policy.get("implementation_state", {})
    for key in (
        "explicit_operation_snapshot_implemented",
        "snapshot_propagation_implemented",
        "off_baseline_source_cap_implemented",
        "on_source_cap4_implemented",
        "deterministic_off_on_grounding_test_added",
    ):
        require(implementation.get(key) is True, f"implementation {key} missing")

    if policy.get("status") == "qualified-runtime-ready":
        for key in (
            "structural_validator_added",
            "dedicated_d1_workflow_added",
            "frozen_r5_replay_complete",
            "flatpak_qualification_complete",
            "runtime_merge_authorized",
        ):
            require(
                implementation.get(key) is True,
                f"qualified D1 missing {key}",
            )
    else:
        require(
            implementation.get("runtime_merge_authorized") is False,
            "unqualified D1 cannot authorize runtime merge",
        )

    print(
        "D1 context policy validation passed: Send snapshots Optimizations "
        "before the first async boundary, OFF preserves 6/12/32 KiB, ON selects "
        "only the D0-qualified source cap 4, and the mode is propagated "
        "explicitly without hidden or persisted backend state"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
