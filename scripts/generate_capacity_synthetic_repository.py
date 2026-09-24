#!/usr/bin/env python3

import argparse
import csv
import json
import pathlib


SCENARIOS = {
    "ewd-csv-50k": {
        "repository_id": "ewd",
        "acronym": "EWD",
        "display_name": "Empirical World3 Dynamics",
        "kind": "csv",
        "rows": 50_000,
    },
    "ewd-csv-150k": {
        "repository_id": "ewd",
        "acronym": "EWD",
        "display_name": "Empirical World3 Dynamics",
        "kind": "csv",
        "rows": 150_000,
    },
    "cbd-json-25k": {
        "repository_id": "cbd",
        "acronym": "CBD",
        "display_name": "Cognitive Belief Dynamics",
        "kind": "json",
        "records": 25_000,
    },
    "cbd-json-75k": {
        "repository_id": "cbd",
        "acronym": "CBD",
        "display_name": "Cognitive Belief Dynamics",
        "kind": "json",
        "records": 75_000,
    },
    "rmd-markdown-4m": {
        "repository_id": "rmd",
        "acronym": "RMD",
        "display_name": "Romanian Monetary Dynamics",
        "kind": "markdown",
        "target_bytes": 4 * 1024 * 1024,
    },
    "rmd-markdown-12m": {
        "repository_id": "rmd",
        "acronym": "RMD",
        "display_name": "Romanian Monetary Dynamics",
        "kind": "markdown",
        "target_bytes": 12 * 1024 * 1024,
    },
}


def write_citation(root: pathlib.Path, display_name: str) -> None:
    (root / "CITATION.cff").write_text(
        """cff-version: 1.2.0
message: "Synthetic capacity qualification fixture."
title: "%s"
version: "0.1.0"
authors:
  - family-names: "AtM"
    given-names: "Qualification"
""" % display_name,
        encoding="utf-8",
    )


def write_status(root: pathlib.Path, scenario: str) -> None:
    (root / "STATUS.md").write_text(
        "# Synthetic Capacity Qualification\n\n"
        f"Scenario: {scenario}\n\n"
        "This repository is generated only inside CI to measure retrieval-index "
        "storage amplification. It is not scientific evidence.\n",
        encoding="utf-8",
    )


def manifest_for(config: dict, payload_path: str) -> dict:
    retrieval = {
        "canonical": ["STATUS.md", "CITATION.cff"],
        "structural": [],
        "evidence": [],
        "tabular": [],
        "implementation": [],
        "exclude": [".github", "__pycache__"],
    }

    if config["kind"] == "csv":
        retrieval["tabular"] = [payload_path]
        retrieval["evidence"] = [payload_path]
    else:
        retrieval["evidence"] = [payload_path]

    return {
        "schema_version": 1,
        "repository_id": config["repository_id"],
        "acronym": config["acronym"],
        "display_name": config["display_name"],
        "version_source": {
            "type": "cff",
            "path": "CITATION.cff",
        },
        "status_source": "STATUS.md",
        "required_paths": [
            "CITATION.cff",
            "STATUS.md",
            payload_path,
        ],
        "retrieval": retrieval,
    }


def write_manifest(
    root: pathlib.Path,
    config: dict,
    payload_path: str,
) -> None:
    atm = root / ".atm"
    atm.mkdir(parents=True, exist_ok=True)
    (atm / "repository.json").write_text(
        json.dumps(
            manifest_for(config, payload_path),
            indent=2,
            sort_keys=True,
        ) + "\n",
        encoding="utf-8",
    )


def write_csv(root: pathlib.Path, rows: int) -> str:
    relative = "data/synthetic_capacity.csv"
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(
            ["id", "category", "label", "evidence_text"]
        )
        for i in range(rows):
            writer.writerow(
                [
                    i,
                    f"sector-{i % 97:02d}",
                    f"observation-{i:06d}",
                    (
                        f"unique_token_{i:06d} "
                        f"series_{i % 997:03d} "
                        "synthetic evidence row for retrieval capacity "
                        "full text indexing"
                    ),
                ]
            )

    return relative


def write_json(root: pathlib.Path, records: int) -> str:
    relative = "model/synthetic_entities.json"
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", encoding="utf-8") as handle:
        handle.write('{"entities":[')
        for i in range(records):
            if i:
                handle.write(",")
            json.dump(
                {
                    "id": f"entity-{i:06d}",
                    "type": "synthetic_capacity_entity",
                    "label": f"Synthetic Entity {i:06d}",
                    "description": (
                        f"unique_token_{i:06d} "
                        f"belief_{i % 997:03d} "
                        "synthetic structured evidence for retrieval "
                        "capacity full text indexing"
                    ),
                    "value": i,
                },
                handle,
                separators=(",", ":"),
            )
        handle.write("]}\n")

    return relative


def write_markdown(root: pathlib.Path, target_bytes: int) -> str:
    relative = "model/synthetic_capacity.md"
    path = root / relative
    path.parent.mkdir(parents=True, exist_ok=True)

    with path.open("w", encoding="utf-8") as handle:
        handle.write("# Synthetic Capacity Markdown\n\n")
        i = 0
        while handle.tell() < target_bytes:
            handle.write(
                f"## Section {i:06d}\n\n"
                f"unique_token_{i:06d} "
                f"monetary_flow_{i % 997:03d} "
                f"sector_balance_{i % 499:03d} "
                "synthetic markdown evidence designed to exercise "
                "section storage and FTS5 token indexing without "
                "claiming scientific meaning.\n\n"
            )
            i += 1

    return relative


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--scenario", choices=sorted(SCENARIOS), required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()

    config = SCENARIOS[args.scenario]
    root = args.output

    if root.exists():
        raise SystemExit(f"output already exists: {root}")

    root.mkdir(parents=True)

    write_citation(root, config["display_name"])
    write_status(root, args.scenario)

    if config["kind"] == "csv":
        payload = write_csv(root, config["rows"])
    elif config["kind"] == "json":
        payload = write_json(root, config["records"])
    else:
        payload = write_markdown(root, config["target_bytes"])

    write_manifest(root, config, payload)

    payload_size = (root / payload).stat().st_size
    metadata = {
        "scenario": args.scenario,
        "repository_id": config["repository_id"],
        "kind": config["kind"],
        "payload_path": payload,
        "payload_bytes": payload_size,
    }

    (root / "synthetic-capacity-metadata.json").write_text(
        json.dumps(metadata, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )

    print(json.dumps(metadata, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
