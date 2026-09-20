# Retrieval benchmark v1

This directory contains the frozen Ask the Model (AtM) deterministic retrieval
benchmark defined by `docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md` R5.

The benchmark is deliberately separate from the moving real-repository
integration smoke tests. R3/R4 integration workflows follow the current
repository `main` branches to detect compatibility drift. R5 instead pins
exact repository versions and full commit SHAs so the same information needs,
relevance judgments and retrieval system can be compared reproducibly.

The reviewed seed corpus is `benchmark.json`. It pins EWD, CBD and RMD
v0.1.0 to exact commit SHAs and contains development/validation topics covering
all R5 topic classes, English/Romanian/mixed-language retrieval, unsupported
premises and deterministic multi-turn clarification. The corpus is an
evaluation fixture with fixed qrels and targets; the current deterministic
retrieval implementation is evaluated against it by the real-corpus runner
rather than by modifying the corpus to fit implementation behavior.

## Relevance scale

AtM v1 uses four relevance grades:

| Grade | Meaning |
| ---: | --- |
| 3 | canonical / essential |
| 2 | directly supporting |
| 1 | useful supplementary |
| 0 | irrelevant |

A judgment also has an explicit `required` flag. Recall@5 is calculated over
required evidence rather than assuming that every grade-3 item is mandatory.

The evaluator treats grades 1–3 as relevant for reciprocal rank and context
precision. nDCG@5 retains the graded values and uses linear gain equal to the
judgment grade with logarithmic rank discount `gain / log2(rank + 1)`.

This follows established information-retrieval practice for graded relevance.
The benchmark's numerical gates remain project-specific engineering targets.

## Benchmark and run contracts

Schemas:

- `data/schemas/retrieval-benchmark-v1.schema.json`
- `data/schemas/retrieval-benchmark-run-v1.schema.json`

A benchmark records:

- a benchmark ID and schema version;
- the exact EWD/CBD/RMD corpus version + snapshot SHA;
- topic ID and underlying information-need ID;
- development/validation split;
- topic type;
- English, Romanian or mixed-language query;
- active and explicitly requested repository scope;
- optional exact-ID target;
- graded relevance judgments;
- unsupported-premise expectation;
- expected turn outcome: `retrieval` or `needs_clarification`;
- optional conversation/turn identity for multi-turn topics.

Clarification is deliberately separate from unsupported evidence. A
`needs_clarification` topic means that the deterministic conversation state
must refuse to guess from the available turn context. It is not scored as a
successful empty retrieval and it must not carry retrieved context.

All language variants of one information need must remain in the same split.

A run records, per topic:

- ordered retrieval results;
- selected context evidence;
- repository ID, repository version and snapshot SHA for every evidence item;
- retrieval latency;
- evidence byte count;
- evidence token count when an agreed tokenizer/counting method is available;
- actual turn outcome: `retrieval` or `needs_clarification`.

A run that reports `needs_clarification` must contain no retrieval results or
context evidence and must report zero evidence bytes. This keeps conversational
control behavior auditable independently from retrieval relevance.

The evaluator refuses a run whose corpus does not exactly match the benchmark
corpus.

## Metrics

`scripts/evaluate_retrieval_benchmark.py` reports:

- exact-ID Success@1;
- MRR;
- nDCG@5;
- required/canonical Recall@5;
- context precision;
- wrong-repository contamination@5 for explicit single-repository topics;
- paired RO–EN nDCG gap by information-need group;
- EN, RO and mixed-language nDCG diagnostics;
- evidence traceability against the pinned corpus metadata;
- median and p95 retrieval latency;
- mean/max evidence bytes;
- mean/max evidence token count and token-count coverage;
- unsupported-topic empty-context rate;
- duplicate-result rate;
- expected turn-outcome accuracy;
- clarification-outcome accuracy.

The provisional gates remain those in the R5 acceptance contract:

- exact-ID Success@1 >= 0.98;
- overall nDCG@5 >= 0.90;
- canonical/required Recall@5 >= 0.90;
- wrong-repository contamination@5 <= 0.05;
- paired RO–EN nDCG gap <= 0.05;
- evidence traceability = 1.00.

In addition, R5 has a deterministic conversation-protocol gate:

- clarification-outcome accuracy = 1.00.

The evaluator reports missing required metrics as failed gates. Targets are not
relaxed merely to make a run pass.

## Corpus integrity

`tests/retrieval_benchmark_corpus_test.py` validates the semantic corpus
invariants in addition to the evaluator's own input checks. In particular it
freezes the reviewed repository SHAs, requires all R5 topic types and all three
language classes, preserves paired Romanian/English information needs, requires
unsupported and clarification cases, checks qrel repository identity and
requires contiguous multi-turn conversations with an unchanged active
repository scope.

Judgments are tied to logical source IDs produced by the AtM indexer. Exact
technical entities use structured-entity IDs, numeric topics use deterministic
CSV row IDs, and canonical status/limitation topics use Markdown section IDs
with line locators from the same section parser used by the index.

## Passing deterministic baseline

R5 now has a deterministic real-corpus runner. It checks out the exact pinned
EWD/CBD/RMD snapshots, validates their SHAs and declared versions, rebuilds the
per-SHA indexes, executes topics through the R3 router and deterministic
retrieval-conversation state, constructs bounded R4 context, records the
versioned run contract and evaluates the result.

The first gate-passing deterministic baseline on 2026-09-20 recorded:

- exact-ID Success@1: **1.00**;
- MRR: **1.00**;
- nDCG@5: **0.9002**;
- required/canonical Recall@5: **0.9833**;
- wrong-repository contamination@5: **0.0207**;
- paired RO–EN nDCG gap: **0.0149**;
- evidence traceability: **1.00**;
- expected turn-outcome accuracy: **1.00**;
- clarification-outcome accuracy: **1.00**.

These values satisfy every provisional R5 gate without changing the frozen
qrels or thresholds. Latency and byte-budget diagnostics remain recorded per
run and are expected to vary by CI host. Evidence token count remains unset
until the project adopts an explicit tokenizer/counting contract.

A later verification on the current post-diversification implementation
(`github-35511507164-1`, 2026-09-20) also passed every gate and recorded:

- exact-ID Success@1: **1.00**;
- MRR: **1.00**;
- nDCG@5: **0.9069**;
- required/canonical Recall@5: **0.9833**;
- wrong-repository contamination@5: **0.0207**;
- paired RO–EN nDCG gap: **0.0333**;
- evidence traceability: **1.00**;
- expected turn-outcome accuracy: **1.00**;
- clarification-outcome accuracy: **1.00**.

The retrieval-quality metrics above are deterministic for the pinned corpus and
current implementation. Latency remains a host diagnostic rather than a frozen
quality result.

R5 passing validates this fixed deterministic retrieval baseline only. It does
not imply that the released v0.2.2 GTK application exposes repository-aware
chat, that the benchmark can never be expanded, or that the scientific models
themselves are validated by AtM.

No embeddings or reranker are required by the passing R5 baseline.
