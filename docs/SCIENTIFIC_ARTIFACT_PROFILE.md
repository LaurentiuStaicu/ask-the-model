# Canonical Scientific Artifact Profile v1

SCI-04 defines the canonical identity layer used by later scientific-result artifacts. It does not discover evidence, execute scientific operations, or generate conversational text.

## Three different identities

AtM keeps three identities separate.

### ScientificContentId

`ScientificContentId` identifies scientific content independent of retrieval route, query wording, UI, timestamps, snapshot provenance, and storage formatting.

For JSON evidence, AtM parses the payload and hashes an AtM-owned typed binary representation under the domain `ATM-SCIENTIFIC-CONTENT-v1\0`. Object member names are sorted deterministically; whitespace and input object-key order therefore do not affect the ID. Array order remains semantic.

For scalar control evidence, the canonical scalar type and value are hashed with the AtM-owned artifact class.

### QualifiedArtifactId

`QualifiedArtifactId` binds a `ScientificContentId` to the qualification information available at this layer:

- repository identity/version;
- pinned snapshot SHA;
- exact source path and locator;
- logical source identity when applicable;
- semantic profile identity/version when applicable.

It uses the separate domain `ATM-QUALIFIED-ARTIFACT-v1\0`.

A snapshot change therefore changes `QualifiedArtifactId` even if `ScientificContentId` remains identical.

SCI-05/SCI-06 extend this same qualification concept at result level with canonical obligations, evidence/control sets, operation versions, and numeric/execution profile.

### StorageDigest

`StorageDigest` is SHA-256 over the complete AtM v1 stored artifact representation, including exact stored payload bytes and the two scientific identities. It uses `ATM-STORAGE-DIGEST-v1\0`.

Consequently, non-semantic JSON whitespace/key-order changes may alter `StorageDigest` while leaving `ScientificContentId` and `QualifiedArtifactId` unchanged.

None of these hashes is a digital signature.

## Canonical numeric values

SCI-04 also defines the numeric primitives needed by later SRA and operation layers.

Exact decimal text is normalized to a signed integer coefficient and base-10 exponent. For example:

- `7.005760432822278` → coefficient `7005760432822278`, exponent `-15`
- `0.8300` → coefficient `83`, exponent `-2`

Zero has the single representation coefficient `0`, exponent `0`.

Binary64 values use their exact IEEE-754 bit pattern. Non-finite values are rejected. This avoids locale- or presentation-dependent numeric identity.

## Admission boundary

A canonical input artifact may be created only from:

1. an `AtmScientificEvidenceAtom` whose status is `TYPED_VALIDATED`; or
2. a recognized deterministic `AtmScientificControlValue` whose fixed AtM-owned control contract is revalidated.

The artifact validator recomputes all three identities and rejects mutation or incomplete provenance.
