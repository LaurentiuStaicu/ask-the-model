# Contributing to Ask the Model (AtM)

Thank you for considering a contribution.

AtM is a local-first desktop interface and retrieval/provenance layer. It is **not** the AI provider, does not own external AI-model files, and is not itself one of the scientific dynamical models it helps explore.

## Start here

Please read:

- [README.md](../README.md) for user-facing capabilities;
- [STATUS.md](../STATUS.md) for the current release boundary;
- [docs/DEVELOPMENT_GUIDE.md](../docs/DEVELOPMENT_GUIDE.md) for code map, invariants and extension points;
- [docs/USER_INTERFACE_GUIDE.md](../docs/USER_INTERFACE_GUIDE.md) for the current GTK surface;
- [docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md](../docs/REPOSITORY_RETRIEVAL_ARCHITECTURE.md) for repository/retrieval design;
- [docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md](../docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md) for acceptance gates;
- [docs/DEPENDENCIES_AND_COMPATIBILITY.md](../docs/DEPENDENCIES_AND_COMPATIBILITY.md) for runtime/build compatibility.

## Useful contribution areas

- UI/UX and accessibility;
- Flatpak/build reliability;
- local provider discovery and compatibility;
- model discovery/capability filtering;
- chat/streaming and multi-chat behavior;
- repository lifecycle and snapshot validation;
- deterministic retrieval/ranking;
- citation/provenance presentation;
- tests, benchmark diagnostics and documentation;
- future persisted conversation/repository-management/provider work.

## Boundary rules

Contributions must preserve these distinctions unless a deliberate architecture change is reviewed:

- AtM provides the interface and retrieval/provenance layer;
- the external provider runs AI models and owns inference/GPU behavior;
- AI model files are separate artifacts with their own licenses;
- EWD, CBD and RMD remain canonical for their scientific content and validation;
- READY means AtM-compatible local snapshot/index, not scientific certification;
- repository-grounded evidence is tied to an exact immutable snapshot SHA;
- retrieved repository text is untrusted data, not behavioral instructions;
- AI-generated text is not automatically an authoritative scientific-model result.

## Repository/retrieval invariants

Changes in this area should not silently weaken:

1. exact SHA identity;
2. fail-closed refresh/update behavior;
3. immutable validated snapshots;
4. regenerable indexes;
5. first-Send per-chat model/repository freeze;
6. current-turn-only grounding evidence;
7. narrow Flatpak filesystem permissions;
8. deterministic benchmark/provenance contracts.

If a change intentionally alters one of these, explain the reason and update architecture, acceptance, tests and release documentation in the same pull request.

## Verification

Every pull request should allow the repository's **Flatpak** GitHub Actions workflow to pass.

For provider/model changes, report the provider, endpoint behavior and representative compatible/incompatible model when relevant.

For repository/retrieval changes, report the fixture or exact repository SHAs used and add regression coverage where practical.

For UI changes, describe the target GTK/elementary environment and verify both light/dark appearance where relevant.

For citation/provenance changes, verify that the user-visible source still identifies repository, version, exact snapshot SHA and source locator.

## Version and release changes

Do not change the application version as part of an unrelated contribution.

A version bump is a deliberate release operation. The synchronized release surfaces are defined in [.github/release_metadata_contract.json](release_metadata_contract.json) and validated before every Flatpak build.

A capability-changing release should update:

- user-facing README/STATUS;
- CHANGELOG and versioned release notes;
- AppStream/CITATION metadata;
- relevant interface/developer documentation;
- regression and acceptance contracts where needed.

## Pull-request scope

Prefer focused pull requests. Avoid combining unrelated UI, retrieval, release and scientific-repository changes unless the release itself requires atomic alignment.

Use the pull-request template to record change class, verification and release impact.

## Reporting problems

Use the repository issue forms for:

- reproducible application/build/UI problems;
- provider/model compatibility;
- repository lifecycle, grounding, retrieval or provenance problems.

Do **not** publish sensitive vulnerability details in a normal public issue.
