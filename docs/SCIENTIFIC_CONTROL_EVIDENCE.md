# Deterministic scientific control evidence

Some repository state changes the scientific meaning of later operations but is not naturally represented as a retrieval entity. AtM reads such state through a separate deterministic control channel.

## Authority model

A control is identified by an AtM-owned enum value. Each control ID is compiled together with:

- one repository ID;
- one exact snapshot-relative source path;
- one RFC 6901 JSON Pointer;
- one expected scalar value type.

Callers do not provide an arbitrary source path or pointer. Repository payload text cannot register new controls.

## Snapshot safety

The control reader first requires the snapshot path itself to end in the AtM storage binding `Repositories/<repository_id>/snapshots/<snapshot_sha>`. It then opens the snapshot root and every path component with no-follow semantics. The source must be a regular file, is read through its already-open descriptor, and is bounded by a fixed size limit.

The returned control value carries that bound repository ID and pinned snapshot SHA together with the exact source path and pointer.

## JSON Pointer

Pointers use RFC 6901 semantics:

- the empty string denotes the document root;
- reference tokens are separated by `/`;
- `~0` decodes to `~`;
- `~1` decodes to `/`;
- array indices are unsigned base-10 integers without leading zeros;
- unresolved tokens fail closed.

No JSONPath, key search, fuzzy lookup or textual fallback is used.

## Initial v1 control

`ATM_SCIENTIFIC_CONTROL_RMD_BEHAVIOURAL_CLOSURE_ACTIVE`

- repository: `rmd`
- source: `model/dynamics/core_contract.json`
- pointer: `/behavioural_closure/active`
- type: boolean

This control preserves the current RMD boundary: later scientific operations can distinguish inactive behavioural closure deterministically, without asking retrieval or an LLM to infer the state from prose.