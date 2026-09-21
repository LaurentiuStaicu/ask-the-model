# Repository & Retrieval Architecture v1

## Status

This document defines the design baseline for the first repository-aware Ask the Model (AtM) implementation.

It is an architecture contract, not a claim that the described repository-management or retrieval functionality is already implemented.

The v1 scope is intentionally limited to three known scientific repositories:

- Empirical World3 Dynamics (EWD);
- Cognitive Belief Dynamics (CBD);
- Romanian Monetary Dynamics (RMD).

Arbitrary repository URLs, generic Git hosting, repository write-back, scientific-model execution and automatic modification of model repositories are outside v1.

## Design goals

Repository-aware AtM must:

1. keep scientific repositories distinct from the local AI model;
2. preserve repository version and exact revision provenance;
3. allow one or more of EWD, CBD and RMD to ground one conversation;
4. keep ordinary local AI chat valid when no repository is selected;
5. download and update repository snapshots without weakening the Flatpak filesystem sandbox;
6. retrieve only evidence relevant to the current question instead of sending a whole repository to the AI model;
7. preserve source attribution through retrieval, context construction and answer presentation;
8. support Romanian, English and mixed Romanian/English technical queries;
9. remain usable with relatively small local language models and modest context windows;
10. add semantic-vector components only when benchmark evidence shows that simpler retrieval is insufficient.

## Terminology

The canonical terminology in `docs/TERMINOLOGY.md` applies.

- **Repository** means EWD, CBD or RMD.
- **AI model** means the local language model used to generate an answer.
- **Source** means a retrieved repository file, section, entity, relation or table row used as evidence.
- **Snapshot** means the exact local repository state associated with one Git commit SHA.
- **Repository version** means the version declared by the scientific repository itself, initially read from `CITATION.cff`.
- **Retrieval index** means a derived local SQLite database built for one exact snapshot.

Repository version and snapshot SHA are deliberately separate concepts.

## Repository selector

### Closed state

Before a repository is selected, the compact control is:

`Repositories`

After selection, only active repository acronyms are shown:

- `EWD`
- `EWD + RMD`
- `EWD + CBD + RMD`

The closed control does not show SHA values, branch names, commit counts or a generic count such as “3 models”.

### Expanded state

The selector presents the long form at selection time:

- `EWD (Empirical World3 Dynamics) v0.1.0`
- `CBD (Cognitive Belief Dynamics) v0.1.0`
- `RMD (Romanian Monetary Dynamics) v0.1.0`

The displayed version is the repository-declared model version for the snapshot available to the conversation.

The selector supports independent multi-selection. A repository that is known but not locally ready remains selectable so the user can include it in the desired set and activate Download. Repository-grounded Send must not use that repository until its validated snapshot and retrieval index reach READY.

### Zero-repository scope

No repository selection is a valid state.

- zero active repositories: ordinary local AI chat;
- one to three active repositories: repository-grounded chat.

Repository-aware functionality must not make ordinary local chat depend on repository installation.

## Repository control group

Repository selection and repository lifecycle are distinct responsibilities but are presented as one visually coherent header group.

The compact selector answers:

> Which repositories are active for this conversation?

Immediately after the selector, the repository group provides:

- `Refresh repositories` — checks the configured GitHub origins and remote SHAs/versions without changing local files;
- `Download/Update` — explicitly downloads missing selected repositories or updates selected repositories for which a newer tracked SHA is available;
- a dedicated repository-status area for checking, ready, offline-local, update-available, download/update progress and errors.

The repository group appears after the local AI-model group:

`[AI model] [Refresh AI] [AI status]    [Repositories] [Refresh repositories] [Download/Update] [Repository status]`

Refresh never silently replaces a local snapshot. Download/Update is the explicit mutation action. A future detailed repository-management surface may add removal/history controls, but it is not required for the compact v1 header workflow.

The UI must not imply that removing a local copy deletes or modifies the upstream GitHub repository.

## Repository catalog

Repository origins are compiled into AtM v1.

The v1 catalog contains exactly:

- `LaurentiuStaicu/empirical-world3-dynamics`;
- `LaurentiuStaicu/cognitive-belief-dynamics`;
- `LaurentiuStaicu/romanian-monetary-dynamics`.

The application does not accept arbitrary repository URLs in v1.

The origin URL, repository owner/name and tracked branch are application-controlled catalog data, not values trusted from downloaded repository content.

## Repository version and revision

AtM v1 tracks at least:

- repository ID;
- repository-declared version read from `CITATION.cff` at the resolved tracked-branch SHA;
- exact installed snapshot SHA;
- exact retrieval-index snapshot SHA.

The normal selector shows only the repository-declared version. Update availability is determined by exact tracked-branch SHA identity, not by GitHub Release metadata.

A future detailed repository-management surface may display published GitHub Release metadata as additional information. If it does, a difference between the tracked repository version and the latest published release is informational rather than an automatic validation failure: a development branch may legitimately move before a release is published.

AtM must not invent suffixes such as `-dev` unless the repository declares them itself.

## Download and update model

AtM resolves the configured tracked branch to an exact Git commit SHA before downloading an archive.

The sequence is:

```text
tracked branch
    ↓
resolve exact SHA
    ↓
download repository archive for that SHA
    ↓
write to staging
    ↓
validate snapshot
    ↓
build retrieval index
    ↓
validate retrieval index
    ↓
atomically make the snapshot current for future chats
```

AtM does not download “whatever main contains at the end of the transfer”. The archive request is tied to the resolved SHA.

Repository updates are explicit user actions. Automatic update checks are allowed; silent automatic repository replacement is not part of v1.

## Repository lifecycle state

The lifecycle state is separate from whether a repository is active in a conversation.

Primary lifecycle:

```text
NOT_INSTALLED
    ↓
DOWNLOADING
    ↓
VALIDATING
    ↓
INDEXING
    ↓
READY
```

Possible additional states include:

- UPDATE_AVAILABLE;
- DOWNLOADING_UPDATE;
- VALIDATING_UPDATE;
- INDEXING_UPDATE;
- REMOVING;
- ERROR;
- INCOMPATIBLE.

A failed update must leave the previously valid snapshot usable.

A repository may be `READY` while inactive for chat.

## Definition of READY

`READY` means that the local snapshot is compatible with AtM retrieval.

It does **not** mean that AtM certifies the scientific validity of the model.

A repository is ready only when all applicable checks pass:

- snapshot is present;
- repository identity matches the fixed catalog entry;
- version metadata can be read;
- AtM repository manifest is supported;
- required paths are present;
- snapshot validation succeeds;
- retrieval index exists;
- index schema is supported;
- index snapshot SHA equals repository snapshot SHA;
- database integrity checks succeed;
- FTS integrity checks succeed.

## Storage model

Validated scientific repository snapshots are intentionally visible to the user in a dedicated AtM directory under Home:

```text
~/Ask the Model/
└── Repositories/
    ├── ewd/
    │   └── snapshots/<sha>/
    ├── cbd/
    │   └── snapshots/<sha>/
    ├── rmd/
    │   └── snapshots/<sha>/
    └── .staging/
        └── <repository-id>/<sha>.part/
```

The Flatpak receives read/write access only to `~/Ask the Model` with directory creation permitted. It must not request `--filesystem=home`, `--filesystem=host` or another broad host-filesystem grant.

Repository snapshots are immutable once validated. The visible folder therefore contains canonical local source copies that remain usable offline and may also be inspected by the user or other tools with their own filesystem permissions.

Regenerable retrieval indexes remain application-private cache data:

```text
$XDG_CACHE_HOME/retrieval/
    ewd/<sha>.sqlite
    cbd/<sha>.sqlite
    rmd/<sha>.sqlite
```

Application state remains private:

```text
$XDG_STATE_HOME/
    repository-state.json
```

Archive downloads may use XDG cache staging, while extraction/validation staging is kept beneath `~/Ask the Model/Repositories/.staging` so atomic promotion into the final snapshot tree remains on the same filesystem.

If an index cache disappears while a valid snapshot remains, AtM rebuilds the index from the visible immutable snapshot rather than redownloading the repository.

## Snapshot immutability and retention

Once a snapshot has been validated and promoted from staging, AtM does not modify repository files inside that snapshot.

Once a retrieval index has been successfully built and validated for a snapshot, that index is treated as immutable.

The minimum v1 retention policy is:

- current snapshot;
- previous snapshot when required for safe update/rollback;
- any snapshot pinned by the active conversation.

A snapshot used by the active conversation cannot be removed.

Because v1 conversation history is not persisted across application restarts, long-term historical snapshot retention is not yet required.

## Repository manifest

Each repository in the v1 catalog must contain:

`.atm/repository.json`

The manifest describes how AtM may consume the repository. It does not define repository origin, execute code or duplicate general citation metadata.

The v1 manifest uses the strict application-controlled JSON Schema embedded at `data/schemas/repository-manifest-v1.schema.json`. Repository manifests are data contracts; they cannot expand the built-in repository catalog.

Conceptual fields:

```json
{
  "schema_version": 1,
  "repository_id": "ewd",
  "acronym": "EWD",
  "display_name": "Empirical World3 Dynamics",
  "version_source": {
    "type": "cff",
    "path": "CITATION.cff"
  },
  "status_source": "STATUS.md",
  "required_paths": [],
  "retrieval": {
    "canonical": [],
    "structural": [],
    "evidence": [],
    "tabular": [],
    "implementation": [],
    "exclude": []
  }
}
```

Manifest path arrays contain repository-relative exact paths or directory prefixes. They are not shell globs and are never executed. Runtime validation additionally rejects absolute paths, traversal components and paths that resolve outside the validated snapshot.

The manifest cannot provide executable parser code. Extractors are application-defined and selected only from an AtM allowlist.

An unsupported manifest major schema version produces an incompatible-repository state rather than heuristic interpretation.

## Stable source identity

Where a repository already provides canonical IDs, AtM preserves them.

Examples include:

- `food_per_capita`;
- `VAR.BELIEF.CLAIM`;
- `LINK.FAMILIARITY.BELIEF`;
- `government_refinancing_interest_loop`.

AtM creates a logical source identity that is independent of the snapshot SHA.

Examples:

```text
ewd:entity:variable:food_per_capita
cbd:entity:variable:VAR.BELIEF.CLAIM
cbd:entity:relation:LINK.FAMILIARITY.BELIEF
rmd:entity:feedback_loop:government_refinancing_interest_loop
rmd:file:STATUS.md
```

A complete evidence identity combines:

```text
logical source ID
+
snapshot SHA
+
snapshot-specific locator
```

This allows the same logical entity to be recognized across repository revisions without pretending that different revisions are identical evidence.

## Source locators

AtM uses source-type-appropriate locators.

For JSON:

- source path;
- JSON Pointer where appropriate;
- repository-native entity ID where available.

For Markdown/prose:

- source path;
- heading path;
- snapshot-specific section identity.

For tabular data:

- dataset identity;
- semantic row key such as year or other declared primary dimensions.

Line numbers may be used for display/permalinks but are not the sole logical identity.

## Snapshot validation

Validation is repository-aware.

Common checks include:

- required common metadata;
- supported AtM manifest;
- path safety;
- readable text/JSON/CSV inputs;
- repository-specific required artifacts.

Repository-specific validation must not assume identical layouts across EWD, CBD and RMD.

Examples of high-value structured inputs include:

- EWD: `science/configs/real_model_structure.json`;
- CBD: model variables, processes, links and contracts;
- RMD: dynamics registries, accounting contracts, provenance and processed data.

## Archive extraction security

Downloaded archives are treated as untrusted input.

AtM accepts only intended regular files and directories.

When libarchive is used, extraction must enable the available secure-write protections corresponding to at least:

- `ARCHIVE_EXTRACT_SECURE_NOABSOLUTEPATHS`;
- `ARCHIVE_EXTRACT_SECURE_NODOTDOT`;
- `ARCHIVE_EXTRACT_SECURE_SYMLINKS`.

The extractor also rejects or safely handles at least:

- absolute paths;
- path traversal using `..`;
- symlinks;
- hardlinks;
- block/character devices;
- FIFOs;
- sockets.

Extraction is resource-bounded. Application-controlled limits cover at least:

- downloaded archive bytes;
- number of archive entries;
- maximum individual extracted-file size;
- maximum total uncompressed bytes written to staging.

Nested archives may be stored as ordinary repository files when otherwise allowed, but AtM does not recursively unpack them during repository ingestion.

Limit values are implementation constants validated against the known EWD/CBD/RMD repositories and may be raised deliberately when those repositories legitimately grow; archive metadata alone is not trusted to bypass the limits.

Repository files are never executed as part of ingestion or retrieval.

## Retrieval-index model

AtM builds one SQLite retrieval index per exact repository snapshot.

Examples:

```text
ewd/<sha>.sqlite
cbd/<sha>.sqlite
rmd/<sha>.sqlite
```

The index is derived cache data. Schema changes cause a rebuild from the immutable snapshot rather than an in-place migration.

`PRAGMA user_version` is the authoritative retrieval-index schema version. The current implementation uses **user_version 2** from `data/schemas/retrieval-index-v2.sql`; schema v2 adds an explicit `status` source role derived from each repository manifest's `status_source`. A cached index with an older or unsupported user version is invalidated and rebuilt from the immutable snapshot rather than migrated in place.

A final validated index is opened read-only for retrieval and should use defensive SQLite settings such as:

- `PRAGMA foreign_keys = ON`;
- `PRAGMA trusted_schema = OFF`;
- `PRAGMA query_only = ON` on retrieval connections.

The exact flags remain subject to the elementary OS 8 platform probe.

## Retrieval-index logical schema

The minimum logical v1 index includes:

- snapshot metadata;
- source files;
- prose/document sections;
- structured entities;
- structured relations;
- dataset metadata;
- dataset rows or queryable tabular representation;
- FTS5 full-text search data.

The design must not flatten every artifact into arbitrary fixed-length text chunks.

Structured JSON entities stay structured. CSV data remains queryable as tabular data. Markdown/prose is segmented primarily by document structure such as headings.

## FTS5

The baseline lexical index uses SQLite FTS5 with a Unicode tokenizer.

The initial candidate tokenizer is:

`unicode61 remove_diacritics 2`

Exact technical identifiers do not depend on the FTS tokenizer; they are handled by structured/exact lookup.

The first implementation favors a normal FTS5 table over an external-content/contentless design because the initial repositories are small and correctness is more important than marginal storage savings.

Index validation includes the FTS5 `integrity-check` command.

## Retrieval v1

The mandatory first retrieval implementation is local and deterministic where possible.

Order of capabilities:

1. exact identifier lookup;
2. structured entity/relation lookup;
3. FTS5/BM25 lexical retrieval;
4. table/dataset lookup;
5. evidence ranking, authority weighting and deduplication.

Embeddings, vector databases and rerankers are not v1 prerequisites.

They may be added only after benchmark results demonstrate a material retrieval-quality improvement that justifies their local storage, memory and latency costs.

## Bilingual query normalization

Repository documents are predominantly English, while queries may be English, Romanian or mixed.

AtM therefore maintains:

- the original query;
- a normalized query representation;
- protected technical identifiers;
- repository scope;
- recognized entities/years/numbers/versions;
- a curated bilingual concept/alias map;
- deterministic routing signals.

Repository-declared bilingual labels are preferred over application-invented translations.

AtM translation aliases are versioned separately from immutable repository indexes so that query expansion can improve without rebuilding every snapshot index.

## Query routing

The router does not have to choose one exclusive intent.

A query may activate several signals, including:

- current state/boundary;
- structure/mechanism;
- evidence/provenance;
- numeric/tabular;
- implementation/code;
- cross-repository comparison.

Explicit repository names restrict retrieval to those repositories, provided they are part of the conversation scope.

If no repository is named, retrieval uses the repositories pinned to the conversation.

## Source authority

Retrieved material is not ranked solely by lexical similarity.

AtM records the implemented source roles explicitly:

- `status` — the manifest-declared `status_source`;
- `canonical`;
- `structural`;
- `evidence`;
- `tabular`;
- `implementation`.

A source may carry more than one role. For current-state questions, the declared
`status_source` has the highest authority. For structure questions, declared
status/canonical structure and structural sources remain comparable so lexical
relevance can distinguish model-level paradigm statements from lower-level
implementation detail.

Within the same authority class, FTS5 BM25 uses a stronger title weight than
body text. Final deterministic ranking also prevents a single non-exact source
file from saturating the primary result set: exact matches and declared status
evidence are preserved, while excess results from the same other source are
deferred and used as backfill when needed. This preserves precise lookup while
allowing complementary canonical evidence into bounded context.

Conflicting evidence is retained and surfaced rather than silently rewritten into a false consensus.

## Context construction

The model never receives the entire repository.

Retrieval returns evidence records. The context builder chooses a compact subset according to:

- repository scope;
- source authority;
- retrieval score;
- deduplication;
- cross-repository balance;
- available model context;
- reserved output budget.

Structured records should be represented compactly instead of serializing large source files verbatim.

Retrieved evidence is ephemeral to the current turn and is not appended permanently to the user/assistant conversation history.

## RAG trust boundary

Retrieved repository content is treated as data, never as behavioral instructions.

Context supplied to the AI model is clearly delimited and accompanied by stable grounding instructions.

The v1 design uses:

- a fixed repository-origin allowlist;
- provenance metadata;
- SHA-256 content hashes for indexed source files and other snapshot/document hashes where appropriate;
- verification that indexed source metadata still corresponds to the pinned immutable snapshot before evidence is exposed;
- retrieval limits;
- excluded operational paths by default;
- source delimiters;
- explicit instructions that retrieved text is untrusted data.

These mitigations reduce but do not claim to eliminate indirect prompt-injection risk.

## Citation model

The AI model receives short temporary evidence labels such as `[S1]`, `[S2]`.

AtM owns the mapping between those labels and evidence objects.

The AI model must not invent repository URLs or provenance metadata.

User-visible citations remain concise, for example:

- `EWD · STATUS`;
- `CBD · LINK.FAMILIARITY.BELIEF`;
- `RMD · feedback registry`.

Citation detail may expose:

- repository name;
- repository version;
- snapshot SHA;
- source path;
- logical source ID;
- entity/heading/dataset locator;
- evidence excerpt;
- immutable GitHub permalink when appropriate.

## Conversation pinning

Repository scope is selected before the first user message.

When the first message is sent, the conversation freezes:

- selected AI model identity and digest when available;
- repository set, including an empty set;
- repository versions;
- repository snapshot SHAs.

Repository updates that occur later affect future chats, not the active chat.

Changing repository scope after the first message starts a new chat.

The same rule is the preferred v1 behavior for changing the AI model, because otherwise one transcript would mix different inference models.

## New Chat

`New Chat` resets:

- provider conversation history;
- retrieval conversation state;
- pinned repository snapshots;
- turn evidence/citation state.

It does not remove downloaded repositories or retrieval indexes.

The previous repository selection may be carried forward as an editable default before the first message of the new chat.

## Multi-turn retrieval state

AtM maintains a compact deterministic retrieval state separate from visible conversation text.

It may include:

- active repositories;
- active entities/concepts;
- relevant scenario;
- time/year scope;
- recent routing signals;
- last evidence/source identities.

This state supports follow-up questions such as:

- “Și în RMD?”;
- “Dar în 2024?”;
- “Sursele?”;
- “Compară cu CBD.”

Ambiguous references should cause clarification rather than arbitrary guessing.

A separate LLM query-rewriter is not a v1 dependency.

## Current-turn evidence only

For generation, AtM combines:

- stable grounding instructions;
- bounded recent conversation history;
- the current user question;
- evidence retrieved for the current turn.

Previous retrieval blocks are not accumulated in the provider message history.

Persistent citation objects may remain associated with their originating turn for later inspection.

## Benchmark-first semantic expansion

The retrieval system is evaluated before embeddings are added.

The benchmark includes:

- English queries;
- Romanian queries;
- mixed Romanian/English technical queries;
- exact-ID lookups;
- current-state questions;
- mechanism/structure questions;
- evidence/provenance questions;
- numeric/tabular questions;
- unsupported-premise questions;
- cross-repository comparisons;
- multi-turn follow-ups.

Semantic embeddings and reranking remain optional later stages.

## External design references

The v1 design is informed by:

- GitHub repository archive endpoints and commit-pinned retrieval;
- Flatpak XDG/sandbox conventions;
- GTK/GNOME compact header/popover patterns;
- JSON Schema Draft 2020-12 for strict manifest validation;
- SQLite FTS5, JSON and integrity facilities;
- OWASP RAG security guidance for source provenance and retrieved-content trust boundaries.

These references guide the implementation but do not replace AtM-specific acceptance tests.
