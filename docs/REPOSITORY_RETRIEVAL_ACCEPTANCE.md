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
- the Flatpak manifest grants exactly the dedicated `~/Ask the Model:create` repository-storage permission;
- `--filesystem=home`, `--filesystem=host` and any additional broad filesystem permission remain absent.

### Pass condition

All checks succeed in the same runtime/SDK baseline used for the release Flatpak.

Failure blocks repository lifecycle implementation until the dependency strategy is revised.

## G-S0 — Startup qualification and clean-install reconciliation

This gate is distinct from G-P0.

G-P0 proves in CI that the supported Flatpak SDK/runtime baseline exposes the
capabilities required by the repository implementation. G-S0 qualifies the
state of the installation that is actually running and reconciles local
repository state without contacting the network.

Passing G-P0 does not imply that a particular installed deployment passes
G-S0.

### Execution identity

For a packaged Flatpak deployment, startup qualification reads the effective
sandbox metadata from `/.flatpak-info` and records at least:

- application ID/ref;
- application commit;
- runtime ref;
- runtime commit;
- architecture/branch information exposed by the deployment;
- application extensions;
- runtime extensions;
- Flatpak version exposed by the instance metadata.

The application ID must match
`io.github.laurentiustaicu.ask_the_model`. The runtime must match the
supported elementary OS 8 runtime family documented by the release.

Missing, malformed or contradictory deployment identity is a platform
qualification failure.

A development execution with no usable Flatpak instance metadata must be
classified explicitly as an unpackaged/development execution. It must never
be represented as a qualified release installation merely because the binary
starts successfully.

### Platform fingerprint

AtM computes a deterministic platform fingerprint from the canonicalized
deployment identity and the startup-qualification policy/schema version.

The fingerprint includes the exact application/runtime refs and commits plus
sorted application/runtime extension identities. Observed Flatpak-version
metadata is recorded for diagnostics; changing the qualification policy or a
deployment identity component requires requalification.

The fingerprint is a local comparison key, not a replacement for Flatpak
repository signature verification or external build/release attestation.

### Local storage boundary

The visible repository root remains:

`~/Ask the Model`

Security-sensitive qualification of this path must fail closed if the root is
a symlink or is not a real directory owned by the current user.

The qualification implementation must use descriptor-based no-follow checks
for the security boundary rather than a check-then-open path test. It must
also verify that the directory is not writable by group/other users and prove
application write capability using an application-owned temporary probe that
is removed after the check.

A storage failure blocks repository-qualified operation but must not turn a
partially inspected repository into READY.

### Repository-state load result

Loading persistent repository state must expose an explicit diagnostic result
instead of silently making corrupt state indistinguishable from a clean
installation.

At minimum, the state-load result distinguishes:

- `ABSENT` — no prior repository state exists;
- `VALID` — state schema and recorded entries are valid;
- `INVALID` — state exists but cannot be accepted.

Invalid state is preserved for diagnosis/recovery unless a separate explicit
repair operation is defined. Startup qualification must not guess the
"latest" snapshot from directory names and must not silently repin a
repository after state corruption.

### Per-repository offline reconciliation

EWD, CBD and RMD are reconciled independently. A failure in one repository
must not prevent a different valid repository from becoming locally usable.

Startup reconciliation performs no remote Refresh and no Download/Update.

For each catalog repository:

1. no valid state entry means `NOT_INSTALLED`;
2. state that names a missing snapshot means `SNAPSHOT_MISSING`;
3. a snapshot path that is not a real non-symlink directory means
   `SNAPSHOT_INVALID`;
4. a local snapshot is validated against the built-in catalog and repository
   manifest contract before it can be READY;
5. the validated repository version must match the persisted version;
6. the retrieval index is validated for that exact snapshot SHA;
7. a missing, stale, schema-incompatible or corrupt index is rebuilt from the
   validated immutable snapshot rather than triggering a repository download;
8. a valid reused index yields `READY`;
9. a successfully rebuilt index yields `READY_REPAIRED_INDEX`;
10. a snapshot/manifest/version failure remains not READY and the snapshot is
    preserved for diagnosis rather than being rewritten in place;
11. a persisted snapshot with an enrolled local integrity seal is checked
    before index validation/rebuild; a mismatch is `SNAPSHOT_INVALID` with a
    stable integrity reason code and no new index is built from that snapshot;
12. READY/READY_REPAIRED_INDEX candidates are sealed again after
    reconciliation and must have the same pre/post local seal;
13. a schema-v1 repository entry with no seal may be enrolled into schema v2
    only after strict snapshot/version/index reconciliation succeeds.

The exact snapshot SHA remains the repository identity. The local snapshot
seal is a separate tamper-detection key and never replaces upstream Git
revision provenance. Reconciliation never selects repository content
heuristically.

### Qualification record

A successful or partially successful qualification writes a durable local
record in application state using an atomic/consistent durable-write path.

The record contains installation facts and qualification outcomes, including:

- qualification schema/policy version;
- qualification timestamp;
- execution mode;
- application/runtime deployment identity;
- platform fingerprint;
- storage-root result;
- repository-state load result;
- per-repository status, exact SHA/version when applicable, index status and
  stable reason codes;
- overall platform/storage qualification outcome.

The record does not claim that a release attestation was verified from inside
the running application. Supply-chain attestation/signature verification is
an external installation/distribution trust decision.

Local AI-provider availability is also not part of the durable installation
qualification record. Provider discovery remains runtime state because the
provider can appear, disappear or change models without changing the AtM
installation.

### Clean-install behavior

A clean installation with no repository state and no local EWD/CBD/RMD
snapshots is a valid G-S0 outcome when the deployment and storage boundary are
valid:

- the installation can be platform/storage qualified;
- EWD, CBD and RMD are `NOT_INSTALLED`;
- no network request is required by G-S0;
- no repository is downloaded automatically;
- provider absence does not turn the installation qualification into a
  failure.

Repository Refresh and Download/Update remain explicit user actions governed
by R1.

### Required regression matrix

Automated coverage must include at least:

1. clean packaged installation, empty state/data, offline -> platform/storage
   qualified and all repositories `NOT_INSTALLED`;
2. valid state + snapshot + valid index -> `READY`;
3. missing index -> rebuilt locally -> `READY_REPAIRED_INDEX`;
4. corrupt or snapshot-mismatched index -> rebuilt locally before use;
5. state points to missing snapshot -> `SNAPSHOT_MISSING`;
6. malformed/corrupt repository-state file -> state `INVALID`, no heuristic
   snapshot selection, existing snapshots preserved;
7. persisted version differs from validated snapshot version -> not READY;
8. snapshot root is a symlink or non-directory -> `SNAPSHOT_INVALID`;
9. visible data root is a symlink or violates the storage boundary -> storage
   qualification failure;
10. wrong/malformed application or runtime deployment identity -> platform
    qualification failure;
11. application/runtime commit or qualification-policy change -> deterministic
    fingerprint change and requalification;
12. durable qualification-record write/read round trip;
13. one broken repository with other valid repositories -> failure isolation;
14. unpackaged development execution -> explicit development/unqualified
    execution state, never a qualified release deployment;
15. startup reconciliation performs no repository network I/O;
16. schema-v1 valid persisted snapshot -> strict local reconciliation -> seal
    enrollment -> schema-v2 state;
17. enrolled seal mismatch -> `SNAPSHOT_INVALID` before retrieval-index
    creation/rebuild;
18. snapshot changes while G-S0 reconciliation is running -> pre/post seal
    mismatch -> not READY;
19. a failed seal check or enrollment never silently changes the persisted
    repository SHA/version.

### Pass condition

G-S0 passes for the installation plane when the packaged deployment identity
and dedicated storage boundary qualify.

Repository readiness is reported independently per EWD/CBD/RMD and may remain
`NOT_INSTALLED` on a clean installation. Any repository reported READY must
have passed exact-SHA local snapshot validation and exact-snapshot retrieval
index validation/rebuild.

G-S0 does not scientifically certify EWD, CBD or RMD. The global READY
definition remains a local AtM compatibility/retrieval state only.

## G-O0 — Independent repository state oracle

G-O0 adds an independent, read-only checker for the local repository store.

Its purpose is not to become another runtime source of truth and not to repair
state. Its purpose is to detect a class of defects that production lifecycle
code cannot reliably expose by calling itself again.

A production validation/reconciliation result and the oracle result are two
separate observations. If production reports a repository READY while the
oracle rejects the same exact generation, the gate fails.

### Independence boundary

The oracle must not call or link the production helpers that establish or
repair repository readiness. In particular, it must not obtain its answer by
calling:

- `RepositoryStateStore`;
- `RepositoryLifecycleService`;
- `StartupQualificationService`;
- `RepositoryNative`;
- `atm_repository_reconcile_local*`;
- `atm_repository_validate_snapshot`;
- `atm_retrieval_index_ensure_for_snapshot`;
- `atm_retrieval_index_validate_snapshot_sources`;
- any future production activation, repair or garbage-collection helper.

The oracle may use generic parsing and storage libraries such as GLib,
JSON-GLib, libyaml and SQLite directly, because the required independence is
from AtM's production decision path, not from the underlying file-format
libraries.

Expected repository identity supplied to the oracle comes from the test
fixture or explicit oracle input rather than from a production runtime object.

The oracle is read-only:

- no network access;
- no archive extraction;
- no index rebuild;
- no state rewrite;
- no snapshot promotion;
- no deletion or garbage collection;
- no implicit recovery.

### State-to-snapshot checks

For each state entry inspected by the oracle:

1. `repository-state.json` is parsed directly;
2. schema version and repository-entry shape are checked independently;
   schema v1 remains a supported legacy input and schema v2 is the current
   state format;
3. repository IDs are unique;
4. the persisted SHA is exactly 40 lowercase hexadecimal characters;
5. the persisted version is non-empty;
6. for schema v2, `snapshot_seal_sha256` is either null for an unenrolled
   legacy generation or exactly 64 lowercase hexadecimal characters;
7. the exact snapshot path is derived as
   `<data_root>/Repositories/<id>/snapshots/<sha>`;
8. the exact snapshot is a real directory and no path component used for the
   inspected generation is a symlink;
9. when a schema-v2 seal is non-null, the oracle independently recomputes the
   `ATM-SNAPSHOT-SEAL-v1` value over the exact snapshot and requires an
   exact match without calling the production snapshot-seal helper;
10. the oracle never selects a different snapshot by directory ordering,
   mtime, lexical order or "latest" heuristics.

An absent repository state entry is a valid NOT_INSTALLED observation. An
invalid state file is an oracle failure for any READY claim and is never
rewritten by the oracle.

### Snapshot identity checks

For the exact snapshot selected by persistent state, the oracle independently
reads the snapshot artifacts required to bind identity:

- `.atm/repository.json` must be a regular non-symlink file;
- manifest `schema_version` must be the supported value;
- repository identity fields must match the explicit expected repository;
- the manifest's version source must resolve to `CITATION.cff`;
- `CITATION.cff` must expose exactly one non-empty top-level version value;
- that version must equal the persisted state version;
- `STATUS.md` and manifest-declared required/retrieval source paths used by
  the oracle must resolve beneath the same exact snapshot without symlink
  escape.

The oracle implementation must parse these artifacts independently rather
than delegating to AtM's production manifest/CFF validators.

### Retrieval-index checks

The exact retrieval-index path is derived independently as:

`<cache_root>/retrieval/<id>/<sha>.sqlite`

The index must be a real regular non-symlink file and is opened read-only.

The oracle must check at least:

1. `PRAGMA user_version` equals the supported retrieval schema version;
2. `PRAGMA integrity_check` returns exactly one `ok` row;
3. `PRAGMA foreign_key_check` returns no rows;
4. `snapshot_metadata` contains exactly the singleton row `id = 1`;
5. its repository ID, repository version and snapshot SHA match the exact
   state/snapshot identity;
6. its manifest schema version matches the supported manifest schema;
7. its stored manifest SHA-256 equals an independently computed SHA-256 of
   the exact snapshot `.atm/repository.json`;
8. every `source_files` row names a safe relative path beneath the exact
   snapshot;
9. each indexed source's stored byte size and SHA-256 equal the actual file;
10. the source-file set and source-role set agree with the source paths/roles
    independently derived from the snapshot manifest;
11. logical source IDs are unique and consistent with their recorded
    repository/path identity;
12. read-only reference checks detect orphaned or contradictory evidence rows
    that are not protected by SQLite foreign keys.

The oracle does not rebuild an invalid index. A missing or failing index is an
oracle failure for a READY claim.

### Required divergence fixtures

Automated coverage must prove that the oracle rejects at least:

1. state SHA points to a missing snapshot;
2. snapshot path or required path crosses a symlink;
3. state version differs from `CITATION.cff`;
4. manifest repository identity differs from the expected repository;
5. index filename/SHA differs from state;
6. `snapshot_metadata` repository ID, version or SHA differs from state;
7. stored manifest hash differs from the snapshot manifest;
8. indexed source size or SHA-256 differs from the exact source file;
9. required/indexed source membership differs from the manifest-derived set;
10. SQLite structural corruption detected by `integrity_check`;
11. a foreign-key violation detected by `foreign_key_check`;
12. extra or contradictory singleton metadata rows;
13. a production READY fixture deliberately corrupted after validation;
14. a schema-v2 persisted snapshot seal that differs from the independently
    recomputed exact-snapshot seal.

At least one fixture must demonstrate the reason for G-O0 explicitly:
production readiness is established first, the underlying local artifacts are
then perturbed without invoking production reconciliation, and the independent
oracle must reject the resulting state.

### Pass condition

G-O0 passes only when:

- a valid exact-SHA repository generation accepted by the production path is
  also accepted by the independent oracle;
- every required divergence fixture is rejected by the oracle;
- oracle execution performs no network or repair action;
- schema-v2 sealed generations are checked by an independent seal
  implementation;
- the oracle's implementation does not call production readiness helpers.

G-O0 is a verification instrument for tests and diagnostics, not a second
runtime authority.

When later lifecycle work introduces immutable generation records, leases,
transactional activation, garbage collection or revocation, G-O0 is extended
to verify the new cross-generation invariants. Its independence constraints
may be strengthened but must not be relaxed to reuse the production decision
path it is intended to check.

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

The header contains two distinct control groups in this order:

`[AI model] [Refresh AI] [AI status]    [Repositories] [Refresh repositories] [Download/Update] [Repository status]`

Repository Refresh performs a read-only remote check. Download/Update is a separate explicit mutation action. Repository status is not displayed in the AI-provider status area.

A `New Chat` action exists and clears the current provider/conversation state.

### Conversation-freeze behavior

Before the first message, repository selection and AI-model selection are editable.

After the first Send:

- the current chat keeps its pinned repository scope and AI-model identity;
- the repository and AI-model selectors remain visible but become insensitive for that chat;
- changing either identity requires the user to create a New chat rather than mutating the active conversation;
- switching between chat tabs restores each tab's own pinned display state.

No automatic scope mutation or implicit chat replacement is permitted.

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
- writes the downloaded archive to application-owned staging;
- supports cancellation;
- extracts/validates under `~/Ask the Model/Repositories/.staging`;
- promotes only validated immutable snapshots into `~/Ask the Model/Repositories/<id>/snapshots/<sha>`;
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

Persistent repository source snapshots are user-visible under `~/Ask the Model/Repositories`. Derived retrieval indexes remain in the application-private XDG cache. The lifecycle must remain usable offline from an already validated local snapshot.

### Update

An update:

- detects a changed remote SHA;
- does not overwrite the current valid snapshot in place;
- builds and validates the replacement first;
- switches current snapshot only after success.

A failed update leaves the previous snapshot usable.

A repository whose enrolled local snapshot seal no longer matches must expose an explicit repair path through the existing Download/Update lifecycle action. For a repair where the tracked remote SHA is unchanged, AtM must:

1. obtain the exact-SHA archive successfully before moving the invalid local snapshot;
2. quarantine the invalid real-directory snapshot rather than rewriting it in place;
3. validate the downloaded snapshot using the normal bounded ingestion contract;
4. promote the validated replacement at the exact SHA;
5. persist the replacement SHA/version/seal only after validation succeeds;
6. retain the quarantined invalid snapshot for diagnosis;
7. fail closed on symlink/non-directory snapshot paths rather than following them.

### Compact-header cancellation and retry boundary

The repository lifecycle backend accepts a `GLib.Cancellable` for remote refresh/download operations and propagates it through the network path. The approved compact v1 header does not require a separate visible Cancel control. A failed or cancelled repository operation may be retried through the existing Refresh or Download/Update action after the operation returns to an idle state.

### Deferred repository-management surface

`Remove Local Copy` and snapshot-history controls belong to a future detailed repository-management surface rather than the compact v1 header.

When removal is implemented, it must:

- affect only AtM local storage;
- never affect GitHub;
- refuse to remove a snapshot pinned by an active conversation;
- preserve any other snapshot required for safe update/rollback.

These constraints remain mandatory for that future surface; they are not part of the compact-header R1 completion gate.

### Pass condition

For the compact v1 header, download, service-level cancellation, retry and update must behave correctly for all three repositories; all three catalog repositories must provide manifests conforming to the same embedded v1 schema; and failure injection must not destroy the last valid ready snapshot.

Removal/history remain a separate future repository-management acceptance gate.

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

**Current development implementation status (2026-09-20): implemented.** The
GTK path freezes repository/model identity per chat tab, supplies only
current-turn evidence, resolves model-facing `[S#]` labels before committing a
grounded turn, rejects unknown labels, preserves the deep-copied provenance
mapping with the answer, removes temporary labels from user-visible text, and
renders compact numbered source references whose popovers recover the required
repository/version/SHA/logical-source/locator/excerpt metadata and immutable
GitHub permalink when it can be constructed safely. Real-repository R4 CI
continues to exercise traceability independently of the UI.

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

Clarification is a distinct conversation outcome, not an alias for unsupported
evidence or an empty retrieval. Benchmark runs must record whether each turn
performed retrieval or returned `needs_clarification`. Clarification topics
must achieve 100% outcome accuracy and must not attach fabricated retrieval
evidence.

### Pass condition

The deterministic retrieval pipeline meets the agreed benchmark gates or its shortcomings are explicitly documented before semantic expansion.

**Current implementation status (2026-09-20): PASS.** The frozen 33-topic
EWD/CBD/RMD benchmark passes all provisional R5 gates on the current
deterministic backend without changing qrels or thresholds. The verified
post-diversification run records Success@1 1.00, MRR 1.00, nDCG@5 0.9069,
required Recall@5 0.9833, wrong-repository contamination@5 0.0207, RO–EN
nDCG gap 0.0333, evidence traceability 1.00 and clarification-outcome
accuracy 1.00. This is an engineering acceptance result for the pinned
retrieval corpus, not a scientific-validity claim for EWD, CBD or RMD.

## R5-P — Production-policy context-efficiency shadow

R5-P is an observational companion to the frozen R5 benchmark. It exists to
measure the actual model-visible retrieval policy used by the application
without changing the historical R5 acceptance baseline.

### Policy identity

The application and the production-policy benchmark must consume the same
compile-time policy constants for:

- maximum ranked results per repository;
- maximum model-visible context sources;
- maximum model-visible context bytes.

The frozen R5 runner keeps its historical 10-results / 8-sources / 32-KiB
configuration for longitudinal comparability. The production shadow uses the
current application policy. A change to production policy must therefore
change the shared policy definition rather than silently changing only the
benchmark or only the runtime.

### Required measurements

For the same frozen topics and exact pinned repository snapshots, R5-P records
at least:

- the existing retrieval-quality metrics and gates;
- context precision;
- required-evidence recall in the context actually exposed to the model;
- model-visible context source count (mean and maximum);
- evidence bytes (mean and maximum);
- retrieval latency median and p95.

Required-evidence context recall is separate from retrieval Recall@5. A
retriever may find the correct evidence while an overly aggressive context
budget removes it before model generation.

### Initial status

R5-P begins as a shadow diagnostic, not a release gate. Its first purpose is to
establish the real production-policy baseline and quantify the difference from
frozen R5.

No production limit may be reduced solely because it lowers bytes or raises
context precision. A candidate context policy is eligible for later promotion
only after benchmark evidence shows that it preserves the frozen R5 gates,
provenance/traceability and the required evidence visible to the model.

Retrieval latency is reported but is not initially gated because CI runner
variance can dominate millisecond-scale local SQLite measurements.

### Candidate policy sweep

Candidate policies may be measured alongside production only as observational
benchmark modes. They must not alter `src/retrieval_policy.h` or application
behavior.

The first controlled sweep isolates:

- ranked results per repository: 6 -> 5;
- model-visible context-source cap: 12 -> 8;
- model-visible byte cap: 32 KiB -> 16 KiB;
- the combined 5-results / 8-sources / 16-KiB policy.

Each axis is measured separately before the combined policy so a regression can
be attributed to the responsible constraint rather than to an opaque bundle of
changes.

A candidate is not eligible for production promotion merely because it reduces
mean bytes or raises context precision. At minimum, it must preserve the
existing frozen R5 gates and must not reduce required-evidence recall visible to
the model relative to the production-policy baseline. Any topic-level recall
loss must be investigated explicitly rather than hidden by aggregate means.

### Pass condition

The frozen R5 benchmark remains unchanged and continues to pass its existing
gates; the production-policy shadow and any candidate sweep are reproducible
from the same corpus, use explicit named policies and produce the required
context-efficiency diagnostics without changing application behavior.

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

1. repository updated during active chat → chat stays on its pinned old SHA;
2. repository selector after first Send → remains visible but locked; changing scope requires New chat;
3. AI-model selector after first Send → remains visible but locked; changing model requires New chat;
4. retrieval index removed → rebuilt from the validated snapshot;
5. index metadata SHA mismatch → repository is rejected or the index is rebuilt before use;
6. corrupt SQLite index → repository is rejected or the index is rebuilt before use;
7. corrupt or unsafe snapshot input → not ready;
8. archive entry-count/uncompressed-size limit exceeded → update/download fails safely and the previous ready snapshot remains usable;
9. indexed source hash mismatch against the pinned snapshot → repository is rejected or the index is rebuilt before use;
10. unsupported AtM manifest schema → incompatible;
11. failed repository update → previous ready snapshot remains usable;
12. current-turn evidence does not accumulate in later provider history;
13. cancelled repository I/O → cancellation propagates and incomplete staging output is not promoted;
14. failed multi-repository Refresh → repositories not reached after the failure do not retain stale remote identities from an older batch;
15. schema-v1 ready snapshot → conversation preparation revalidates it and enrolls a schema-v2 local snapshot seal;
16. locally modified snapshot after seal enrollment → conversation grounding fails closed with NOT_READY and preserves the expected seal;
17. snapshot content changes during index validation/rebuild → pre/post seal mismatch and preparation fails before persistence or grounding;
18. snapshot mtime-only change → local snapshot seal remains stable, while content or executable-mode change changes the seal;
19. enrolled seal mismatch → grounding rejects the snapshot before a missing index can be rebuilt and the existing Download action becomes required;
20. explicit same-SHA repair → exact archive is obtained first, invalid snapshot is quarantined, validated replacement is promoted and persistent seal is replaced only after success;
21. invalid snapshot symlink → repair refuses to follow or quarantine it as a real snapshot directory.

Future repository-management regression gate, when removal/history is implemented:

- active-conversation snapshot removal → blocked;
- removal affects only AtM local storage and never the upstream repository.

## Documentation gate

Before a stage is merged:

- `docs/ARCHITECTURE.md` must not contradict the implemented boundary;
- `docs/INTERFACE_DESIGN_REQUIREMENTS.md` must reflect approved UI semantics;
- `docs/DEPENDENCIES_AND_COMPATIBILITY.md` must list any new build/runtime dependencies actually introduced;
- README capability claims must remain conservative;
- release/status documentation must distinguish implemented behavior from planned behavior;
- local snapshot-seal documentation must state explicitly that the seal is a local integrity key, not the upstream Git commit SHA or an external supply-chain attestation.

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
