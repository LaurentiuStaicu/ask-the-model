#!/usr/bin/env python3

import hashlib
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
EVIDENCE_PATH = ROOT / "benchmarks" / "durability-v3" / "evidence.json"

EXPECTED_M12A_SOURCE = {
    "actions_run_id": 36161193316,
    "artifact_name": "a1-m12-runtime-authority-replay",
    "artifact_id": 10875149763,
    "artifact_sha256": (
        "ba1c080083fa43c6b6a43f6e213f3637108387321abb827655f673670535b27b"
    ),
    "artifact_json_sha256": (
        "1e712ffb3d12f7406281b8c6852ad5629197f75732a91db75f6fafadd60b50f2"
    ),
    "atm_source_commit": "9ffc6b126e00e5b2f9ec5a28b327093d7351131d",
}

EXPECTED_M12B_SOURCE = {
    "actions_run_id": 36161193052,
    "artifact_name": "a1-m12b-lifecycle-fsync-eio",
    "artifact_id": 10875790793,
    "artifact_sha256": (
        "5fd3bf5ab048c8020ac9c03c76e631e3ba54cbede14b2cbca02ce66ce78d5803"
    ),
    "artifact_json_sha256": (
        "ce0e9fe27bd3468eafb49a97ac80e6cb072feedb4dcd58a4636f143ee1211e76"
    ),
    "atm_source_commit": "9ffc6b126e00e5b2f9ec5a28b327093d7351131d",
}

EXPECTED_BOUNDARIES = [
    "post_durable_ingest_pre_preindex_seal",
    "post_preindex_seal_pre_index",
    "post_index_pre_postindex_seal",
    "post_postindex_seal_pre_state_capacity",
    "post_state_capacity_pre_authority",
    "after_authority",
]

EXPECTED_SEQUENCE = [
    "MUTATION_LEASE",
    "DURABLE_INGEST_S1_DEST_SOURCE",
    "PROMOTED_PRE_INDEX_SEAL_MATCHES_PRE_BARRIER_SEAL",
    "COORDINATED_RETRIEVAL_INDEX",
    "POST_INDEX_SEAL_AND_COUNTS_MATCH",
    "STATE_CAPACITY_PREFLIGHT",
    "GUARDED_CONTROL_DB_PUBLICATION",
]

EXPECTED_M12B_CHECKPOINTS = [
    "runtime_ingest_before_tree_fsync",
    "namespace_repository_before_child_fsync",
    "runtime_ingest_before_destination_parent_fsync",
    "runtime_ingest_before_source_parent_fsync",
]


def fail(message: str) -> None:
    raise SystemExit(
        f"durability-v3 evidence validation failed: {message}"
    )


def exact_measured_digest(
    payload: dict,
    source: dict,
    expected_source: dict,
    label: str,
) -> None:
    if source != expected_source:
        fail(f"{label} source provenance drifted")

    measured = dict(payload)
    measured.pop("source", None)
    canonical = (
        json.dumps(measured, indent=2, sort_keys=True) + "\n"
    ).encode("utf-8")
    digest = hashlib.sha256(canonical).hexdigest()
    if digest != expected_source["artifact_json_sha256"]:
        fail(
            f"{label} measured payload no longer matches frozen "
            f"artifact JSON digest: {digest}"
        )


def validate_m12a(m12a: dict) -> None:
    exact_measured_digest(
        m12a,
        m12a.get("source"),
        EXPECTED_M12A_SOURCE,
        "M12a",
    )

    if m12a.get("kernel") != {
        "machine": "x86_64",
        "release": "6.17.0-1022-azure",
        "system": "Linux",
    }:
        fail("M12a kernel context drifted")
    if m12a.get("measurement_id") != (
        "atm-a1-m12-runtime-authority-replay-v1"
    ):
        fail("M12a measurement_id drifted")
    if m12a.get("status") != "measurement-only":
        fail("M12a payload must remain measurement-only")
    if m12a.get("runtime_fault_qualification_complete") is not False:
        fail("M12a artifact must not self-authorize runtime qualification")

    protocol = m12a.get("protocol")
    if not isinstance(protocol, dict):
        fail("M12a protocol is missing")
    if protocol.get("boundary_order") != EXPECTED_BOUNDARIES:
        fail("M12a boundary order drifted")
    if protocol.get("local_sequence") != EXPECTED_SEQUENCE:
        fail("M12a local authority sequence drifted")
    if protocol.get("network_download_excluded") is not True:
        fail("M12a network scope marker drifted")
    if protocol.get("process_interruption_only") is not True:
        fail("M12a process-interruption scope marker drifted")
    if protocol.get("physical_power_loss_claim") is not False:
        fail("M12a must not claim physical power-loss qualification")

    results = m12a.get("results")
    if not isinstance(results, list) or len(results) != 6:
        fail("M12a must freeze exactly six interruption results")

    by_boundary = {
        item.get("boundary"): item
        for item in results
        if isinstance(item, dict)
    }
    if set(by_boundary) != set(EXPECTED_BOUNDARIES):
        fail("M12a frozen boundary set drifted")

    for boundary in EXPECTED_BOUNDARIES[:-1]:
        item = by_boundary[boundary]
        verification = item.get("verification", {})
        if item.get("authority_target_satisfied") is not True:
            fail(f"{boundary}: authority target no longer satisfied")
        if item.get("classification_match") is not True:
            fail(f"{boundary}: classification match lost")
        if item.get("expected_classification") != "EMPTY_AUTHORITY_VALID":
            fail(f"{boundary}: expected class drifted")
        if verification.get("classification") != "EMPTY_AUTHORITY_VALID":
            fail(f"{boundary}: empty-valid authority result drifted")
        if verification.get("active_generation_id") != 0:
            fail(f"{boundary}: pre-authority generation must remain zero")
        if verification.get("active_repository_sha") is not None:
            fail(f"{boundary}: pre-authority SHA must remain absent")
        if verification.get("qualified") is not True:
            fail(f"{boundary}: verifier qualification lost")

    after = by_boundary["after_authority"]
    after_verification = after.get("verification", {})
    if after.get("expected_classification") != "NEW_AUTHORITY_VALID":
        fail("after-authority expected class drifted")
    if after.get("authority_target_satisfied") is not True:
        fail("after-authority target is no longer satisfied")
    if after.get("classification_match") is not True:
        fail("after-authority classification match was lost")
    if after_verification.get("classification") != "NEW_AUTHORITY_VALID":
        fail("after-authority classification drifted")
    if after_verification.get("active_generation_id") != 1:
        fail("after-authority generation must remain one")
    if after_verification.get("active_repository_sha") != (
        "0123456789abcdef0123456789abcdef01234567"
    ):
        fail("after-authority SHA drifted")
    if after_verification.get("seal_match") is not True:
        fail("after-authority seal match was lost")
    if after_verification.get("qualified") is not True:
        fail("after-authority verifier qualification lost")

    mismatch = m12a.get("seal_mismatch", {})
    execution = mismatch.get("execution", {})
    verification = mismatch.get("verification", {})
    if execution.get("seal_mismatch_detected") is not True:
        fail("forced seal mismatch detection was lost")
    if execution.get("authority_published") is not False:
        fail("forced seal mismatch unexpectedly publishes authority")
    if verification.get("classification") != "EMPTY_AUTHORITY_VALID":
        fail("seal-mismatch recovery classification drifted")
    if verification.get("active_generation_id") != 0:
        fail("seal mismatch must preserve generation zero")
    if verification.get("active_repository_sha") is not None:
        fail("seal mismatch must preserve absent repository authority")
    if verification.get("qualified") is not True:
        fail("seal mismatch verifier qualification lost")

    if m12a.get("summary") != {
        "after_authority_new_valid": True,
        "all_pre_authority_interruptions_empty_valid": True,
        "seal_mismatch_fail_closed": True,
    }:
        fail("M12a summary drifted")


def validate_m12b(m12b: dict) -> None:
    exact_measured_digest(
        m12b,
        m12b.get("source"),
        EXPECTED_M12B_SOURCE,
        "M12b",
    )

    if m12b.get("kernel") != {
        "machine": "x86_64",
        "release": "6.17.0-1022-azure",
        "system": "Linux",
    }:
        fail("M12b kernel context drifted")
    if m12b.get("measurement_id") != (
        "atm-a1-m12b-lifecycle-fsync-eio-v1"
    ):
        fail("M12b measurement_id drifted")
    if m12b.get("status") != "measurement-only":
        fail("M12b payload must remain measurement-only")
    if m12b.get("source_commit") != EXPECTED_M12B_SOURCE[
        "atm_source_commit"
    ]:
        fail("M12b embedded source commit drifted")
    if m12b.get("runtime_fault_qualification_complete") is not False:
        fail("M12b artifact must not self-authorize runtime qualification")

    protocol = m12b.get("protocol")
    if not isinstance(protocol, dict):
        fail("M12b protocol is missing")
    expected_protocol = {
        "block_layer_claim": False,
        "fault_injection_scope": "M12_TEST_TARGET_ONLY",
        "m11b_remains_block_layer_evidence": True,
        "network_download_excluded_with_test_only_local_archive_seam": True,
        "optimizations_snapshot": "ON",
        "physical_power_loss_claim": False,
        "real_lifecycle_entrypoint": (
            "RepositoryLifecycleService.download_or_update"
        ),
    }
    if protocol != expected_protocol:
        fail("M12b protocol drifted")

    boundaries = m12b.get("boundaries")
    if not isinstance(boundaries, list) or len(boundaries) != 4:
        fail("M12b must freeze exactly four lifecycle EIO scenarios")
    checkpoints = [item.get("checkpoint") for item in boundaries]
    if checkpoints != EXPECTED_M12B_CHECKPOINTS:
        fail("M12b checkpoint set/order drifted")

    for item in boundaries:
        checkpoint = item["checkpoint"]
        if item.get("control_authority_unchanged") is not True:
            fail(f"{checkpoint}: Control DB authority changed")
        if item.get("control_generation_after") != 0:
            fail(f"{checkpoint}: Control DB generation must remain zero")
        if item.get("repository_remains_unready") is not True:
            fail(f"{checkpoint}: repository unexpectedly became ready")

    if m12b.get("summary") != {
        "all_control_generations_unchanged": True,
        "all_lifecycle_operations_failed_closed": True,
        "all_repository_authority_absent": True,
        "all_requested_faults_reached": True,
        "scenario_count": 4,
    }:
        fail("M12b summary drifted")


def main() -> int:
    with EVIDENCE_PATH.open(encoding="utf-8") as handle:
        evidence = json.load(handle)

    if evidence.get("schema_version") != 1:
        fail("schema_version must remain 1")
    if evidence.get("evidence_id") != (
        "atm-snapshot-durability-runtime-evidence-v1"
    ):
        fail("evidence_id drifted")
    if evidence.get("status") != "qualification-only":
        fail("F12 evidence must remain qualification-only")
    if evidence.get("runtime_fault_qualification_complete") is not False:
        fail("F12 must not itself complete runtime fault policy")

    m12a = evidence.get("m12_runtime_authority")
    m12b = evidence.get("m12b_lifecycle_fsync_eio")
    if not isinstance(m12a, dict):
        fail("M12a frozen payload is missing")
    if not isinstance(m12b, dict):
        fail("M12b frozen payload is missing")

    validate_m12a(m12a)
    validate_m12b(m12b)

    if EXPECTED_M12A_SOURCE["atm_source_commit"] != (
        EXPECTED_M12B_SOURCE["atm_source_commit"]
    ):
        fail("M12a/M12b must be frozen from the same final source head")

    interpretation = evidence.get("interpretation")
    if not isinstance(interpretation, dict):
        fail("F12 interpretation is missing")
    for key in (
        "m12a_completed",
        "m12b_completed",
        "policy_review_ready",
        "runtime_wiring_already_selected",
    ):
        if interpretation.get(key) is not True:
            fail(f"F12 interpretation.{key} must remain true")
    if interpretation.get("runtime_fault_qualification_complete") is not False:
        fail("F12 must defer runtime qualification completion to P4")
    if interpretation.get("automatic_orphan_recovery_authorized") is not False:
        fail("F12 must not authorize orphan recovery")
    if interpretation.get("scope") != (
        "PROCESS_INTERRUPTION_PLUS_LIFECYCLE_EXACT_FSYNC_ERROR_PROPAGATION"
    ):
        fail("F12 interpretation scope drifted")

    reason = str(interpretation.get("reason", "")).lower()
    for phrase in (
        "same final source head",
        "every deterministic interruption before guarded control db publication",
        "seal-valid new generation",
        "seal mismatch fails closed",
        "real optimizations-on repositorylifecycleservice.download_or_update",
        "exact fsync eio",
        "generation 0",
        "does not claim physical power-loss durability",
        "does not retest http download",
        "does not authorize automatic orphan recovery",
        "separate p4 policy review",
    ):
        if phrase not in reason:
            fail(f"F12 review rationale lost: {phrase}")

    limitations = "\n".join(
        str(value).lower()
        for value in evidence.get("limitations", [])
    )
    for phrase in (
        "evidence freeze, not a policy-selection step",
        "m12a proves deterministic process-interruption",
        "m12b proves lifecycle error propagation",
        "m10/m11b",
        "http download",
        "authority-wide writer exclusion",
    ):
        if phrase not in limitations:
            fail(f"F12 limitation lost: {phrase}")

    print(
        "durability-v3 evidence: PASS "
        "(M12a+M12b frozen; P4 runtime qualification review still required)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
