# Retrieval benchmark v1

This directory is reserved for the frozen Ask the Model (AtM) deterministic
retrieval benchmark defined by
`docs/REPOSITORY_RETRIEVAL_ACCEPTANCE.md` R5.

The benchmark is deliberately separate from the moving real-repository
integration smoke tests. R3/R4 integration workflows follow the current
repository `main` branches to detect compatibility drift. R5 instead pins
exact repository versions and full commit SHAs so the same information needs,
relevance judgments and retrieval system can be compared reproducibly.

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

This follows the established information-retrieval practice of using nDCG for
graded relevance. NIST TREC evaluation tooling likewise supports graded nDCG
with relevance levels as gains by default.

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

An `expect_unsupported` topic describes a premise or requested claim that the
pinned repositories do not establish. It does **not** mean that retrieval should
be empty. Canonical limitation, validation-boundary or negative evidence may be
positively relevant and required when it is what allows a grounded answer to
correct the premise. If no such evidence exists, an unsupported topic may have
no positive qrels.

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
- unsupported-topic empty-context rate (diagnostic only; neither direction is
  universally preferable because some unsupported premises have relevant
  boundary evidence and others do not);
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
relaxed merely to make a run pass. A benchmark with no clarification topic
therefore cannot satisfy the clarification gate.

## Stage boundary

This contract/evaluator does not itself constitute the R5 benchmark.

The next R5 step is to pin the exact corpus, author reviewed topic variants and
qrels against those snapshots, then build a deterministic run generator over
the R3 router/R4 context builder. Multi-turn topics remain in the benchmark
contract but require the conversation retrieval-state implementation before
they can be scored honestly.

No embeddings or reranker are introduced by R5.
