#!/usr/bin/env python3

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
COORD = ROOT / "src" / "coordination_lease.c"
GEN = ROOT / "src" / "repository_generation_lease.c"
GROUNDING = ROOT / "src" / "ConversationGrounding.vala"
LIFECYCLE = ROOT / "src" / "RepositoryLifecycleService.vala"
SESSION = ROOT / "src" / "ConversationSession.vala"
MESON = ROOT / "meson.build"


def fail(message: str) -> None:
    raise SystemExit(
        f"generation lease wiring validation failed: {message}"
    )


def require(text: str, needle: str, context: str) -> int:
    pos = text.find(needle)
    if pos < 0:
        fail(f"{context} lost required contract: {needle}")
    return pos


def main() -> int:
    coord = COORD.read_text(encoding="utf-8")
    gen = GEN.read_text(encoding="utf-8")
    grounding = GROUNDING.read_text(encoding="utf-8")
    lifecycle = LIFECYCLE.read_text(encoding="utf-8")
    session = SESSION.read_text(encoding="utf-8")
    meson = MESON.read_text(encoding="utf-8")

    require(
        coord,
        "ATM_COORDINATION_LEASE_MODE_SHARED",
        "common lease helper",
    )
    require(
        coord,
        "ATM_COORDINATION_LEASE_MODE_EXCLUSIVE",
        "common lease helper",
    )
    require(
        coord,
        "return atm_coordination_lease_acquire_mode (\n"
        "        path,\n"
        "        ATM_COORDINATION_LEASE_MODE_EXCLUSIVE,",
        "legacy exclusive wrapper",
    )

    require(
        gen,
        "ATM_COORDINATION_LEASE_MODE_SHARED",
        "generation shared lease",
    )
    require(
        gen,
        "ATM_COORDINATION_LEASE_MODE_EXCLUSIVE",
        "future-GC exclusive probe",
    )
    require(
        gen,
        "if (generation_id <= 0)",
        "generation-zero guard",
    )
    zero_guard = require(
        gen,
        "if (generation_id <= 0)",
        "generation-zero guard",
    )
    dir_create = require(
        gen,
        "ensure_lease_directory (",
        "generation lease directory",
    )
    if zero_guard >= dir_create:
        fail("generation 0 must fail before generation-lock directory creation")

    grounding_field = require(
        grounding,
        "private RepositoryGenerationLease? generation_lease = null;",
        "grounding ownership",
    )
    hold = require(
        grounding,
        "internal void hold_generation_lease (",
        "grounding ownership",
    )
    destructor_release = require(
        grounding,
        "generation_lease = null;",
        "grounding ownership",
    )
    if not (
        grounding_field < destructor_release < hold
    ):
        fail("grounding lease ownership/destruction ordering drifted")

    method_start = require(
        lifecycle,
        "        private async ConversationGrounding\n"
        "        prepare_conversation_grounding_for_generation (",
        "grounding lifecycle",
    )
    method_end = lifecycle.find(
        "\n        public async ConversationGrounding\n"
        "        prepare_conversation_grounding (",
        method_start,
    )
    if method_end < 0:
        fail("grounding lifecycle method boundary missing")
    method = lifecycle[method_start:method_end]

    zero_path = require(
        method,
        "if (selected.length == 0)",
        "zero-repository path",
    )
    positive_guard = require(
        method,
        "if (generation_id <= 0)",
        "positive-generation path",
    )
    snapshot = require(
        method,
        "bool optimized_operation =\n"
        "                optimization_mode_snapshot ();",
        "optimization gate",
    )
    acquire = require(
        method,
        "RepositoryGenerationLease.\n"
        "                            acquire_shared (",
        "shared generation lease",
    )
    attach = require(
        method,
        "grounding.hold_generation_lease (",
        "shared generation lease",
    )
    load = require(
        method,
        "ControlStateNative.load_repository_values_at_generation (",
        "generation read",
    )
    prepare = require(
        method,
        "result = yield prepare_snapshot (",
        "snapshot/index use",
    )

    if not (
        zero_path < positive_guard < snapshot <
        acquire < attach < load < prepare
    ):
        fail(
            "generation lease must be gated after zero/positive checks "
            "and held before generation/snapshot/index use"
        )

    if "try_acquire_mutation_lease" in method:
        fail(
            "reader path must never acquire global mutation lease "
            "while holding a generation shared lease"
        )

    require(
        session,
        "grounding = prepared_grounding;",
        "session grounding ownership",
    )
    reset = require(
        session,
        "public void reset ()",
        "session reset",
    )
    drop = session.find(
        "grounding = null;",
        reset,
    )
    if drop < 0:
        fail("session reset no longer drops grounding ownership")

    runtime_targets = [
        (
            "executable(\n  meson.project_name(),",
            "\n)\n\nrepository_store_oracle",
        ),
        (
            "conversation_grounding_vala_test = executable(",
            "\n)\n\ntest(\n  'conversation-grounding-vala'",
        ),
        (
            "conversation_session_test = executable(",
            "\n)\n\ntest(\n  'conversation-session'",
        ),
        (
            "repository_lifecycle_service_test = executable(",
            "\n)\n\ntest(\n  'repository-lifecycle-service'",
        ),
    ]
    for start_marker, end_marker in runtime_targets:
        start = require(
            meson,
            start_marker,
            "Meson target",
        )
        end = meson.find(end_marker, start)
        if end < 0:
            fail(f"Meson target boundary missing: {start_marker}")
        block = meson[start:end]
        if "src/repository_generation_lease.c" not in block:
            fail(
                f"generation lease source missing from target: {start_marker}"
            )

    require(
        meson,
        "repository_generation_lease_test = executable(",
        "generation lease native tests",
    )

    print(
        "generation lease wiring validation passed: "
        "default-OFF gate + SH lifetime + no-upgrade lock order"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
