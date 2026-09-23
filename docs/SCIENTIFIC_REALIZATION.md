# SCI-07a — deterministic scientific realization

SCI-07a adds the first presentation layer above a finalized Scientific Result Artifact (SRA).

It deliberately has **no LLM dependency**.

The trust boundary is:

```
finalized + validated SRA
        ↓
RealizationView
        ↓
Direct Renderer
        ↓
user-visible technical answer
```

## RealizationView

`atm-realization-view/1` copies only validated SRA content into opaque realization blocks.

Block classes are:

- answerability;
- established fact;
- derived fact;
- constraint;
- conflict;
- limitation.

In v1 every block is mandatory. There is no omission policy yet. This is intentionally stricter than the later optional LLM planner.

The view also preserves exact per-block qualified support IDs for deterministic citation mapping in later work. Support IDs are metadata; the direct renderer does not invent citation labels.

## Direct renderer

`atm-direct-render/1` renders every mandatory block exactly once.

Romanian and English profiles change only presentation labels. Scientific values, units, status codes, reason codes, operation IDs and numeric-profile IDs are copied verbatim.

The renderer does not:

- round numeric values;
- reinterpret status codes;
- infer missing content;
- summarize conflicts or limitations away;
- call an LLM;
- stream model-generated text;
- create citations.

Control characters and embedded line breaks in SRA strings are escaped before display so a value cannot inject new synthetic sections into the direct answer.

## Availability rule

A Direct Render can be constructed only from an SRA that is already finalized and passes `atm_sra_result_validate()`.

Therefore failure or absence of an AI model cannot make an otherwise valid scientific result unavailable.

## Relationship to SCI-07b

SCI-07b may later add an optional structured LLM realization plan. That planner must operate over the same mandatory RealizationView and must be validated deterministically before any user-visible realization is produced.

Direct Render remains the fallback and baseline of truth.
