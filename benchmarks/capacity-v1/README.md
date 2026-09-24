# Capacity qualification evidence v1

This directory freezes the evidence used by C0 disk/inode admission planning.

`evidence.json` is **qualification evidence, not production policy**. It
records the exact GitHub Actions artifacts and repository SHAs behind C0-M1,
C0-E1, C0-E2 and C0-E3 so later implementation work does not depend on
expiring CI artifacts or prose-only pull-request comments.

## Exact-SHA rule

Measured repository observations are keyed by:

`repository_id + repository_sha`

A result measured for one SHA must not be treated as a capacity guarantee for
a later repository SHA. A future production policy may use an exact matching
profile as evidence, but an unknown SHA is classified as unqualified for
proactive byte prediction and must fall back to the already-qualified
fail-closed ENOSPC behavior rather than silently reusing stale numbers.

This rule is especially important because the frozen corpus has materially
different shapes:

- CBD is small and balanced;
- EWD is retrieval-index dominant;
- RMD is snapshot/repair dominant.

The C0-M1 measurements therefore rejected the idea of one universal
`archive_size × multiplier` formula.

## What the observations mean

Allocated bytes are host/filesystem observations from the C0-M1 qualification
environment. They are not portable upper bounds for every Linux filesystem.

The runtime-independent C0 model remains layered:

1. C0-F2 inspects the completed archive without writing and gives the exact
   materialized-entry requirement for inode planning.
2. C0-F3 separates pre-download admission from post-download/pre-mutation
   admission so the completed archive is not double-counted.
3. C0-F1 groups data/cache/state by `st_dev`, sums only real phase overlap
   and evaluates bytes/inodes independently.
4. E1/E2/E3 establish that genuine disk/inode exhaustion remains fail-closed
   and restart-safe even when admission cannot predict a later external
   capacity loss.

## State-root scaling evidence

C0-M2 extends the state-root evidence from one small Control DB to completed
histories of 1, 100 and 1000 generations. In the qualification environment,
the Control DB allocated footprint grew from 40,960 B to 229,376 B, while the
constrained guarded-publication frontier remained unchanged: 16 KiB and
32 KiB failed closed, while 64 KiB and 128 KiB completed atomically.

This is useful evidence that immediate publication headroom is not simply
proportional to total Control DB history size. It is still one Linux/SQLite/
filesystem environment and therefore is not a portable reserve guarantee.

## Reserve policy

No reserve is selected here.

The issue contract explicitly forbids freezing an arbitrary percentage. A
later production change must document its byte/inode reserve rationale,
including state-root SQLite headroom, and must keep the reserve policy
separable from storage formats and from these raw measurements.

## Reproducibility

The manifest stores action run IDs, artifact IDs, SHA-256 digests, AtM source
heads and pinned repository SHAs. The original artifacts remain useful for
full raw JSON while retained by GitHub Actions; this checked-in manifest
preserves the reviewed evidence identity after artifact expiry.
