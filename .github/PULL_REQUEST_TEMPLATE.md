## Summary

Describe the change and why it is needed.

## Change class

- [ ] UI / accessibility
- [ ] Flatpak / build / packaging
- [ ] Provider discovery / compatibility
- [ ] Model discovery / capability handling
- [ ] Chat / streaming / multi-chat
- [ ] Repository lifecycle / snapshot validation
- [ ] Retrieval / grounding / provenance
- [ ] Documentation
- [ ] Release / versioning

## Boundary checklist

- [ ] I did not treat AtM as the AI provider.
- [ ] I did not imply that AtM bundles or owns external AI models unless this PR explicitly implements that feature.
- [ ] I preserved exact repository snapshot/provenance identity for grounded evidence, or documented a reviewed architecture change.
- [ ] I did not present AI-generated output as a canonical EWD/CBD/RMD result without repository-grounded provenance or actual model execution.
- [ ] I updated documentation if the implemented capability boundary changed.
- [ ] I did not change the version unless this is an authorized release change.

## Verification

Describe the checks performed. The Flatpak GitHub Actions workflow should pass.

For repository/retrieval changes, include fixtures or exact SHAs when relevant.

For UI changes, include the target GTK/Linux environment and visual behavior checked.

## Release impact

- [ ] No version bump required.
- [ ] Release documentation/metadata update required.
- [ ] New minor/patch release preparation required.
