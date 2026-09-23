# Canonical scientific artifact profile

SCI-04 introduces one AtM-owned envelope for scientific source artifacts.

The profile is deliberately narrower than a general research-object standard. It borrows the provenance principle that a derived entity should remain linked to the entity that was used to produce it, while keeping AtM's existing snapshot/source provenance and avoiding a new JSON-LD runtime dependency.

## Profile identity

- schema version: `1`
- profile: `atm-scientific-artifact/v1`
- profile version: `1`
- artifact ID: `sha256:<64 lowercase hexadecimal characters>`

## Accepted origins

### Typed evidence

Only an SCI-02 evidence atom with:

- status `TYPED_VALIDATED`
- reason `typed_validated`
- complete repository/version/snapshot/source provenance
- an AtM profile semantic type
- a non-empty raw payload

may become a canonical artifact.

The artifact preserves the structured metadata already normalized by retrieval, including relation endpoints and dataset row identity when present.

### Deterministic control evidence

Only an AtM-known control ID whose repository, source path, JSON Pointer and value type exactly match the SCI-03b control specification may become a canonical artifact.

The initial control artifact type is:

`rmd.behavioural_closure_active.control`

## Canonical digest

The artifact ID is SHA-256 over a versioned, domain-separated binary framing.

Every field is hashed in a fixed order. Each field includes:

1. the field-name length and bytes;
2. an explicit presence byte;
3. when present, the field-value length and bytes.

This distinguishes missing values from empty values and prevents delimiter ambiguity.

Numeric control values, when a future control specification admits them, use GLib's locale-independent round-trippable `g_ascii_dtostr()` representation before hashing.

The digest covers source profile identity, repository/snapshot provenance, physical and logical locators, semantic/structural metadata, payload type and payload value.

It deliberately excludes volatile state such as retrieval ranking, lexical score, UI state, session time and LLM output.

## Meaning of the artifact ID

The ID is an integrity and identity digest for the AtM artifact envelope. It is not:

- a statement that the repository claim is scientifically true;
- a substitute for the repository snapshot SHA;
- a citation by itself;
- a cryptographic signature;
- an execution permission.

Changing any canonical provenance, semantic metadata or payload field changes the artifact ID. Validation recomputes the digest and fails if a public artifact structure has been mutated after construction.

## Standards relationship

The profile uses provenance concepts compatible with W3C PROV's entity/derivation model and the research-object motivation of RO-Crate, but it is not a PROV or RO-Crate serialization.

RFC 8785 is relevant to the general need for deterministic hashable representations. AtM v1 does not claim JCS compliance: repository payload bytes are preserved exactly and the artifact envelope uses a simpler AtM-owned length-prefixed framing instead.
