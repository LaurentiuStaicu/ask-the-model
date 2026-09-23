# SCI-06b — SRA scientific derivations

SCI-06b opens SRA `derived_facts[]` only through the versioned AtM Scientific Operation Registry.

It does not permit callers to insert arbitrary numeric outputs, formulas, repository expressions, LLM text or user code.

## Binding model

A derivation is requested as:

- one registered operation qualified name;
- an ordered list of `role -> established_fact_id` bindings;
- semantic metadata for the output fact.

The binding order must exactly match the operation descriptor. Every referenced input must already be an established SRA fact with a finite numeric value, an explicit unit and qualified support.

SCI-06b v1 deliberately permits only operations whose registry class is `DERIVATION`. `RMD_FINANCIAL_STOCK_IDENTITY_CHECK@1` remains registered and executable, but cannot yet create an SRA constraint; that step waits for a versioned tolerance-policy binding.

## Numeric qualification

The SRA keeps its existing global policy:

`atm-numeric/exact-decimal+binary64-v1`

A derivation records the operation's own profile separately:

`atm-numeric/binary64-basic-v1`

The compatibility relation is explicit AtM policy. The operation profile never overwrites the SRA profile.

Numeric derived facts retain both:

- a locale-independent `g_ascii_dtostr()` representation;
- the exact binary64 bit pattern.

Missing results are explicit. For example, `RMD_SAFE_RATIO@1` with denominator zero creates outcome `MISSING`, value `MISSING` and reason `ZERO_DENOMINATOR`; it never creates zero, NaN or infinity.

## Provenance

Scientific content contains the semantic derived result, including exact numeric bits or missing-state reason.

Qualification contains the method:

- registered operation/version;
- operation numeric profile;
- exact ordered role-to-fact bindings;
- the union of the established input support.

Changing the derivation method or its input binding therefore changes qualification even when the semantic output is unchanged.

The global `operations[]` set is not free-form after finalization: it must equal the set of operations actually used by derived facts.

## Validation

Final SRA validation deterministically re-executes every derived fact from its bound established facts and rejects:

- unknown or CHECK-class operations;
- wrong role order;
- missing/non-numeric input facts;
- incompatible units or operation preconditions;
- unsupported numeric profiles;
- missing required repository/profile qualification;
- support that is not exactly inherited from the input facts;
- mutations to output, binary64 bits, reason code, operation or input bindings;
- arbitrary operation claims not backed by a derived fact.

No LLM is involved.
