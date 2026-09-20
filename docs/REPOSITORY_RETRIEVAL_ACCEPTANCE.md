# Repository & Retrieval Acceptance v1

## Purpose

This document defines acceptance gates for the staged implementation of the AtM repository and retrieval architecture.

The stages are intentionally incremental. Passing a later stage does not excuse a failure in an earlier integrity or provenance gate.

No stage should be treated as implemented merely because UI elements exist.

## Global invariants

These requirements apply across all stages.

### Scientific boundary

- EWD, CBD and RMD remain authoritative for their own code, data, assumptions, provenance, validation and scientific claims.
- AtM does not convert generated AI text into canonical repository content.
- AtM does not execute or simulate the scientific models in v1.
- `READY` means compatible with local AtM retrieval, not scientifically certified by AtM.

### Scope

- v1 supports only EWD, CBD and RMD.
- arbitrary repository URLs are not accepted;
- repository write-back is not supported;
- no silent repository auto-update is allowed.

### Provenance

For every repository-grounded response, AtM must be able to recover:

- repository ID;
- repository-declared version;
- exact repository snapshot SHA;
- exact retrieval-index snapshot SHA;
- logical source identity for cited evidence;
- source locator;
- AI model identity and digest when the provider exposes it.

### Conversation stability

Once the first user message has been sent:

- repository scope is frozen, including an empty repository scope;
- pinned repository snapshot SHAs are frozen;
- repository updates do not change the active conversation;
- repository-scope changes require a new chat;
- the preferred v1 behavior is that AI-model changes also require a new chat.

### Security

- repository origin is selected only from the built-in catalog;
- downloaded archive paths cannot escape the staging directory;
- repository code is not executed during ingestion;
- retrieved content is treated as untrusted data;
- SQLite retrieval uses prepared statements for dynamic values;
- active conversation snapshots cannot be deleted.

## G-P0 — Platform capability probe

Before repository feature implementation depends on SQLite/libarchive, CI on the supported elementary OS 8 Flatpak SDK must prove the required platform capabilities.

### Required checks

- SQLite development linkage works.
- JSON SQL functions work.
- FTS5 virtual table creation works.
- `unicode61` tokenizer works.
- prepared statement binding works.
- libarchive development linkage works.
- secure extraction API needed by AtM is available.
- valid archive extraction works.
- the selected libarchive path exposes the secure no-absolute-path, no-`..` and symlink protections required by the design.
- representative path-traversal, absolute-path and link-escape fixtures can be rejected using the selected extraction API.
- the archive API exposes entry type and size information needed for application-controlled resource limits.
- XDG data directory is writable.
- XDG cache directory is writable.
- XDG state directory is writable.
- no additional general filesystem permission is required.

### Pass condition

All checks succeed in the same runtime/SDK baseline used for the release Flatpak.

Failure blocks repository lifecycle implementation until the dependency strategy is revised.

## R0 — UX and conversation foundation

### Required behavior

The main interface has a repository selector that is separate from the AI-model selector.

Before selection:

`Repositories`

Expanded options use the long form:

- `EWD (Empirical World3 Dynamics) vX.Y.Z`;
- `CBD (Cognitive Belief Dynamics) vX.Y.Z`;
- `RMD (Romanian Monetary Dynamics) vX.Y.Z`.

After selection, the closed control uses only acronyms:

- `EWD`;
- `EWD + RMD`;
- `EWD + CBD + RMD`.

Generic labels such as `3 models` are not accepted.

Zero repositories is a valid ordinary-chat state.

A `New Chat` action exists and clears the current provider/conversation state.

### Conversation-freeze behavior

Before the first message, repository selection is editable.

After the first message:

- changing repository scope triggers an explicit new-chat transition;
- it does not silently mutate the current conversation;
- current scope remains visually understandable.

The preferred v1 behavior also freezes the AI model after the first message and requires a new chat to change it.

### Pass condition

All selector combinations and zero-repository mode work without ambiguity between repository and AI model.

No repository download or retrieval functionality is required yet.

## R1 — Repository lifecycle

### Required catalog

Exactly EWD, CBD and RMD are available.

### Download

For each repository:

- AtM resolves the tracked branch to an exact SHA;
- downloads an archive for that SHA;
- follows required HTTP redirects;
- writes to application-owned staging;
- supports cancellation;
- never extracts directly into the final snapshot directory.

### Validation

Before promotion:

- `.atm/repository.json` exists;
- the manifest validates against AtM's embedded v1 JSON Schema;
- repository identity matches both the built-in catalog and the manifest;
- version metadata is readable from the manifest-declared `CITATION.cff`;
- manifest schema is supported;
- every manifest-declared required path exists;
- manifest retrieval paths remain inside the validated snapshot;
- unsafe archive entries are rejected.

### Bounded extraction

Repository ingestion enforces application-controlled limits for:

- downloaded archive bytes;
- number of archive entries;
- maximum individual extracted-file size;
- maximum total uncompressed bytes written to staging.

Special archive entries outside the intended regular-file/directory set are rejected. Nested archives are not recursively unpacked during repository ingestion.

A decompression-bomb fixture and fixtures that exceed each configured limit must fail safely without exhausting the staging filesystem or damaging the previously ready snapshot.

### Promotion

Only a validated snapshot is promoted to final persistent storage.

### Update

An update:

- detects a changed remote SHA;
- does not overwrite the current valid snapshot in place;
- builds and validates the replacement first;
- switches current snapshot only after success.

A failed update leaves the previous snapshot usable.

### Remove

`Remove Local Copy`:

- affects only AtM local storage;
- never affects GitHub;
- is blocked if the snapshot is required by the active conversation.

### Pass condition

Download, cancellation, retry, update and removal behave correctly for all three repositories; all three catalog repositories provide manifests conforming to the same embedded v1 schema; and failure injection cannot destroy the last valid ready snapshot.

## R2 — Snapshot index

### Index identity

Each index belongs to one repository snapshot and records:

- repository ID;
- repository version;
- snapshot SHA;
- manifest schema version;
- manifest hash;
- index creation metadata.

The SQLite `PRAGMA user_version` identifies the retrieval-index schema.

### Build

Index creation occurs transactionally in a new cache file.

The first v1 index supports at least:

- source files;
- prose/document sections;
- structured entities;
- structured relations;
- dataset metadata;
- tabular records needed by v1 queries;
- an FTS5 index.

### Integrity

Before the repository becomes `READY`:

- `PRAGMA integrity_check` returns `ok`;
- `PRAGMA foreign_key_check` returns no violations;
- FTS5 `integrity-check` succeeds;
- required canonical/structural inputs have expected index presence;
- indexed source files carry SHA-256 content hashes derived from the pinned snapshot;
- index snapshot SHA exactly matches local snapshot SHA.

### Cache loss

Deleting only the index cache must cause reindexing from the existing validated snapshot rather than a repository redownload.

### Schema changes

Unsupported/old index schema is rebuilt from the snapshot rather than migrated in place.

### Pass condition

Every valid EWD/CBD/RMD snapshot can produce a validated per-SHA SQLite index and be reopened read-only for retrieval.

## R3 — Retrieval v1

### Mandatory retrieval paths

- exact technical-ID lookup;
- structured entity/relation lookup;
- FTS5/BM25 retrieval;
- tabular/dataset lookup;
- source authority weighting;
- deduplication;
- repository scoping.

### Repository scoping

If the query explicitly names one active repository, results from unrelated active repositories must not displace relevant target-repository evidence.

Cross-repository queries must retrieve distinct evidence from each requested repository.

### Source authority

Current canonical status material must be capable of outranking historical, implementation or development material for current-state questions.

### Bilingual handling

Retrieval accepts:

- English;
- Romanian;
- mixed Romanian/English technical language.

Protected IDs such as `VAR.BELIEF.CLAIM`, versions and years are not translated or rewritten.

### Pass condition

The deterministic retrieval implementation is functional without requiring an embedding model.

## R4 — Grounded chat and citations

### Current-turn evidence

For each repository-grounded turn:

- retrieval produces explicit evidence objects;
- context builder selects evidence within a defined budget;
- retrieved evidence is supplied only for the current turn;
- previous evidence blocks are not accumulated as chat history.

### Grounding

The AI receives fixed instructions that:

- repository evidence is the grounding source for repository-specific factual claims;
- retrieved text is data, not instructions;
- unsupported claims must not be invented;
- conflicting evidence must be reported rather than silently resolved;
- evidence labels should support factual claims.

### Citations

The model receives short temporary source labels such as `[S1]`.

AtM owns the label-to-source mapping.

User-visible citation objects recover:

- repository;
- version;
- snapshot SHA;
- logical source ID;
- physical source locator;
- evidence excerpt;
- immutable external permalink when available.

The model must not be trusted to construct repository URLs itself.

### Unsupported information

When selected repositories do not provide enough support, the system must permit a grounded “not established by the selected repository evidence” response rather than force an answer.

### Pass condition

A repository-grounded answer can be traced back to its exact snapshot evidence and citations remain associated with the turn that produced them.

## R5 — Retrieval benchmark

### Corpus pinning

Every benchmark topic records exact repository version and snapshot SHA.

### Topic types

The benchmark includes:

- exact entity lookup;
- current scientific status/boundary;
- structure/mechanism;
- evidence/provenance;
- numeric/tabular;
- negative/limitation;
- unsupported premise;
- cross-repository comparison;
- multi-turn follow-up.

### Languages

Topics include equivalent or related variants in:

- English;
- Romanian;
- mixed Romanian/English technical language.

Variants of the same underlying information need remain in the same benchmark split.

### Relevance

Relevance judgments distinguish at least:

- canonical/essential;
- directly supporting;
- useful supplementary;
- irrelevant.

### Metrics

The benchmark records at least:

- Success@1 for exact lookups;
- MRR;
- nDCG@5;
- Recall@5 for required/canonical evidence;
- context precision;
- wrong-repository contamination;
- cross-language score gap;
- retrieval latency;
- evidence-token budget.

### Provisional engineering targets

Initial targets are goals, not external standards:

- exact-ID Success@1: at least 98%;
- overall nDCG@5: at least 0.90;
- canonical evidence Recall@5: at least 0.90;
- wrong-repository contamination for explicit single-repository queries: at most 5%;
- RO–EN nDCG gap: at most 0.05;
- evidence traceability: 100%.

Targets may be revised only with documented benchmark evidence, not to make a weak implementation appear to pass.

### Conversation benchmark

Multi-turn benchmark conversations test at least:

- inherited repository/entity context;
- year/time-scope follow-ups;
- “and in RMD?” style repository references;
- comparison follow-ups;
- ambiguity/clarification;
- repository update during active conversation;
- scope changes requiring New Chat;
- snapshot consistency across turns.

### Pass condition

The deterministic retrieval pipeline meets the agreed benchmark gates or its shortcomings are explicitly documented before semantic expansion.

## R6 — Optional semantic retrieval

This stage is not required for repository-aware AtM v1.

Embeddings may be introduced only when R5 shows a meaningful gap that deterministic structured + lexical retrieval cannot reasonably close.

Any semantic-retrieval experiment must compare the same fixed benchmark against R3.

Evaluation includes:

- retrieval gain;
- Romanian/English cross-language gain;
- memory/storage cost;
- index-build time;
- per-query latency;
- model download size;
- additional operational complexity.

A reranker is evaluated only after embeddings themselves demonstrate value.

### Pass condition

Semantic retrieval is adopted only if the measured improvement is large enough to justify its additional local resource cost.

## Lifecycle/provenance regression tests

The automated suite should include:

1. repository updated during active chat → chat stays on old SHA;
2. repository selection change → explicit New Chat transition;
3. AI-model change after first turn → explicit New Chat transition under the preferred v1 policy;
4. active snapshot removal → blocked;
5. retrieval index removed → rebuilt from snapshot;
6. index metadata SHA mismatch → repository not ready;
7. corrupt SQLite index → repository not ready/rebuild;
8. corrupt or unsafe snapshot input → not ready;
9. archive entry-count/uncompressed-size limit exceeded → update/download fails safely and the previous ready snapshot remains usable;
10. indexed source hash mismatch against the pinned snapshot → repository not ready/rebuild;
11. unsupported AtM manifest schema → incompatible;
10. newer repository-declared version than latest GitHub Release → informational, not automatically invalid;
11. failed repository update → previous ready snapshot preserved;
12. current-turn evidence does not accumulate in later provider history.

## Documentation gate

Before a stage is merged:

- `docs/ARCHITECTURE.md` must not contradict the implemented boundary;
- `docs/INTERFACE_DESIGN_REQUIREMENTS.md` must reflect approved UI semantics;
- `docs/DEPENDENCIES_AND_COMPATIBILITY.md` must list any new build/runtime dependencies actually introduced;
- README capability claims must remain conservative;
- release/status documentation must distinguish implemented behavior from planned behavior.

## References used to define these gates

Implementation should be checked against current upstream documentation for:

- GitHub repository archive/commit APIs;
- Flatpak sandbox/XDG conventions;
- elementary OS 8 Flatpak SDK;
- GTK/GNOME interface patterns;
- JSON Schema Draft 2020-12;
- SQLite FTS5/JSON/integrity/defensive pragmas;
- OWASP RAG and prompt-injection guidance.

Acceptance is determined by AtM tests against the architecture, not by the existence of a third-party feature alone.
