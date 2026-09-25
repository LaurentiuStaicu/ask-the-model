#!/usr/bin/env python3

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
LIFECYCLE = ROOT / "src" / "RepositoryLifecycleService.vala"
SERVICE = ROOT / "src" / "RepositoryService.vala"
NATIVE = ROOT / "src" / "RepositoryNative.vala"
MESON = ROOT / "meson.build"
POLICY = ROOT / "qualification" / "capacity-policy-v1.json"


def fail(message: str) -> None:
    raise SystemExit(
        f"capacity runtime wiring validation failed: {message}"
    )


def require(haystack: str, needle: str, context: str) -> int:
    position = haystack.find(needle)
    if position < 0:
        fail(f"{context} lost required contract: {needle}")
    return position


def block_between(
    text: str,
    start_marker: str,
    end_marker: str,
    context: str,
) -> str:
    start = require(text, start_marker, context)
    end = text.find(end_marker, start + len(start_marker))
    if end < 0:
        fail(f"{context} end marker is missing")
    return text[start:end]


def main() -> int:
    lifecycle = LIFECYCLE.read_text(encoding="utf-8")
    service = SERVICE.read_text(encoding="utf-8")
    native = NATIVE.read_text(encoding="utf-8")
    meson = MESON.read_text(encoding="utf-8")
    policy = json.loads(POLICY.read_text(encoding="utf-8"))

    if policy.get("status") != "selected-runtime-wired":
        fail("runtime wiring requires selected-runtime-wired policy status")
    if policy.get("runtime_integration_selected") is not True:
        fail("runtime wiring must be selected in the production policy")

    integration = policy.get("runtime_integration")
    if not isinstance(integration, dict):
        fail("runtime integration metadata is missing")
    if integration.get("runtime_source") != (
        "src/RepositoryLifecycleService.vala"
    ):
        fail("runtime integration source drifted")
    if integration.get("authority_lease_required") is not True:
        fail("runtime integration lost the authority-lease prerequisite")
    if integration.get("checkpoints") != [
        "PRE_DOWNLOAD",
        "POST_DOWNLOAD_PRE_MUTATION",
        "STATE_PUBLICATION",
    ]:
        fail("runtime checkpoint registry drifted")

    gate = policy.get("runtime_gate")
    if not isinstance(gate, dict):
        fail("runtime gate is missing from production policy")
    if gate.get("default_enabled") is not False:
        fail("Optimizations must remain default OFF")
    if gate.get("operation_snapshot_required") is not True:
        fail("capacity runtime must use one operation snapshot")

    operation = block_between(
        lifecycle,
        "        public async uint download_or_update (",
        "\n    }\n}",
        "download_or_update",
    )

    snapshot = require(
        operation,
        "bool optimized_operation =\n"
        "                optimization_mode_snapshot ();",
        "download_or_update",
    )
    lease = require(
        operation,
        "if (optimized_operation) {\n"
        "                bool contended = false;",
        "download_or_update",
    )
    if lease < snapshot:
        fail("authority lease must follow the operation snapshot")

    checkpoint_a = require(
        operation,
        "if (optimized_operation) {\n"
        "                                require_download_capacity (",
        "checkpoint A",
    )
    download = require(
        operation,
        "yield client.download_archive_to_staging (",
        "checkpoint A",
    )
    if checkpoint_a >= download:
        fail("checkpoint A must run before archive .part creation/download")

    checkpoint_b = require(
        operation,
        "if (optimized_operation &&\n"
        "                            archive_path != null) {",
        "checkpoint B",
    )
    mutation_call = require(
        operation,
        "require_mutation_capacity (",
        "checkpoint B",
    )
    quarantine = require(
        operation,
        "RepositoryNative.quarantine_snapshot (",
        "checkpoint B",
    )
    prepare = require(
        operation,
        "yield prepare_snapshot (",
        "checkpoint B",
    )
    if not (checkpoint_b < mutation_call < quarantine < prepare):
        fail(
            "checkpoint B must precede same-SHA quarantine and snapshot preparation"
        )

    state_guard = require(
        operation,
        "if (optimized_operation) {\n"
        "                            require_state_commit_capacity (",
        "state guard",
    )
    state_commit = require(
        operation,
        "state_store.set_current (",
        "state guard",
    )
    if state_guard >= state_commit:
        fail("state headroom guard must run before Control DB publication")

    no_space_count = lifecycle.count(
        "throw new RepositoryError.NO_SPACE ("
    )
    if no_space_count != 3:
        fail(
            "runtime capacity wiring must have exactly three explicit NO_SPACE rejection sites"
        )

    for method, label in (
        ("require_download_capacity", "pre-download"),
        ("require_mutation_capacity", "post-download"),
        ("require_state_commit_capacity", "state-publication"),
    ):
        start = require(
            lifecycle,
            f"        private void {method} (",
            label,
        )
        next_method = lifecycle.find(
            "\n        private ",
            start + 1,
        )
        if next_method < 0:
            next_method = lifecycle.find(
                "\n        internal ",
                start + 1,
            )
        if next_method < 0:
            next_method = len(lifecycle)
        method_text = lifecycle[start:next_method]
        require(
            method_text,
            "if (!admitted) {",
            label,
        )
        require(
            method_text,
            "throw new RepositoryError.NO_SPACE (",
            label,
        )
        require(
            method_text,
            "catch (GLib.Error error)",
            label,
        )
        require(
            method_text,
            "new RepositoryError.STORAGE (",
            label,
        )

    staging_create = require(
        service,
        "GLib.DirUtils.create_with_parents (",
        "archive staging",
    )
    part_create = require(
        service,
        "partial_file.replace_async (",
        "archive staging",
    )
    if staging_create >= part_create:
        fail("archive staging parent must exist before .part creation")

    for binding in (
        "capacity_download_preflight",
        "capacity_mutation_preflight",
        "capacity_state_commit_preflight",
    ):
        require(native, binding, "RepositoryNative bindings")

    runtime_sources = (
        "src/capacity_admission_model.c",
        "src/capacity_operation_plan.c",
        "src/capacity_measurement.c",
        "src/repository_capacity_policy.c",
        "src/repository_capacity_admission.c",
        "src/repository_capacity_ui_bridge.c",
    )
    main_start = require(
        meson,
        "executable(\n  meson.project_name(),",
        "main executable",
    )
    main_end = meson.find(
        "\n)\n\nrepository_store_oracle",
        main_start,
    )
    if main_end < 0:
        fail("main executable boundary is missing")
    main_target = meson[main_start:main_end]

    lifecycle_start = require(
        meson,
        "repository_lifecycle_service_test = executable(",
        "lifecycle test target",
    )
    lifecycle_end = meson.find(
        "\n)\n\ntest(\n  'repository-lifecycle-service'",
        lifecycle_start,
    )
    if lifecycle_end < 0:
        fail("lifecycle test target boundary is missing")
    lifecycle_target = meson[lifecycle_start:lifecycle_end]

    for source in runtime_sources:
        if source not in main_target:
            fail(f"main executable is missing {source}")
        if source not in lifecycle_target:
            fail(f"lifecycle test target is missing {source}")

    if "capacity_operation_kind (" not in lifecycle:
        fail("runtime operation classification helper is missing")

    print(
        "capacity runtime wiring validation passed: "
        "OFF gate + A/B/state ordering + specific NO_SPACE mapping"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
