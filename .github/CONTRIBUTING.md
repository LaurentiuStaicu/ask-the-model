# Contributing to Ask the Model (AtM)

Thank you for considering a contribution.

AtM is a local-first desktop interface. It is **not** the AI provider, does not contain the AI model files, and is not itself one of the scientific dynamical models it is intended to help explore.

## Before contributing

Please read:

- [README.md](../README.md) for first-use and capability boundaries;
- [STATUS.md](../STATUS.md) for the current application state;
- [docs/DEPENDENCIES_AND_COMPATIBILITY.md](../docs/DEPENDENCIES_AND_COMPATIBILITY.md) for provider/runtime compatibility;
- [releases/](../releases/) for released behavior.

## Useful contribution areas

- UI/UX and accessibility;
- Flatpak/build reliability;
- local provider discovery and compatibility;
- model discovery/capability filtering;
- streaming/chat behavior;
- documentation and troubleshooting;
- future repository-aware retrieval/provenance work.

## Boundary rules

Contributions must preserve these distinctions unless a deliberate design change is reviewed:

- AtM provides the interface;
- the provider runs models and owns inference/GPU behavior;
- AI models are separate artifacts with their own licenses;
- scientific repositories remain canonical for their code, data, assumptions, provenance and validation;
- AI-generated text is not automatically an authoritative scientific-model result.

## Verification

Every pull request should allow the repository's **Flatpak** GitHub Actions workflow to pass.

For changes affecting provider/model behavior, report the provider, endpoint behavior and representative compatible/incompatible model when relevant.

For UI changes, describe the intended behavior under both light and dark appearance where applicable.

## Version and release changes

Do not change the application version as part of an unrelated contribution.

The main workflow can publish a GitHub Release when a versioned release-note file exists. A version bump therefore requires deliberate release preparation, consistent metadata and reviewed release notes. The required synchronized surfaces are defined in [.github/release_metadata_contract.json](release_metadata_contract.json) and validated before every Flatpak build.

## Reporting problems

Use the repository issue forms for reproducible application/build problems or provider/model compatibility concerns.

Do **not** publish sensitive vulnerability details in a normal public issue.
