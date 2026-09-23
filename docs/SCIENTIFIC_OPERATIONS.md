# Scientific Operation Registry v1

SCI-06a introduces AtM-owned, versioned scientific operations without yet relaxing the SCI-05 prohibition on derived SRA facts. SRA integration is a separate fail-closed step after this executor is qualified.

## Authority boundary

The registry is compiled into AtM. Repository formula strings, equations, scripts, LLM output, prompt text and user-provided code are not executable scientific authority.

Every operation has a fixed:

- qualified operation name and version;
- operation class (derivation or check);
- repository/profile binding metadata;
- numeric profile;
- ordered list of input roles.

Execution accepts only that fixed role order. Every numeric input must be finite and carry an explicit unit.

## Initial operations

### `CBD_BAYES_LR_UPDATE@1`

Inputs:

- `prior_probability`, unit `1`, constrained to `0 <= p <= 1`;
- `likelihood_ratio`, unit `1`, constrained to `LR > 0`.

The implementation uses an algebraically stable form and handles prior probabilities 0 and 1 explicitly. No repository expression parser is involved.

### `RMD_FINANCIAL_STOCK_CLOSE@1`

Computes, in fixed binary64 operation order:

`((opening + transactions) + revaluations) + other_changes`

All four inputs must use the same explicit unit.

### `RMD_FINANCIAL_STOCK_IDENTITY_CHECK@1`

Computes the expected closing stock with the same operation order, then compares it with an observed closing stock using explicit absolute and relative tolerances. Accounting quantities and the absolute tolerance use the same unit; relative tolerance is dimensionless.

### `RMD_SAFE_RATIO@1`

The v1 ratio operation requires numerator and denominator to use the same unit. A zero denominator returns an explicit `MISSING` outcome with reason `ZERO_DENOMINATOR`; it is never coerced to zero or silently represented by NaN/infinity.

## Numeric boundary

The initial registry uses `atm-numeric/binary64-basic-v1`: finite binary64 inputs, a fixed source-level operation sequence and explicit failure on non-finite results. This is deliberately small. Transcendental functions, arbitrary repository formulas, simulations and behavioural closures are outside SCI-06a.

## SRA boundary

SCI-06a does not make `derived_facts[]` writable and does not permit operation claims in an SRA. SCI-06b will bind registered operations to established input facts and qualified support before that gate is opened.
