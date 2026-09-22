#!/usr/bin/env python3
"""Validate the AtM invariant registry without third-party dependencies."""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

ID_RE = re.compile(r"^[A-Z][A-Z0-9-]*-(S0|S1)-[0-9]{3}$")
CATEGORIES = {
    "STARTUP",
    "REPOSITORY",
    "INGEST",
    "RETRIEVAL",
    "SESSION",
    "SCIENTIFIC",
    "STATE",
    "TRUST",
    "AUDIT",
    "RECOVERY",
    "LIFECYCLE",
    "RESOURCE",
    "RELEASE",
}
LIFECYCLES = {"enforced", "planned", "retired"}
SEVERITIES = {"S0", "S1"}


def fail(message: str) -> None:
    raise ValueError(message)


def load_json(path: Path) -> object:
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"{path}: {exc}")


def require_nonempty_string(value: object, label: str) -> str:
    if not isinstance(value, str) or not value.strip():
        fail(f"{label} must be a non-empty string")
    return value


def require_string_list(value: object, label: str) -> list[str]:
    if not isinstance(value, list):
        fail(f"{label} must be an array")
    result: list[str] = []
    for index, item in enumerate(value):
        result.append(require_nonempty_string(item, f"{label}[{index}]"))
    if len(set(result)) != len(result):
        fail(f"{label} contains duplicate entries")
    return result


def validate_file_reference(repo_root: Path, reference: str, invariant_id: str) -> None:
    path_text = reference.split("#", 1)[0]
    path = repo_root / path_text
    if not path.is_file():
        fail(f"{invariant_id}: enforcement reference does not exist: {reference}")


def validate_test_reference(meson_text: str, reference: str, invariant_id: str) -> None:
    if not reference.startswith("meson:"):
        fail(f"{invariant_id}: unsupported test reference: {reference}")
    test_name = reference.removeprefix("meson:")
    if not test_name:
        fail(f"{invariant_id}: empty Meson test reference")
    if f"'{test_name}'" not in meson_text:
        fail(f"{invariant_id}: Meson test not found: {test_name}")


def validate_registry(repo_root: Path, registry_path: Path, schema_path: Path) -> None:
    registry = load_json(registry_path)
    load_json(schema_path)

    if not isinstance(registry, dict):
        fail("registry root must be an object")
    if registry.get("schema_version") != 1:
        fail("schema_version must be 1")
    if registry.get("registry_id") != "atm-invariants/v1":
        fail("registry_id must be atm-invariants/v1")
    require_nonempty_string(registry.get("scope"), "scope")

    policy = registry.get("qualification_policy")
    if not isinstance(policy, dict):
        fail("qualification_policy must be an object")
    if policy.get("blocking_severities") != ["S0", "S1"]:
        fail("blocking_severities must be exactly [S0, S1]")
    if policy.get("scoring") != "none":
        fail("qualification scoring must be 'none'")

    invariants = registry.get("invariants")
    if not isinstance(invariants, list) or not invariants:
        fail("invariants must be a non-empty array")

    meson_path = repo_root / "meson.build"
    meson_text = meson_path.read_text(encoding="utf-8")

    seen_ids: set[str] = set()
    severity_counts = {"S0": 0, "S1": 0}
    lifecycle_counts = {name: 0 for name in LIFECYCLES}

    for index, entry in enumerate(invariants):
        label = f"invariants[{index}]"
        if not isinstance(entry, dict):
            fail(f"{label} must be an object")

        invariant_id = require_nonempty_string(entry.get("id"), f"{label}.id")
        match = ID_RE.fullmatch(invariant_id)
        if match is None:
            fail(f"{invariant_id}: invalid invariant id")
        if invariant_id in seen_ids:
            fail(f"duplicate invariant id: {invariant_id}")
        seen_ids.add(invariant_id)

        category = require_nonempty_string(entry.get("category"), f"{invariant_id}.category")
        if category not in CATEGORIES:
            fail(f"{invariant_id}: unsupported category: {category}")

        severity = require_nonempty_string(entry.get("severity"), f"{invariant_id}.severity")
        if severity not in SEVERITIES:
            fail(f"{invariant_id}: unsupported severity: {severity}")
        if severity != match.group(1):
            fail(f"{invariant_id}: severity field does not match invariant id")
        severity_counts[severity] += 1

        lifecycle = require_nonempty_string(entry.get("lifecycle"), f"{invariant_id}.lifecycle")
        if lifecycle not in LIFECYCLES:
            fail(f"{invariant_id}: unsupported lifecycle: {lifecycle}")
        lifecycle_counts[lifecycle] += 1

        require_nonempty_string(entry.get("statement"), f"{invariant_id}.statement")
        enforced_by = require_string_list(entry.get("enforced_by"), f"{invariant_id}.enforced_by")
        tested_by = require_string_list(entry.get("tested_by"), f"{invariant_id}.tested_by")

        if lifecycle == "enforced" and (not enforced_by or not tested_by):
            fail(f"{invariant_id}: enforced invariants require enforcement and test references")

        for reference in enforced_by:
            validate_file_reference(repo_root, reference, invariant_id)
        for reference in tested_by:
            validate_test_reference(meson_text, reference, invariant_id)

    print(
        "Invariant registry OK: "
        f"{len(invariants)} invariants; "
        f"S0={severity_counts['S0']}, S1={severity_counts['S1']}; "
        f"enforced={lifecycle_counts['enforced']}, "
        f"planned={lifecycle_counts['planned']}, "
        f"retired={lifecycle_counts['retired']}."
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--registry",
        default="qualification/invariants-v1.json",
        type=Path,
    )
    parser.add_argument(
        "--schema",
        default="data/schemas/invariant-registry-v1.schema.json",
        type=Path,
    )
    args = parser.parse_args()

    repo_root = Path(__file__).resolve().parent.parent
    registry_path = args.registry if args.registry.is_absolute() else repo_root / args.registry
    schema_path = args.schema if args.schema.is_absolute() else repo_root / args.schema

    try:
        validate_registry(repo_root, registry_path, schema_path)
    except (ValueError, OSError) as exc:
        print(f"Invariant registry validation failed: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
